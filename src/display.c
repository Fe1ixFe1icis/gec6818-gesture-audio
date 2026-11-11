/*
 * display.c —— LCD 界面（M4 交付主体）
 *
 * 画布策略：先在内存画布（800×480×4，1.5MB 静态 BSS）上整帧绘制，
 * 再一次性提交，杜绝「画到一半」的撕裂（boardGuide.md §3.1）。
 *
 * 字库：内置 5×7 ASCII 点阵（0x20–0x5F），按整数倍放大绘制。
 * 不引入 freetype、不读字体文件 —— 板上零依赖、零解码开销。
 * 中文留到 M4 用字模工具生成数组后再加，别在这里临时挂字体库。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "display.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"
#include "config.h"

#if defined(BOARD) && BOARD
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#endif

#define TAG "display"

/* ---------------- 配色（0x00RRGGBB） ---------------- */
#define C_BG        0x000A0E12u
#define C_PANEL     0x00151C24u
#define C_EDGE      0x002B3947u
#define C_TEXT      0x00E6EDF3u
#define C_DIM       0x006D7F91u
#define C_ACCENT    0x0038D6A0u
#define C_ACCENT2   0x00489BE8u
#define C_WARN      0x00E0A030u
#define C_BARBG     0x00202A34u
#define C_GRID      0x001E2830u

/* ---------------- 5×7 点阵字库（列优先，bit0 = 顶行） ---------------- */
static const unsigned char FONT5X7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}  /* _ */
};

/* ---------------- 画布 ---------------- */
static uint32_t s_cv[LCD_W * LCD_H];
static int      s_ready;

#if defined(BOARD) && BOARD
static int             s_fb_fd = -1;
static unsigned char  *s_fb_mem;
static size_t          s_fb_len;
static size_t          s_fb_line;
#endif

static void px(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
    s_cv[y * LCD_W + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (j = y; j < y + h; j++) {
        uint32_t *row = &s_cv[j * LCD_W + x];
        for (i = 0; i < w; i++) row[i] = c;
    }
}

static void rect_outline(int x, int y, int w, int h, uint32_t c)
{
    fill_rect(x, y, w, 2, c);
    fill_rect(x, y + h - 2, w, 2, c);
    fill_rect(x, y, 2, h, c);
    fill_rect(x + w - 2, y, 2, h, c);
}

static void panel(int x, int y, int w, int h)
{
    fill_rect(x, y, w, h, C_PANEL);
    rect_outline(x, y, w, h, C_EDGE);
}

static void draw_char(int x, int y, int ch, int scale, uint32_t color)
{
    int col, row;
    const unsigned char *g;

    if (ch >= 'a' && ch <= 'z') ch -= 32;          /* 大写映射，字库只有大写 */
    if (ch < 0x20 || ch > 0x5F) ch = '?';
    g = FONT5X7[ch - 0x20];

    for (col = 0; col < 5; col++) {
        unsigned char bits = g[col];
        for (row = 0; row < 7; row++) {
            if (bits & (1u << row))
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

/* 绘制字符串，返回结束 x 坐标（便于右对齐计算） */
static int draw_text(int x, int y, const char *s, int scale, uint32_t color)
{
    while (*s) {
        draw_char(x, y, *s, scale, color);
        x += 6 * scale;
        s++;
    }
    return x;
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * 6 * scale;
}

static void draw_text_right(int right_x, int y, const char *s, int scale, uint32_t color)
{
    draw_text(right_x - text_width(s, scale), y, s, scale, color);
}

/* 单向进度条：v 在 [lo,hi] 内 */
static void bar_h(int x, int y, int w, int h, int v, int lo, int hi, uint32_t color)
{
    int fill;

    fill_rect(x, y, w, h, C_BARBG);
    if (hi <= lo) return;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    fill = (v - lo) * w / (hi - lo);
    fill_rect(x, y, fill, h, color);
}

/* 双向条（EQ 这类有中心零点的参数），零点在正中 */
static void bar_bi(int x, int y, int w, int h, int v, int lo, int hi, uint32_t color)
{
    int mid = w / 2;
    int span = (hi - lo);
    int off;

    fill_rect(x, y, w, h, C_BARBG);
    if (span <= 0) return;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    off = (v - lo) * w / span - mid;
    if (off >= 0) fill_rect(x + mid, y, off, h, color);
    else          fill_rect(x + mid + off, y, -off, h, color);
    fill_rect(x + mid, y, 2, h, C_DIM);          /* 零点刻度 */
}

static void fmt_time(char *buf, int len, int ms)
{
    int sec = ms / 1000;
    if (sec < 0) sec = 0;
    snprintf(buf, len, "%02d:%02d", sec / 60, sec % 60);
}

/* ---------------- 各区块 ---------------- */
static void draw_header(const status_t *st)
{
    const char *state = play_state_name(st->state);
    uint32_t state_color = C_ACCENT;

    fill_rect(0, 0, LCD_W, 52, C_PANEL);
    fill_rect(0, 50, LCD_W, 2, C_EDGE);

    draw_text(18, 16, "GESTURE AUDIO", 3, C_TEXT);
    draw_text(18 + text_width("GESTURE AUDIO", 3) + 18, 20, "V" APP_VERSION, 2, C_DIM);

    if (st->state == PLAY_PAUSED) state_color = C_WARN;
    if (st->state == PLAY_ERROR || st->state == PLAY_EOF) state_color = C_DIM;
    draw_text_right(LCD_W - 18, 18, state, 3, state_color);
}

static void draw_volume(const status_t *st)
{
    char num[8];
    const int x = 16, y = 66, w = 268, h = 176;

    panel(x, y, w, h);
    draw_text(x + 16, y + 14, "VOLUME", 2, C_DIM);

    snprintf(num, sizeof num, "%d", st->params.volume);
    draw_text(x + 16, y + 42, num, 9, C_TEXT);
    draw_text(x + 16 + text_width(num, 9) + 6, y + 42 + 40, "%", 2, C_ACCENT);

    bar_h(x + 16, y + h - 34, w - 32, 14, st->params.volume, VOL_MIN, VOL_MAX, C_ACCENT);
}

static void draw_row(int x0, int w, int y, const char *label, const char *value,
                     int is_bi, int v, int lo, int hi, uint32_t color)
{
    const int bar_x = x0 + 118;
    const int bar_w = w - 118 - 76;

    draw_text(x0, y + 6, label, 2, C_DIM);
    if (is_bi) bar_bi(bar_x, y + 4, bar_w, 18, v, lo, hi, color);
    else       bar_h (bar_x, y + 4, bar_w, 18, v, lo, hi, color);
    draw_text_right(x0 + w - 6, y + 6, value, 2, C_TEXT);
}

static void draw_params(const status_t *st)
{
    char v[24];
    const int x = 300, y = 66, w = 484, h = 176;

    panel(x, y, w, h);
    draw_text(x + 16, y + 8, "PARAMETERS", 1, C_DIM);

    snprintf(v, sizeof v, "%.1fX", st->params.speed_x10 / 10.0);
    draw_row(x + 16, w - 32, y + 20, "SPEED", v, 0, st->params.speed_x10,
             SPEED_MIN_X10, SPEED_MAX_X10, C_ACCENT2);

    snprintf(v, sizeof v, "%s %dMS", st->params.echo_on ? "ON" : "OFF",
             st->params.echo_delay_ms);
    draw_row(x + 16, w - 32, y + 52, "ECHO", v, 0,
             st->params.echo_on ? st->params.echo_fb : 0,
             ECHO_FB_MIN, ECHO_FB_MAX, st->params.echo_on ? C_ACCENT : C_DIM);

    snprintf(v, sizeof v, "%+dDB", st->params.eq_low);
    draw_row(x + 16, w - 32, y + 84, "EQ LOW", v, 1, st->params.eq_low,
             EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
    snprintf(v, sizeof v, "%+dDB", st->params.eq_mid);
    draw_row(x + 16, w - 32, y + 112, "EQ MID", v, 1, st->params.eq_mid,
             EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
    snprintf(v, sizeof v, "%+dDB", st->params.eq_high);
    draw_row(x + 16, w - 32, y + 140, "EQ HI", v, 1, st->params.eq_high,
             EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
}

static void draw_scope(const status_t *st)
{
    int x = 16, y = 258, w = LCD_W - 32, h = 132;
    int mid = y + h / 2;
    int i;

    panel(x, y, w, h);
    /* 网格 */
    for (i = 1; i < 4; i++) fill_rect(x, y + i * h / 4, w, 1, C_GRID);
    for (i = 1; i < 8; i++) fill_rect(x + i * w / 8, y, 1, h, C_GRID);
    fill_rect(x, mid, w, 1, C_EDGE);

    for (i = 0; i < SCOPE_POINTS - 1; i++) {
        int x0 = x + i * (w - 4) / (SCOPE_POINTS - 1) + 2;
        int x1 = x + (i + 1) * (w - 4) / (SCOPE_POINTS - 1) + 2;
        int y0 = mid - (int)((long)st->scope[i] * (h / 2 - 6) / 32768);
        int y1 = mid - (int)((long)st->scope[i + 1] * (h / 2 - 6) / 32768);
        int t;

        if (x1 - x0 <= 1) { px(x0, y0, C_ACCENT); continue; }
        for (t = 0; t <= x1 - x0; t++) {
            int yy = y0 + (y1 - y0) * t / (x1 - x0);
            px(x0 + t, yy, C_ACCENT);
            px(x0 + t, yy + 1, C_ACCENT);
        }
    }
}

static void draw_footer(const status_t *st)
{
    char buf[160], t1[16], t2[16];
    int y = 400;

    panel(16, y, LCD_W - 32, 66);

    fmt_time(t1, sizeof t1, st->pos_ms);
    fmt_time(t2, sizeof t2, st->len_ms);
    snprintf(buf, sizeof buf, "TRK %d/%d   %s/%s",
             st->track_index + 1, st->track_count > 0 ? st->track_count : 0, t1, t2);
    draw_text(30, y + 12, buf, 2, C_TEXT);

    snprintf(buf, sizeof buf, "%s  %dMM", gesture_name(st->gesture), st->distance_mm);
    draw_text(30, y + 38, buf, 2, C_ACCENT2);

    snprintf(buf, sizeof buf, "NET %d  UND %lu  FPS %d",
             st->clients, st->underruns, st->fps);
    draw_text_right(LCD_W - 30, y + 12, buf, 2, C_DIM);

    snprintf(buf, sizeof buf, "Q %d  DROP %lu",
             ctrl_queue_len(), ctrl_dropped());
    draw_text_right(LCD_W - 30, y + 38, buf, 2, C_DIM);
}

static void render(const status_t *st)
{
    fill_rect(0, 0, LCD_W, LCD_H, C_BG);
    draw_header(st);
    draw_volume(st);
    draw_params(st);
    draw_scope(st);
    draw_footer(st);
}

/* ---------------- BMP 导出（调试版式） ---------------- */
static void wr_u16(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}
static void wr_u32(FILE *f, unsigned long v)
{
    wr_u16(f, (unsigned int)(v & 0xFFFF));
    wr_u16(f, (unsigned int)((v >> 16) & 0xFFFF));
}

int display_dump_bmp(const char *path)
{
    FILE *f;
    int x, y;
    const int row_pad = (4 - (LCD_W * 3) % 4) % 4;
    const unsigned long datasz = (unsigned long)(LCD_W * 3 + row_pad) * LCD_H;
    status_t st;

    if (!path) return -1;

    /* 自渲染一帧：调用方不必关心内部画布状态 */
    status_get(&st);
    st.fps = 0;
    render(&st);

    f = fopen(path, "wb");
    if (!f) return -1;

    fputc('B', f); fputc('M', f);
    wr_u32(f, 54 + datasz);
    wr_u16(f, 0); wr_u16(f, 0);
    wr_u32(f, 54);
    wr_u32(f, 40);
    wr_u32(f, LCD_W);
    wr_u32(f, LCD_H);
    wr_u16(f, 1);
    wr_u16(f, 24);
    wr_u32(f, 0);
    wr_u32(f, datasz);
    wr_u32(f, 2835); wr_u32(f, 2835);
    wr_u32(f, 0); wr_u32(f, 0);

    for (y = LCD_H - 1; y >= 0; y--) {                 /* BMP 自下而上 */
        for (x = 0; x < LCD_W; x++) {
            uint32_t c = s_cv[y * LCD_W + x];
            fputc((int)(c & 0xFF), f);                 /* B */
            fputc((int)((c >> 8) & 0xFF), f);          /* G */
            fputc((int)((c >> 16) & 0xFF), f);         /* R */
        }
        for (x = 0; x < row_pad; x++) fputc(0, f);
    }
    fclose(f);
    return 0;
}

/* ---------------- 后端 ---------------- */
#if defined(BOARD) && BOARD
static int fb_open(void)
{
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;

    s_fb_fd = open("/dev/fb0", O_RDWR);
    if (s_fb_fd < 0) {
        LOGE(TAG, "打开 /dev/fb0 失败（需 root，且 fb 驱动要已加载）");
        return -1;
    }
    if (ioctl(s_fb_fd, FBIOGET_VSCREENINFO, &vi) < 0 ||
        ioctl(s_fb_fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        LOGE(TAG, "读取 framebuffer 参数失败");
        close(s_fb_fd);
        s_fb_fd = -1;
        return -1;
    }
    if (vi.xres != LCD_W || vi.yres != LCD_H || vi.bits_per_pixel != 32) {
        LOGW(TAG, "屏参异常：%ux%u %ubpp（期望 %dx%d 32bpp），仍按实际 line_length 提交",
             vi.xres, vi.yres, vi.bits_per_pixel, LCD_W, LCD_H);
    }

    /* 必须用 line_length × yres_virtual，用 w*h*4 在虚拟高度更大时会缺页 */
    s_fb_len  = (size_t)fi.line_length * vi.yres_virtual;
    s_fb_line = (size_t)fi.line_length;
    s_fb_mem  = (unsigned char *)mmap(NULL, s_fb_len, PROT_READ | PROT_WRITE,
                                      MAP_SHARED, s_fb_fd, 0);
    if (s_fb_mem == MAP_FAILED) {
        LOGE(TAG, "mmap framebuffer 失败（长度 %lu）", (unsigned long)s_fb_len);
        close(s_fb_fd);
        s_fb_fd = -1;
        return -1;
    }
    LOGT(TAG, "framebuffer 就绪：%ux%u line=%lu map=%lu",
         vi.xres, vi.yres, (unsigned long)s_fb_line, (unsigned long)s_fb_len);
    return 0;
}

static void fb_flush(void)
{
    int y;
    if (!s_fb_mem) return;
    for (y = 0; y < LCD_H; y++)
        memcpy(s_fb_mem + (size_t)y * s_fb_line, &s_cv[y * LCD_W], (size_t)LCD_W * 4);
}

static void fb_close(void)
{
    if (s_fb_mem) { munmap(s_fb_mem, s_fb_len); s_fb_mem = NULL; }
    if (s_fb_fd >= 0) { close(s_fb_fd); s_fb_fd = -1; }
}
#endif /* BOARD */

#if !defined(BOARD) || !BOARD
/* 主机模拟：一行仪表，便于 grep / 重定向进日志对比 */
static void emit_ascii(const status_t *st)
{
    char t1[16], t2[16];
    fmt_time(t1, sizeof t1, st->pos_ms);
    fmt_time(t2, sizeof t2, st->len_ms);
    LOGT(TAG,
         "[UI] %s %s vol=%d spd=%.1f echo=%s/%dms/%d eq=%+d/%+d/%+d %s/%s trk=%d/%d "
         "net=%d und=%lu ges=%s dist=%dmm",
         play_state_name(st->state),
         st->params.echo_on ? "ECHO" : "----",
         st->params.volume, st->params.speed_x10 / 10.0,
         st->params.echo_on ? "on" : "off", st->params.echo_delay_ms, st->params.echo_fb,
         st->params.eq_low, st->params.eq_mid, st->params.eq_high,
         t1, t2, st->track_index + 1, st->track_count,
         st->clients, st->underruns, gesture_name(st->gesture), st->distance_mm);
}
#endif

int display_init(void)
{
    s_ready = 1;
#if defined(BOARD) && BOARD
    if (fb_open() != 0) {
        LOGW(TAG, "framebuffer 不可用，显示线程降级为终端输出（业务不受影响）");
    }
#endif
    return 0;
}

void display_finish(void)
{
#if defined(BOARD) && BOARD
    fb_close();
#endif
    s_ready = 0;
}

void *display_thread(void *arg)
{
    int frame = 0;
    int64_t last = now_ms();
    status_t st;
    int interval;

    (void)arg;
#if defined(BOARD) && BOARD
    interval = 1000 / DISPLAY_FPS;
#else
    interval = DISPLAY_ASCII_MS;
#endif
    LOGT(TAG, "显示线程启动，刷新间隔 %d ms（%s）", interval,
#if defined(BOARD) && BOARD
         "framebuffer"
#else
         "终端模拟"
#endif
    );

    while (!g_quit) {
        int64_t t0 = now_ms();

        status_get(&st);
        st.frame_no = (unsigned int)frame;
        /* 真实帧率：用相邻两帧的实际间隔，而不是拿目标值冒充 */
        st.fps = (t0 > last) ? (int)(1000 / (t0 - last)) : interval > 0 ? 1000 / interval : 0;

        /* 主机模拟模式不画内存画布（省 CPU），只输出仪表行；
         * 版式检查用 --dump-bmp，避免两套渲染逻辑走偏 */
#if defined(BOARD) && BOARD
        render(&st);
        fb_flush();
#else
        emit_ascii(&st);
#endif
        frame++;

        {
            int wait = interval - (int)(now_ms() - t0);
            if (wait > 0) os_sleep_ms(wait);
        }
        last = t0;                       /* 下一帧用它算真实帧率 */
    }
    return NULL;
}
