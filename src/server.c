/*
 * server.c —— TCP 服务器（M6 交付主体）
 *
 * 协议：projectBrief.md §6（冻结）。行文本、空格分词、大小写不敏感。
 *   请求：PLAY | PAUSE | RESUME | NEXT | PREV | VOL n | SPEED f |
 *         ECHO on|off delay fb | EQ low|mid|high gain | STATUS | HELP | QUIT
 *   应答：OK ... / ERR <原因>；状态行 STATUS vol=.. speed=.. ...
 *
 * 职责边界：这里只解析与回执，**不直接改参数**。参数变更统一走 ctrl 队列，
 * 由主控线程做并发仲裁后再落地（boardGuide.md §5）。STATUS 是只读查询，
 * 允许就地应答，不占用仲裁通道。
 *
 * 单线程 select 多路复用：客户端上限 4，规模很小，用线程池是过度设计，
 * 而且多线程写同一 socket 还要额外加锁。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp：glibc 不在 string.h 里声明，漏了会隐式声明 */
#include <ctype.h>
#include <stdarg.h>

#include "server.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"
#include "config.h"

#if !defined(_WIN32)
#include <sys/select.h>
#include <fcntl.h>
#endif

#define TAG "server"

typedef struct {
    os_sock_t fd;
    char      in[SERVER_LINE_MAX];
    int       inlen;
    int       in_use;
} client_t;

static client_t  s_cli[SERVER_MAX_CLIENTS];
static os_sock_t s_listen = OS_SOCK_INVALID;
static int       s_port;
static int       s_inited;

static const char *HELP_TEXT =
    "OK HELP: PLAY PAUSE RESUME NEXT PREV VOL<0-100> SPEED<0.5-2.0> "
    "ECHO<on|off delay fb> EQ<low|mid|high gain> STATUS QUIT\n";

/* ---------------- 工具 ---------------- */
static int set_nonblock(os_sock_t fd)
{
#ifdef _WIN32
    u_long nb = 1;
    return ioctlsocket(fd, FIONBIO, &nb) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

static int sock_send_all(os_sock_t fd, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = (int)send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            if (os_sock_would_block()) { os_sleep_ms(1); continue; }
            return -1;
        }
        sent += n;
    }
    return 0;
}

static void reply(os_sock_t fd, const char *fmt, ...)
{
    char buf[SERVER_LINE_MAX];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\n';
    buf[n] = '\0';
    sock_send_all(fd, buf, n);
}

/* ---------------- STATUS 行（协议唯一格式出口） ---------------- */
const char *server_format_status(char *buf, int len)
{
    status_t st;

    if (!buf || len <= 0) return "";
    status_get(&st);
    /* STATUS 是协议的只读快照：参数直接取当前值，不依赖播放线程刷新。
     * 播放线程只在“有帧”时搬运 st->params；EOF/STOPPED/ERROR 时若不补这一行，
     * 客户端会看到切歌前甚至开机时的旧音量/速度/回声（M6-02 抓到）。 */
    params_get(&st.params);
    st.clients = server_client_count();      /* 在线数只有本模块知道，就地补正 */
    snprintf(buf, (size_t)len,
             "STATUS vol=%d speed=%.2f state=%s track=%d/%d pos=%d len=%d "
             "echo=%s,%d,%d eq=%d,%d,%d dist=%d gesture=%s und=%lu "
             "clients=%d q=%d\n",
             st.params.volume, st.params.speed_x10 / 10.0,
             play_state_name(st.state),
             st.track_index + 1, st.track_count, st.pos_ms, st.len_ms,
             st.params.echo_on ? "on" : "off",
             st.params.echo_delay_ms, st.params.echo_fb,
             st.params.eq_low, st.params.eq_mid, st.params.eq_high,
             st.distance_mm, gesture_name(st.gesture),
             st.underruns, st.clients, ctrl_queue_len());
    return buf;
}

/* ---------------- 指令解析 ---------------- */
static int parse_int(const char *s, int *out)
{
    char *end;
    long v;
    if (!s || !*s) return -1;
    v = strtol(s, &end, 10);
    if (*end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_speed_x10(const char *s, int *out)
{
    char *end;
    double v;
    long x10;

    if (!s || !*s) return -1;
    v = strtod(s, &end);
    if (*end != '\0') return -1;
    x10 = (long)(v * 10.0 + (v >= 0 ? 0.5 : -0.5));
    *out = (int)x10;
    return 0;
}

int server_parse_line(const char *line, ctrl_msg_t *out, char *err, int errlen)
{
    char work[SERVER_LINE_MAX];
    char *tok[6];
    int ntok = 0;
    char *p;

    if (err && errlen > 0) err[0] = '\0';
    if (!line || !out) return -1;

    snprintf(work, sizeof work, "%s", line);
    for (p = work; *p; p++) *p = (char)toupper((unsigned char)*p);

    p = strtok(work, " \t\r\n");
    while (p && ntok < 6) {
        tok[ntok++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    if (ntok == 0) return -1;

    memset(out, 0, sizeof *out);
    out->src = SRC_NET;
    out->ts_ms = now_ms();

    if (!strcmp(tok[0], "PLAY"))   { out->id = CMD_PLAY;   return 0; }
    if (!strcmp(tok[0], "PAUSE"))  { out->id = CMD_PAUSE;  return 0; }
    if (!strcmp(tok[0], "RESUME")) { out->id = CMD_RESUME; return 0; }
    if (!strcmp(tok[0], "NEXT"))   { out->id = CMD_NEXT;   return 0; }
    if (!strcmp(tok[0], "PREV"))   { out->id = CMD_PREV;   return 0; }

    if (!strcmp(tok[0], "VOL")) {
        int v;
        if (ntok < 2) { if (err) snprintf(err, (size_t)errlen, "VOL 缺少参数"); return -1; }
        if (parse_int(tok[1], &v) != 0) { if (err) snprintf(err, (size_t)errlen, "VOL 参数非整数"); return -1; }
        if (v < VOL_MIN || v > VOL_MAX) { if (err) snprintf(err, (size_t)errlen, "VOL 超出 %d..%d", VOL_MIN, VOL_MAX); return -2; }
        out->id = CMD_VOL; out->a = v;
        return 0;
    }

    if (!strcmp(tok[0], "SPEED")) {
        int x10;
        if (ntok < 2) { if (err) snprintf(err, (size_t)errlen, "SPEED 缺少参数"); return -1; }
        if (parse_speed_x10(tok[1], &x10) != 0) { if (err) snprintf(err, (size_t)errlen, "SPEED 参数非数字"); return -1; }
        if (x10 < SPEED_MIN_X10 || x10 > SPEED_MAX_X10) {
            if (err) snprintf(err, (size_t)errlen, "SPEED 超出 %.1f..%.1f",
                              SPEED_MIN_X10 / 10.0, SPEED_MAX_X10 / 10.0);
            return -2;
        }
        out->id = CMD_SPEED; out->a = x10;
        return 0;
    }

    if (!strcmp(tok[0], "ECHO")) {
        int on, delay, fb;
        if (ntok < 4) { if (err) snprintf(err, (size_t)errlen, "ECHO 需要 3 个参数：on|off delay fb"); return -1; }
        if (!strcmp(tok[1], "ON")) on = 1;
        else if (!strcmp(tok[1], "OFF")) on = 0;
        else { if (err) snprintf(err, (size_t)errlen, "ECHO 第一参数须为 on/off"); return -1; }
        if (parse_int(tok[2], &delay) != 0) { if (err) snprintf(err, (size_t)errlen, "ECHO delay 非整数"); return -1; }
        if (parse_int(tok[3], &fb) != 0) { if (err) snprintf(err, (size_t)errlen, "ECHO fb 非整数"); return -1; }
        if (delay < ECHO_DELAY_MIN_MS || delay > ECHO_DELAY_MAX_MS) {
            if (err) snprintf(err, (size_t)errlen, "ECHO delay 超出 %d..%d ms",
                              ECHO_DELAY_MIN_MS, ECHO_DELAY_MAX_MS);
            return -2;
        }
        if (fb < ECHO_FB_MIN || fb > ECHO_FB_MAX) {
            if (err) snprintf(err, (size_t)errlen, "ECHO fb 超出 %d..%d", ECHO_FB_MIN, ECHO_FB_MAX);
            return -2;
        }
        out->id = CMD_ECHO; out->a = on; out->b = delay; out->c = fb;
        return 0;
    }

    if (!strcmp(tok[0], "EQ")) {
        int band, gain;
        if (ntok < 3) { if (err) snprintf(err, (size_t)errlen, "EQ 需要 2 个参数：low|mid|high gain"); return -1; }
        if (!strcmp(tok[1], "LOW")) band = 0;
        else if (!strcmp(tok[1], "MID")) band = 1;
        else if (!strcmp(tok[1], "HIGH")) band = 2;
        else { if (err) snprintf(err, (size_t)errlen, "EQ 频段须为 low/mid/high"); return -1; }
        if (parse_int(tok[2], &gain) != 0) { if (err) snprintf(err, (size_t)errlen, "EQ 增益非整数"); return -1; }
        if (gain < EQ_GAIN_MIN_DB || gain > EQ_GAIN_MAX_DB) {
            if (err) snprintf(err, (size_t)errlen, "EQ 增益超出 %d..%d dB",
                              EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
            return -2;
        }
        out->id = CMD_EQ; out->a = band; out->b = gain;
        return 0;
    }

    if (err) snprintf(err, (size_t)errlen, "未知指令 %s", tok[0]);
    return -1;
}

/* ---------------- 连接管理 ---------------- */
static void client_close(int idx)
{
    if (!s_cli[idx].in_use) return;
    os_sock_close(s_cli[idx].fd);
    s_cli[idx].fd = OS_SOCK_INVALID;
    s_cli[idx].in_use = 0;
    s_cli[idx].inlen = 0;
    LOGT(TAG, "客户端断开，当前在线 %d", server_client_count());
}

int server_client_count(void)
{
    int i, n = 0;
    for (i = 0; i < SERVER_MAX_CLIENTS; i++) if (s_cli[i].in_use) n++;
    return n;
}

static void accept_new(void)
{
    for (;;) {
        os_sock_t fd = accept(s_listen, NULL, NULL);
        int i, slot = -1;

        if (fd == OS_SOCK_INVALID) return;
        set_nonblock(fd);

        for (i = 0; i < SERVER_MAX_CLIENTS; i++)
            if (!s_cli[i].in_use) { slot = i; break; }

        if (slot < 0) {
            sock_send_all(fd, "ERR 连接数已满\n", 18);
            os_sock_close(fd);
            LOGW(TAG, "拒绝连接：已达上限 %d", SERVER_MAX_CLIENTS);
            return;
        }
        s_cli[slot].fd = fd;
        s_cli[slot].in_use = 1;
        s_cli[slot].inlen = 0;
        LOGT(TAG, "客户端接入（slot %d），当前在线 %d", slot, server_client_count());
        reply(fd, "OK %s %s 已连接，输入 HELP 查看指令", APP_NAME, APP_VERSION);
    }
}

/* 处理一行：回执 + 投递指令 */
static void handle_line(int idx, char *line)
{
    ctrl_msg_t msg;
    char err[96];
    int rc;

    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return;

    if (!strcasecmp(line, "HELP")) { sock_send_all(s_cli[idx].fd, HELP_TEXT, (int)strlen(HELP_TEXT)); return; }
    if (!strcasecmp(line, "QUIT")) {
        sock_send_all(s_cli[idx].fd, "OK BYE\n", 7);
        client_close(idx);
        return;
    }
    if (!strcasecmp(line, "STATUS")) {
        char buf[320];
        sock_send_all(s_cli[idx].fd, server_format_status(buf, sizeof buf),
                      (int)strlen(buf));
        return;
    }

    rc = server_parse_line(line, &msg, err, sizeof err);
    if (rc < 0) {
        reply(s_cli[idx].fd, "ERR %s", err[0] ? err : "语法错误");
        return;
    }
    if (ctrl_post(&msg) != 0) {
        reply(s_cli[idx].fd, "ERR 内部队列异常");
        return;
    }
    LOGD(TAG, "投递指令 %d（%s）", (int)msg.id, line);
    reply(s_cli[idx].fd, "OK %s", line);
}

static void read_client(int idx)
{
    char chunk[256];
    int n, i;

    n = (int)recv(s_cli[idx].fd, chunk, (int)sizeof chunk, 0);
    if (n == 0) { client_close(idx); return; }
    if (n < 0) {
        if (!os_sock_would_block()) client_close(idx);
        return;
    }

    for (i = 0; i < n; i++) {
        char c = chunk[i];
        if (c == '\n') {
            s_cli[idx].in[s_cli[idx].inlen] = '\0';
            handle_line(idx, s_cli[idx].in);
            s_cli[idx].inlen = 0;
            if (!s_cli[idx].in_use) return;      /* QUIT 已关闭连接 */
        } else if (c != '\r') {
            if (s_cli[idx].inlen < SERVER_LINE_MAX - 1)
                s_cli[idx].in[s_cli[idx].inlen++] = c;
            else {
                /* 超长行直接丢弃并断开，防止无换行数据把缓冲区塞满 */
                reply(s_cli[idx].fd, "ERR 单行超过 %d 字节", SERVER_LINE_MAX - 1);
                client_close(idx);
                return;
            }
        }
    }
}

/* ---------------- 对外接口 ---------------- */
int server_init(int port)
{
    struct sockaddr_in addr;
    int on = 1;

    if (os_net_init() != 0) {
        LOGE(TAG, "网络栈初始化失败");
        return -1;
    }
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen == OS_SOCK_INVALID) {
        LOGE(TAG, "socket() 失败");
        return -1;
    }
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof addr) < 0) {
        LOGE(TAG, "bind 端口 %d 失败（可能已被占用：ps | grep -i gesture）", port);
        os_sock_close(s_listen);
        s_listen = OS_SOCK_INVALID;
        return -1;
    }
    if (listen(s_listen, SERVER_MAX_CLIENTS) < 0) {
        LOGE(TAG, "listen 失败");
        os_sock_close(s_listen);
        s_listen = OS_SOCK_INVALID;
        return -1;
    }
    set_nonblock(s_listen);

    s_port = port;
    s_inited = 1;
    LOGT(TAG, "监听 0.0.0.0:%d，最多 %d 个客户端", port, SERVER_MAX_CLIENTS);
    return 0;
}

void server_finish(void)
{
    int i;
    for (i = 0; i < SERVER_MAX_CLIENTS; i++) client_close(i);
    if (s_listen != OS_SOCK_INVALID) {
        os_sock_close(s_listen);
        s_listen = OS_SOCK_INVALID;
    }
    os_net_cleanup();
    s_inited = 0;
}

void *server_thread(void *arg)
{
    int64_t last_status = now_ms();
    (void)arg;

    if (!s_inited) return NULL;
    LOGT(TAG, "服务线程启动");

    while (!g_quit) {
        fd_set rd;
        struct timeval tv;
        int maxfd = -1;
        int i, rc;

        FD_ZERO(&rd);
        FD_SET(s_listen, &rd);
        maxfd = (int)s_listen;
        for (i = 0; i < SERVER_MAX_CLIENTS; i++) {
            if (!s_cli[i].in_use) continue;
            FD_SET(s_cli[i].fd, &rd);
            if ((int)s_cli[i].fd > maxfd) maxfd = (int)s_cli[i].fd;
        }

        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;                 /* 100ms，兼顾响应与状态广播 */
        rc = select(maxfd + 1, &rd, NULL, NULL, &tv);
        if (rc < 0) {
            if (os_sock_would_block()) continue;
            LOGE(TAG, "select 出错，服务线程退出");
            break;
        }

        if (rc > 0) {
            if (FD_ISSET(s_listen, &rd)) accept_new();
            for (i = 0; i < SERVER_MAX_CLIENTS; i++) {
                if (!s_cli[i].in_use) continue;
                if (FD_ISSET(s_cli[i].fd, &rd)) read_client(i);
            }
        }

        /* 周期状态广播：客户端据此刷新界面，不必轮询 */
        if (now_ms() - last_status >= SERVER_STATUS_INTERVAL_MS) {
            char buf[320];
            int len;
            last_status = now_ms();
            server_format_status(buf, sizeof buf);
            len = (int)strlen(buf);
            for (i = 0; i < SERVER_MAX_CLIENTS; i++)
                if (s_cli[i].in_use) sock_send_all(s_cli[i].fd, buf, len);
        }
    }

    LOGT(TAG, "服务线程退出");
    return NULL;
}
