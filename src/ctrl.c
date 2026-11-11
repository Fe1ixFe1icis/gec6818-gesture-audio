/*
 * ctrl.c —— 参数存储 + 控制队列 + 状态快照
 *
 * 三段临界区各自独立加锁（不在持有 A 锁时去拿 B 锁），避免死锁。
 * 队列用「环形缓冲 + 条件变量」而不是 SysV/POSIX mq：
 *   - 所有模块同进程，进程内队列的延迟低于内核消息队列
 *   - 3.4 内核镜像的 CONFIG_POSIX_MQUEUE 未必开启，SysV 又需要额外清理
 *   - 需要拆进程时，只换本文件的实现，接口（ctrl.h）不动
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "ctrl.h"
#include "common.h"

#define TAG "ctrl"

/* ---------------- 内部状态 ---------------- */
static pthread_mutex_t s_qmtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_qcond = PTHREAD_COND_INITIALIZER;
static ctrl_msg_t      s_queue[CTRL_QUEUE_LEN];
static int             s_qhead, s_qtail, s_qcount;
static unsigned long   s_dropped;

static pthread_mutex_t s_pmtx = PTHREAD_MUTEX_INITIALIZER;
static audio_params_t  s_params;
static unsigned int    s_pver;

static pthread_mutex_t s_smtx = PTHREAD_MUTEX_INITIALIZER;
static status_t        s_status;

static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void apply_defaults_locked(void)
{
    s_params.volume        = DEF_VOLUME;
    s_params.speed_x10     = DEF_SPEED_X10;
    s_params.echo_on       = DEF_ECHO_ON;
    s_params.echo_delay_ms = DEF_ECHO_DELAY_MS;
    s_params.echo_fb       = DEF_ECHO_FB;
    s_params.eq_low        = DEF_EQ_LOW;
    s_params.eq_mid        = DEF_EQ_MID;
    s_params.eq_high       = DEF_EQ_HIGH;
}

int ctrl_init(void)
{
    pthread_mutex_lock(&s_qmtx);
    s_qhead = s_qtail = s_qcount = 0;
    s_dropped = 0;
    memset(s_queue, 0, sizeof s_queue);
    pthread_mutex_unlock(&s_qmtx);

    pthread_mutex_lock(&s_pmtx);
    apply_defaults_locked();
    s_pver = 1;
    pthread_mutex_unlock(&s_pmtx);

    pthread_mutex_lock(&s_smtx);
    memset(&s_status, 0, sizeof s_status);
    s_status.state       = PLAY_STOPPED;
    s_status.distance_mm = -1;
    s_status.params      = s_params;
    pthread_mutex_unlock(&s_smtx);

    LOGD(TAG, "初始化完成，队列容量 %d，默认音量 %d，默认速度 %.1f",
         CTRL_QUEUE_LEN, DEF_VOLUME, DEF_SPEED_X10 / 10.0);
    return 0;
}

void ctrl_finish(void)
{
    /* 唤醒可能阻塞在 take() 的消费者，让它自行退出 */
    pthread_mutex_lock(&s_qmtx);
    pthread_cond_broadcast(&s_qcond);
    pthread_mutex_unlock(&s_qmtx);
}

/* ---------------- 消息队列 ---------------- */
int ctrl_post(const ctrl_msg_t *msg)
{
    ctrl_msg_t copy;
    if (!msg) return -1;

    copy = *msg;
    if (copy.ts_ms == 0) copy.ts_ms = now_ms();

    pthread_mutex_lock(&s_qmtx);
    if (s_qcount >= CTRL_QUEUE_LEN) {
        /* 满则丢弃最旧的一条：控制指令宁可丢旧的，也不要丢最新的意图 */
        s_qhead = (s_qhead + 1) % CTRL_QUEUE_LEN;
        s_qcount--;
        s_dropped++;
    }
    s_queue[s_qtail] = copy;
    s_qtail = (s_qtail + 1) % CTRL_QUEUE_LEN;
    s_qcount++;
    pthread_cond_signal(&s_qcond);
    pthread_mutex_unlock(&s_qmtx);

    if (s_dropped && (s_dropped % 50 == 1))
        LOGW(TAG, "队列拥塞，累计丢弃 %lu 条指令（消费者跟不上）", s_dropped);
    return 0;
}

int ctrl_post_cmd(ctrl_id_t id, ctrl_src_t src, int a, int b, int c)
{
    ctrl_msg_t m;
    memset(&m, 0, sizeof m);
    m.id  = id;
    m.src = src;
    m.a = a; m.b = b; m.c = c;
    m.ts_ms = now_ms();
    return ctrl_post(&m);
}

int ctrl_take(ctrl_msg_t *out, int timeout_ms)
{
    struct timespec deadline;
    int rc = 0;

    if (!out) return -1;
    os_realtime_after_ms(&deadline, timeout_ms);

    pthread_mutex_lock(&s_qmtx);
    while (s_qcount == 0 && timeout_ms != 0) {
        rc = pthread_cond_timedwait(&s_qcond, &s_qmtx, &deadline);
        if (rc != 0) break;                 /* 超时 */
    }
    if (s_qcount == 0) {
        pthread_mutex_unlock(&s_qmtx);
        return -1;
    }
    *out = s_queue[s_qhead];
    s_qhead = (s_qhead + 1) % CTRL_QUEUE_LEN;
    s_qcount--;
    pthread_mutex_unlock(&s_qmtx);
    return 0;
}

int ctrl_queue_len(void)
{
    int n;
    pthread_mutex_lock(&s_qmtx);
    n = s_qcount;
    pthread_mutex_unlock(&s_qmtx);
    return n;
}

unsigned long ctrl_dropped(void)
{
    unsigned long n;
    pthread_mutex_lock(&s_qmtx);
    n = s_dropped;
    pthread_mutex_unlock(&s_qmtx);
    return n;
}

/* ---------------- 参数存储 ---------------- */
void params_get(audio_params_t *out)
{
    if (!out) return;
    pthread_mutex_lock(&s_pmtx);
    *out = s_params;
    pthread_mutex_unlock(&s_pmtx);
}

void params_set(const audio_params_t *in)
{
    if (!in) return;
    pthread_mutex_lock(&s_pmtx);
    s_params = *in;
    s_params.volume        = clamp_i(s_params.volume, VOL_MIN, VOL_MAX);
    s_params.speed_x10     = clamp_i(s_params.speed_x10, SPEED_MIN_X10, SPEED_MAX_X10);
    s_params.echo_delay_ms = clamp_i(s_params.echo_delay_ms,
                                     ECHO_DELAY_MIN_MS, ECHO_DELAY_MAX_MS);
    s_params.echo_fb       = clamp_i(s_params.echo_fb, ECHO_FB_MIN, ECHO_FB_MAX);
    s_params.echo_on       = s_params.echo_on ? 1 : 0;
    s_pver++;
    pthread_mutex_unlock(&s_pmtx);
}

void params_reset_default(void)
{
    pthread_mutex_lock(&s_pmtx);
    apply_defaults_locked();
    s_pver++;
    pthread_mutex_unlock(&s_pmtx);
}

unsigned int params_version(void)
{
    unsigned int v;
    pthread_mutex_lock(&s_pmtx);
    v = s_pver;
    pthread_mutex_unlock(&s_pmtx);
    return v;
}

int params_set_volume(int v)
{
    int r;
    pthread_mutex_lock(&s_pmtx);
    s_params.volume = clamp_i(v, VOL_MIN, VOL_MAX);
    r = s_params.volume;
    s_pver++;
    pthread_mutex_unlock(&s_pmtx);
    return r;
}

int params_set_speed_x10(int v)
{
    int r;
    pthread_mutex_lock(&s_pmtx);
    s_params.speed_x10 = clamp_i(v, SPEED_MIN_X10, SPEED_MAX_X10);
    r = s_params.speed_x10;
    s_pver++;
    pthread_mutex_unlock(&s_pmtx);
    return r;
}

int params_set_eq(int band, int gain_db)
{
    int r;
    pthread_mutex_lock(&s_pmtx);
    gain_db = clamp_i(gain_db, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
    switch (band) {
    case 0: s_params.eq_low  = gain_db; r = s_params.eq_low;  break;
    case 1: s_params.eq_mid  = gain_db; r = s_params.eq_mid;  break;
    case 2: s_params.eq_high = gain_db; r = s_params.eq_high; break;
    default:
        pthread_mutex_unlock(&s_pmtx);
        return -1;
    }
    s_pver++;
    pthread_mutex_unlock(&s_pmtx);
    return r;
}

int params_apply(const ctrl_msg_t *msg, int *changed)
{
    int applied = 0;

    if (changed) *changed = 0;
    if (!msg) return -1;

    pthread_mutex_lock(&s_pmtx);
    switch (msg->id) {
    case CMD_VOL:
        s_params.volume = clamp_i(msg->a, VOL_MIN, VOL_MAX);
        applied = 1;
        break;
    case CMD_SPEED:
        s_params.speed_x10 = clamp_i(msg->a, SPEED_MIN_X10, SPEED_MAX_X10);
        applied = 1;
        break;
    case CMD_ECHO:
        s_params.echo_on       = msg->a ? 1 : 0;
        s_params.echo_delay_ms = clamp_i(msg->b, ECHO_DELAY_MIN_MS, ECHO_DELAY_MAX_MS);
        s_params.echo_fb       = clamp_i(msg->c, ECHO_FB_MIN, ECHO_FB_MAX);
        applied = 1;
        break;
    case CMD_EQ:
        if (msg->a < 0 || msg->a > 2) {
            pthread_mutex_unlock(&s_pmtx);
            return -2;
        }
        if (msg->a == 0)      s_params.eq_low  = clamp_i(msg->b, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
        else if (msg->a == 1) s_params.eq_mid  = clamp_i(msg->b, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
        else                  s_params.eq_high = clamp_i(msg->b, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
        applied = 1;
        break;
    default:
        break;
    }
    if (applied) s_pver++;
    pthread_mutex_unlock(&s_pmtx);

    if (!applied) return -1;
    if (changed) *changed = 1;
    return 0;
}

/* ---------------- 状态快照 ---------------- */
void status_get(status_t *out)
{
    if (!out) return;
    pthread_mutex_lock(&s_smtx);
    *out = s_status;
    pthread_mutex_unlock(&s_smtx);
}

status_t *status_lock(void)
{
    pthread_mutex_lock(&s_smtx);
    return &s_status;
}

void status_unlock(void)
{
    pthread_mutex_unlock(&s_smtx);
}
