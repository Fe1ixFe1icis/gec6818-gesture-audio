/*
 * display.c —— LCD 界面（M4 交付主体）
 *
 * 画布策略：先在内存画布（800×480×4，1.5MB 静态 BSS）上整帧绘制，
 * 再一次性提交，杜绝「画到一半」的撕裂（boardGuide.md §3.1）。
 *
 * 字库：ASCII 用内置 5×7 点阵按整数倍放大；中文用 tools/gen_font_cn.py
 * 预生成的 16×16 点阵（src/font_cn.c）。不引入 freetype、不读字体文件 ——
 * 板上零依赖、零解码开销（boardGuide.md §3.1 的坑位要求）。
 *
 * 版式（2026-11-19 M4 重构，800×480）：
 *   ┌──────────────────────────────────────────────┐
 *   │ 手势音频播放器 Vx.y.z     网/欠/帧    [播放中] │ 48px 顶栏
 *   ├──────────────┬───────────────────────────────┤
 *   │ 音量  72 %   │ 速度  ██████░░░░       1.2X    │
 *   │ ████████░░░  │ 回声  ████░░░░░░       开      │ 左 250 / 右 516
 *   │  [ - ] [ + ] │ 低音  ██░░░░██░░       +3DB    │
 *   │              │ 中音  …                        │
 *   ├──────────────┴───────────────────────────────┤
 *   │ 波形示波器 + 播放进度线                        │ 140px
 *   ├──────────────────────────────────────────────┤
 *   │ [上一首] [播放] [下一首] │ 曲目 1/3  00:12/00:38│ 64px
 *   └──────────────────────────────────────────────┘
 *
 * 触摸几何（命中区）也定义在本文件：display.c 是版式与命中区的唯一 owner，
 * touch.c 只做 evdev → 坐标 → display_hit_test() → 投递指令（src/ui_ext.h）。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "display.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"
#include "config.h"
#include "font_cn.h"
#include "ui_ext.h"

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
#define C_ERR       0x00E05A5Au
#define C_BARBG     0x00202A34u
#define C_GRID      0x001E2830u

/* ---------------- 版式常量（改版式只改这一段） ---------------- */
#define MARGIN      12
#define HDR_H       48
#define HDR_PILL_X  (LCD_W - 16 - 144)
#define HDR_PILL_Y  10
#define HDR_PILL_W  144
#define HDR_PILL_H  28

#define LEFT_X      MARGIN
#define LEFT_Y      58
#define LEFT_W      250
#define LEFT_H      192

#define RIGHT_X     (LEFT_X + LEFT_W + 10)          /* 272 */
#define RIGHT_W     (LCD_W - RIGHT_X - MARGIN)      /* 516 */
#define RIGHT_Y     LEFT_Y
#define RIGHT_H     LEFT_H

#define ROW_Y(i)    (RIGHT_Y + 10 + (i) * 36)       /* 5 行：68/104/140/176/212 */
#define BAR_X       (RIGHT_X + 64)                  /* 336 */
#define BAR_W       330
#define BAR_H       16
#define ROW_ZONE_H  36                              /* 命中区比条高：手指友好 */

#define SCOPE_X     MARGIN
#define SCOPE_Y     (LEFT_Y + LEFT_H + 8)           /* 258 */
#define SCOPE_W     (LCD_W - 2 * MARGIN)            /* 776 */
#define SCOPE_H     140

#define FOOT_Y      (SCOPE_Y + SCOPE_H + 6)         /* 404 */
#define FOOT_H      (LCD_H - FOOT_Y - MARGIN)       /* 64 */
#define BTN_W       150
#define BTN_X0      MARGIN                          /* 12 */
#define BTN_X1      (BTN_X0 + BTN_W + 12)           /* 174 */
#define BTN_X2      (BTN_X1 + BTN_W + 12)           /* 336 */
#define INFO_X      (BTN_X2 + BTN_W + 12)           /* 498 */
#define INFO_W      (LCD_W - INFO_X - MARGIN)       /* 290 */

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

/* ---------------- ASCII（5×7） ---------------- */
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

/* 绘制 ASCII 字符串，返回结束 x 坐标（便于右对齐计算） */
static int draw_text(int x, int y, const char *s, int scale, uint32_t color)
{
    while (*s) {
        draw_char(x, y, *s, scale, color);
        x += 6 * scale;
        s++;
    }
    return x;
}

/* ---------------- 中文（16×16 点阵） ---------------- */
static void draw_cn_glyph(int x, int y, const unsigned char *g, uint32_t color)
{
    int row, col;

    for (row = 0; row < FONT_CN_H; row++) {
        unsigned int bits = ((unsigned int)g[row * 2] << 8) | g[row * 2 + 1];
        for (col = 0; col < FONT_CN_W; col++) {
            if (bits & (1u << (FONT_CN_W - 1 - col)))
                px(x + col, y + row, color);
        }
    }
}

/* UTF-8 解码：返回码点并前移指针；非法/截断返回 0 */
static unsigned int utf8_next(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned int cp;
    int n, i;

    if (!*p) return 0;
    if (*p < 0x80) { *pp = p + 1; return *p; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 3; }
    else { *pp = p + 1; return 0; }

    p++;
    for (i = 0; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *pp = p; return 0; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *pp = p + n;
    return cp;
}

/*
 * 中英混排：ASCII 用 5×7×scale（在 16px 行高内垂直居中），汉字用 16×16。
 * 两种字宽不同（12/16），所以必须按字符累加而不是 strlen。
 */
static int draw_utf8(int x, int y, const char *s, int scale, uint32_t color)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p) {
        unsigned int cp = utf8_next(&p);

        if (cp == 0) break;
        if (cp < 0x80) {
            draw_char(x, y + (FONT_CN_H - 7 * scale) / 2, (int)cp, scale, color);
            x += 6 * scale;
        } else {
            int idx = font_cn_index(cp);
            if (idx >= 0) draw_cn_glyph(x, y, font_cn_glyph(idx), color);
            else          draw_char(x, y + 4, '?', 2, color);
            x += FONT_CN_W;
        }
    }
    return x;
}

static int text_utf8_width(const char *s, int scale)
{
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;

    while (*p) {
        unsigned int cp = utf8_next(&p);
        if (cp == 0) break;
        w += (cp < 0x80) ? 6 * scale : FONT_CN_W;
    }
    return w;
}

static void draw_utf8_right(int right_x, int y, const char *s, int scale, uint32_t color)
{
    draw_utf8(right_x - text_utf8_width(s, scale), y, s, scale, color);
}

/* 单字宽按钮：居中画一段文字（中英混排都吃） */
static void draw_label_center(int x, int y, int w, int h, const char *s,
                              int scale, uint32_t color)
{
    int tw = text_utf8_width(s, scale);
    draw_utf8(x + (w - tw) / 2, y + (h - FONT_CN_H) / 2, s, scale, color);
}

/* ---------------- 通用控件 ---------------- */
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

/* ---------------- 触摸命中区 ---------------- */
typedef struct {
    short x, y, w, h;
    short key;
} hit_zone_t;

static const hit_zone_t s_zones[] = {
    /* 左侧音量按钮 */
    { LEFT_X + 16,  LEFT_Y + 124, 96, 52, UI_KEY_VOL_DOWN },
    { LEFT_X + 138, LEFT_Y + 124, 96, 52, UI_KEY_VOL_UP   },
    /* 右侧参数：条的两半分别是「减 / 加」；回声整条是开关 */
    { BAR_X, ROW_Y(0), BAR_W / 2, ROW_ZONE_H, UI_KEY_SPEED_DOWN },
    { BAR_X + BAR_W / 2, ROW_Y(0), BAR_W / 2, ROW_ZONE_H, UI_KEY_SPEED_UP },
    { BAR_X, ROW_Y(1), BAR_W, ROW_ZONE_H, UI_KEY_ECHO_TOGGLE },
    { BAR_X, ROW_Y(2), BAR_W / 2, ROW_ZONE_H, UI_KEY_LOW_DOWN },
    { BAR_X + BAR_W / 2, ROW_Y(2), BAR_W / 2, ROW_ZONE_H, UI_KEY_LOW_UP },
    { BAR_X, ROW_Y(3), BAR_W / 2, ROW_ZONE_H, UI_KEY_MID_DOWN },
    { BAR_X + BAR_W / 2, ROW_Y(3), BAR_W / 2, ROW_ZONE_H, UI_KEY_MID_UP },
    { BAR_X, ROW_Y(4), BAR_W / 2, ROW_ZONE_H, UI_KEY_HIGH_DOWN },
    { BAR_X + BAR_W / 2, ROW_Y(4), BAR_W / 2, ROW_ZONE_H, UI_KEY_HIGH_UP },
    /* 底部传输键 */
    { BTN_X0, FOOT_Y, BTN_W, FOOT_H, UI_KEY_PREV },
    { BTN_X1, FOOT_Y, BTN_W, FOOT_H, UI_KEY_TOGGLE },
    { BTN_X2, FOOT_Y, BTN_W, FOOT_H, UI_KEY_NEXT }
};

static const hit_zone_t *zone_of(int key)
{
    unsigned int i;
    for (i = 0; i < sizeof s_zones / sizeof s_zones[0]; i++)
        if (s_zones[i].key == key) return &s_zones[i];
    return NULL;
}

int display_hit_test(int x, int y)
{
    unsigned int i;

    for (i = 0; i < sizeof s_zones / sizeof s_zones[0]; i++) {
        const hit_zone_t *z = &s_zones[i];
        if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h)
            return z->key;
    }
    return UI_KEY_NONE;
}

/* 触摸反馈状态：touch 线程写，display 线程读；只用于视觉反馈，不加锁 */
static volatile int     s_flash_key;
static volatile int64_t s_flash_until;
static volatile int     s_mark_x, s_mark_y;
static volatile int64_t s_mark_until;

void display_flash_key(int key)
{
    s_flash_key = key;
    s_flash_until = now_ms() + 300;
}

void display_touch_mark(int x, int y)
{
    s_mark_x = x;
    s_mark_y = y;
    s_mark_until = now_ms() + 300;
}

static void draw_key_button(int x, int y, int w, int h, const char *label,
                            int scale, uint32_t color)
{
    panel(x, y, w, h);
    draw_label_center(x, y, w, h, label, scale, color);
}

/* ---------------- 各区块 ---------------- */
static const char *state_cn(play_state_t s)
{
    switch (s) {
    case PLAY_PLAYING: return "播放中";
    case PLAY_PAUSED:  return "已暂停";
    case PLAY_EOF:     return "已结束";
    case PLAY_ERROR:   return "出错";
    case PLAY_STOPPED: /* fall through */
    default:           return "已停止";
    }
}

static uint32_t state_color(play_state_t s)
{
    switch (s) {
    case PLAY_PLAYING: return C_ACCENT;
    case PLAY_PAUSED:  return C_WARN;
    case PLAY_ERROR:   return C_ERR;
    case PLAY_EOF:     /* fall through */
    case PLAY_STOPPED: /* fall through */
    default:           return C_DIM;
    }
}

static const char *gesture_cn(gesture_t g)
{
    switch (g) {
    case GESTURE_UP:    return "上";
    case GESTURE_DOWN:  return "下";
    case GESTURE_LEFT:  return "左";
    case GESTURE_RIGHT: return "右";
    case GESTURE_PUSH:  return "推";
    case GESTURE_PULL:  return "拉";
    case GESTURE_HOVER: return "悬停";
    case GESTURE_NONE:  /* fall through */
    default:            return "无";
    }
}

static void draw_header(const status_t *st)
{
    char buf[96];
    int tw;
    uint32_t sc = state_color(st->state);

    fill_rect(0, 0, LCD_W, HDR_H, C_PANEL);
    fill_rect(0, HDR_H - 2, LCD_W, 2, C_EDGE);

    draw_utf8(16, 16, "手势音频播放器", 1, C_TEXT);
    draw_text(16 + 7 * FONT_CN_W + 12, 20, "V" APP_VERSION, 2, C_DIM);

    /* 顶栏中部：网络 / 欠载 / 帧率（诊断三件套） */
    snprintf(buf, sizeof buf, "网 %d  欠 %lu  帧 %d",
             st->clients, st->underruns, st->fps);
    draw_utf8(316, 16, buf, 2, C_DIM);

    /* 右上：状态胶囊 */
    panel(HDR_PILL_X, HDR_PILL_Y, HDR_PILL_W, HDR_PILL_H);
    tw = text_utf8_width(state_cn(st->state), 1);
    draw_utf8(HDR_PILL_X + (HDR_PILL_W - tw) / 2, HDR_PILL_Y + 6,
              state_cn(st->state), 1, sc);
}

static void draw_volume(const status_t *st)
{
    char num[8];
    int x = LEFT_X, y = LEFT_Y;
    int nx;

    panel(x, y, LEFT_W, LEFT_H);
    draw_utf8(x + 16, y + 10, "音量", 1, C_DIM);

    snprintf(num, sizeof num, "%d", st->params.volume);
    nx = draw_text(x + 16, y + 36, num, 8, C_TEXT);
    draw_utf8(nx + 8, y + 36 + 40, "%", 2, C_ACCENT);

    bar_h(x + 16, y + 104, LEFT_W - 32, 16,
          st->params.volume, VOL_MIN, VOL_MAX, C_ACCENT);

    draw_key_button(x + 16,  y + 124, 96, 52, "-", 5, C_TEXT);
    draw_key_button(x + 138, y + 124, 96, 52, "+", 5, C_TEXT);
}

static void draw_row_cn(int row_y, const char *label, const char *value,
                        int is_bi, int v, int lo, int hi, uint32_t color)
{
    draw_utf8(RIGHT_X + 16, row_y + 10, label, 1, C_DIM);
    if (is_bi) bar_bi(BAR_X, row_y + 10, BAR_W, BAR_H, v, lo, hi, color);
    else       bar_h (BAR_X, row_y + 10, BAR_W, BAR_H, v, lo, hi, color);
    draw_utf8_right(RIGHT_X + RIGHT_W - 16, row_y + 10, value, 2, C_TEXT);
}

static void draw_params(const status_t *st)
{
    char v[24];

    panel(RIGHT_X, RIGHT_Y, RIGHT_W, RIGHT_H);

    snprintf(v, sizeof v, "%.1fX", st->params.speed_x10 / 10.0);
    draw_row_cn(ROW_Y(0), "速度", v, 0, st->params.speed_x10,
                SPEED_MIN_X10, SPEED_MAX_X10, C_ACCENT2);

    draw_row_cn(ROW_Y(1), "回声", st->params.echo_on ? "开" : "关", 0,
                st->params.echo_on ? st->params.echo_fb : 0,
                0, ECHO_FB_MAX, st->params.echo_on ? C_ACCENT : C_DIM);

    snprintf(v, sizeof v, "%+dDB", st->params.eq_low);
    draw_row_cn(ROW_Y(2), "低音", v, 1, st->params.eq_low,
                EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
    snprintf(v, sizeof v, "%+dDB", st->params.eq_mid);
    draw_row_cn(ROW_Y(3), "中音", v, 1, st->params.eq_mid,
                EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
    snprintf(v, sizeof v, "%+dDB", st->params.eq_high);
    draw_row_cn(ROW_Y(4), "高音", v, 1, st->params.eq_high,
                EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB, C_ACCENT2);
}

static void draw_scope(const status_t *st)
{
    int x = SCOPE_X, y = SCOPE_Y, w = SCOPE_W, h = SCOPE_H;
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
        int y0 = mid - (int)((long)st->scope[i] * (h / 2 - 10) / 32768);
        int y1 = mid - (int)((long)st->scope[i + 1] * (h / 2 - 10) / 32768);
        int t;

        if (x1 - x0 <= 1) { px(x0, y0, C_ACCENT); continue; }
        for (t = 0; t <= x1 - x0; t++) {
            int yy = y0 + (y1 - y0) * t / (x1 - x0);
            px(x0 + t, yy, C_ACCENT);
            px(x0 + t, yy + 1, C_ACCENT);
        }
    }

    /* 播放进度：竖线 + 底部细条 */
    if (st->len_ms > 0) {
        int pos = st->pos_ms < 0 ? 0 : st->pos_ms;
        int pxl = (int)((long)pos * (w - 4) / st->len_ms);
        if (pxl > w - 4) pxl = w - 4;
        fill_rect(x + 2 + pxl, y + 2, 2, h - 6, C_ACCENT2);
        fill_rect(x, y + h - 4, w, 4, C_BARBG);
        fill_rect(x, y + h - 4, 2 + pxl, 4, C_ACCENT2);
    }
}

static void draw_footer(const status_t *st)
{
    char buf[96], t1[16], t2[16];

    draw_key_button(BTN_X0, FOOT_Y, BTN_W, FOOT_H, "上一首", 1, C_ACCENT2);
    draw_key_button(BTN_X1, FOOT_Y, BTN_W, FOOT_H,
                    st->state == PLAY_PLAYING ? "暂停" : "播放", 1, C_ACCENT);
    draw_key_button(BTN_X2, FOOT_Y, BTN_W, FOOT_H, "下一首", 1, C_ACCENT2);

    panel(INFO_X, FOOT_Y, INFO_W, FOOT_H);

    fmt_time(t1, sizeof t1, st->pos_ms);
    fmt_time(t2, sizeof t2, st->len_ms);
    snprintf(buf, sizeof buf, "曲目 %d/%d", st->track_index + 1,
             st->track_count > 0 ? st->track_count : 1);
    draw_utf8(INFO_X + 12, FOOT_Y + 8, buf, 2, C_TEXT);
    snprintf(buf, sizeof buf, "%s/%s", t1, t2);
    draw_utf8_right(INFO_X + INFO_W - 12, FOOT_Y + 8, buf, 2, C_DIM);

    snprintf(buf, sizeof buf, "手势 %s", gesture_cn(st->gesture));
    draw_utf8(INFO_X + 12, FOOT_Y + 34, buf, 2, C_ACCENT2);
    if (st->distance_mm >= 0) snprintf(buf, sizeof buf, "%dMM", st->distance_mm);
    else                      snprintf(buf, sizeof buf, "--MM");
    draw_utf8_right(INFO_X + INFO_W - 12, FOOT_Y + 34, buf, 2, C_DIM);
}

/* 触摸反馈画在最上层：按键描边 + 触点小方框 */
static void draw_feedback(void)
{
    int64_t now = now_ms();
    int key = s_flash_key;

    if (key && now < s_flash_until) {
        const hit_zone_t *z = zone_of(key);
        if (z) rect_outline(z->x, z->y, z->w, z->h, C_ACCENT);
    }
    if (now < s_mark_until) {
        int mx = s_mark_x, my = s_mark_y;
        rect_outline(mx - 8, my - 8, 16, 16, C_WARN);
        rect_outline(mx - 9, my - 9, 18, 18, C_WARN);
    }
}

static void render(const status_t *st)
{
    fill_rect(0, 0, LCD_W, LCD_H, C_BG);
    draw_header(st);
    draw_volume(st);
    draw_params(st);
    draw_scope(st);
    draw_footer(st);
    draw_feedback();
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
