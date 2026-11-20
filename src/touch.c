/*
 * touch.c —— 电容触摸输入（evdev / gslX680），M4
 *
 * 链路：
 *   /dev/input/eventN (gslX680) → EV_ABS/EV_KEY → 缩放成 800×480
 *     → tap：display_hit_test() 找命中键 → ctrl_post_cmd()
 *     → swipe：左右切歌 / 上下调音量
 *
 * 板端实测（2026-11-19，`cat /proc/bus/input/devices`）：
 *   N: Name="gslX680"  H: Handlers=mouse0 event0
 *   B: EV=b (SYN|KEY|ABS)  B: KEY=400 (BTN_TOUCH=330)  B: ABS=3 (X|Y)
 *   → 设备编号可能随内核/插拔变化，所以按名字匹配，不写死 event0。
 *
 * 坐标：原始范围不是 0..800（gslx680 常见 0..1023 或 0..4095），必须按
 * EVIOCGABS 的 min/max 缩放；方向不一致时改下面三个校准开关
 * （boardGuide.md §3.2：先用触点坐标日志确认四角，再决定是否交换/取反）。
 *
 * 依赖显示层的命中区：display.c 是版式几何的唯一 owner（src/ui_ext.h）。
 */
#include <stdio.h>
#include <string.h>

#include "touch.h"
#include "ui_ext.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"
#include "config.h"

#define TAG "touch"

/* ---------------- 校准开关（只有方向不对时才要改） ---------------- */
#define TOUCH_SWAP_XY   0   /* 1 = 交换 X/Y（面板旋转 90° 时） */
#define TOUCH_INVERT_X  0   /* 1 = X 反向 */
#define TOUCH_INVERT_Y  0   /* 1 = Y 反向 */

/* ---------------- 手势判定 ---------------- */
#define TOUCH_SWIPE_MIN 60      /* 命中区里最短滑动距离（800×480 像素） */
#define TOUCH_SWIPE_MS  500     /* 超过这个时长不算滑动，避免误切歌 */
#define TOUCH_MOVE_PX   12      /* 超过才算「动过」，否则按 tap 处理 */

#if defined(BOARD) && BOARD

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <linux/input.h>

static int  s_fd = -1;
static char s_name[64];
static int  s_min_x, s_max_x, s_min_y, s_max_y;   /* 原始坐标范围（EVIOCGABS） */
static int  s_raw_x, s_raw_y;

static int     s_down;
static int     s_await_start;   /* 等第一个 SYN_REPORT 再取起点（按下时刻坐标可能是上一帧的） */
static int     s_moved;
static int     s_start_x, s_start_y;
static int64_t s_down_ms;

static int clampv(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* 原始坐标 → 屏幕坐标（缩放 → 交换 → 取反） */
static int map_x(int raw)
{
    int span = s_max_x - s_min_x;
    int v;

    if (span <= 0) return clampv(raw, 0, LCD_W - 1);   /* 范围异常：直通，靠实测校准 */
#if TOUCH_SWAP_XY
    span = s_max_y - s_min_y;
    v = (span > 0) ? (raw - s_min_y) * (LCD_W - 1) / span : 0;
#else
    v = (raw - s_min_x) * (LCD_W - 1) / span;
#endif
    v = clampv(v, 0, LCD_W - 1);
#if TOUCH_INVERT_X
    v = LCD_W - 1 - v;
#endif
    return v;
}

static int map_y(int raw)
{
    int span = s_max_y - s_min_y;
    int v;

    if (span <= 0) return clampv(raw, 0, LCD_H - 1);
#if TOUCH_SWAP_XY
    span = s_max_x - s_min_x;
    v = (span > 0) ? (raw - s_min_x) * (LCD_H - 1) / span : 0;
#else
    v = (raw - s_min_y) * (LCD_H - 1) / span;
#endif
    v = clampv(v, 0, LCD_H - 1);
#if TOUCH_INVERT_Y
    v = LCD_H - 1 - v;
#endif
    return v;
}

/* ---------------- 键 → 指令 ---------------- */
static void post(ctrl_id_t id, int a, int b, int c)
{
    ctrl_post_cmd(id, SRC_BUTTON, a, b, c);
}

static void vol_step(int delta)
{
    audio_params_t p;
    params_get(&p);
    post(CMD_VOL, clampv(p.volume + delta, VOL_MIN, VOL_MAX), 0, 0);
}

static void eq_step(int band, int delta)
{
    audio_params_t p;
    int v;

    params_get(&p);
    v = (band == 0) ? p.eq_low : (band == 1) ? p.eq_mid : p.eq_high;
    post(CMD_EQ, band, clampv(v + delta, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB), 0);
}

static void act_key(int key)
{
    audio_params_t p;

    switch (key) {
    case UI_KEY_VOL_DOWN:    vol_step(-VOL_STEP);           break;
    case UI_KEY_VOL_UP:      vol_step(+VOL_STEP);           break;

    case UI_KEY_SPEED_DOWN:  params_get(&p);
                             post(CMD_SPEED, clampv(p.speed_x10 - 1,
                                  SPEED_MIN_X10, SPEED_MAX_X10), 0, 0);
                             break;
    case UI_KEY_SPEED_UP:    params_get(&p);
                             post(CMD_SPEED, clampv(p.speed_x10 + 1,
                                  SPEED_MIN_X10, SPEED_MAX_X10), 0, 0);
                             break;

    case UI_KEY_ECHO_TOGGLE: params_get(&p);
                             post(CMD_ECHO, !p.echo_on, p.echo_delay_ms, p.echo_fb);
                             break;

    case UI_KEY_LOW_DOWN:    eq_step(0, -3);                break;
    case UI_KEY_LOW_UP:      eq_step(0, +3);                break;
    case UI_KEY_MID_DOWN:    eq_step(1, -3);                break;
    case UI_KEY_MID_UP:      eq_step(1, +3);                break;
    case UI_KEY_HIGH_DOWN:   eq_step(2, -3);                break;
    case UI_KEY_HIGH_UP:     eq_step(2, +3);                break;

    case UI_KEY_PREV:        post(CMD_PREV, 0, 0, 0);       break;
    case UI_KEY_TOGGLE:      post(CMD_TOGGLE, 0, 0, 0);     break;
    case UI_KEY_NEXT:        post(CMD_NEXT, 0, 0, 0);       break;

    default:                                                break;
    }
}

/* ---------------- 事件处理 ---------------- */
static void on_tap(int x, int y)
{
    int key = display_hit_test(x, y);

    display_touch_mark(x, y);
    LOGD(TAG, "触点 (%d,%d) → 键 %d", x, y, key);
    if (key == UI_KEY_NONE) return;

    display_flash_key(key);
    act_key(key);
}

static void on_down(void)
{
    s_down        = 1;
    s_await_start = 1;
    s_moved       = 0;
    s_down_ms     = now_ms();
    LOGD(TAG, "按下 raw=(%d,%d)，等首个 SYN 取起点", s_raw_x, s_raw_y);
}

static void on_up(void)
{
    int x = map_x(s_raw_x), y = map_y(s_raw_y);
    int dx = x - s_start_x, dy = y - s_start_y;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int64_t dt = now_ms() - s_down_ms;

    s_down = 0;

    if (!s_moved) {
        if (dt < 800) on_tap(x, y);      /* 长按不触发业务，避免误操作 */
        return;
    }
    if (dt > TOUCH_SWIPE_MS) return;     /* 拖太慢，不当作滑动 */

    if (ax >= ay && ax >= TOUCH_SWIPE_MIN) {
        LOGD(TAG, "滑动：水平 dx=%d → %s", dx, dx < 0 ? "下一首" : "上一首");
        post(dx < 0 ? CMD_NEXT : CMD_PREV, 0, 0, 0);
    } else if (ay > ax && ay >= TOUCH_SWIPE_MIN) {
        LOGD(TAG, "滑动：垂直 dy=%d → 音量%+d", dy, dy < 0 ? +VOL_STEP : -VOL_STEP);
        vol_step(dy < 0 ? +VOL_STEP : -VOL_STEP);
    }
}

static void handle_event(const struct input_event *ev)
{
    if (ev->type == EV_ABS) {
        if (ev->code == ABS_X)      s_raw_x = ev->value;
        else if (ev->code == ABS_Y) s_raw_y = ev->value;
    } else if (ev->type == EV_KEY) {
        if (ev->code == BTN_TOUCH || ev->code == BTN_LEFT) {
            if (ev->value) on_down();
            else           on_up();
        }
    } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        int dx, dy;

        if (!s_down) return;
        if (s_await_start) {            /* 按下后的第一帧才算起点 */
            s_start_x     = map_x(s_raw_x);
            s_start_y     = map_y(s_raw_y);
            s_down_ms     = now_ms();
            s_await_start = 0;
            LOGD(TAG, "起点 (%d,%d)", s_start_x, s_start_y);
            return;
        }
        dx = map_x(s_raw_x) - s_start_x;
        dy = map_y(s_raw_y) - s_start_y;
        if (dx > TOUCH_MOVE_PX || dx < -TOUCH_MOVE_PX ||
            dy > TOUCH_MOVE_PX || dy < -TOUCH_MOVE_PX)
            s_moved = 1;
    }
}

/* 按名字找触摸设备：event0..event15 逐个 EVIOCGNAME */
static int open_by_name(void)
{
    char path[40];
    int i;

    for (i = 0; i < 16; i++) {
        int fd;
        char name[64] = "";
        struct input_absinfo ax, ay;

        snprintf(path, sizeof path, "/dev/input/event%d", i);
        fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) < 0) {
            close(fd);
            continue;
        }
        if (!strstr(name, "gsl") && !strstr(name, "X680") &&
            !strstr(name, "touch") && !strstr(name, "Touch")) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 ||
            ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) {
            LOGW(TAG, "%s（%s）没有 ABS_X/ABS_Y，跳过", path, name);
            close(fd);
            continue;
        }

        s_fd     = fd;
        s_min_x  = ax.minimum;
        s_max_x  = ax.maximum;
        s_min_y  = ay.minimum;
        s_max_y  = ay.maximum;
        snprintf(s_name, sizeof s_name, "%s", name);
        LOGT(TAG, "触摸设备：%s（%s）ABS_X %d..%d ABS_Y %d..%d",
             name, path, s_min_x, s_max_x, s_min_y, s_max_y);
        if (s_max_x <= s_min_x || s_max_y <= s_min_y) {
            LOGW(TAG, "触摸 ABS 范围异常（max<=min），坐标将直通不缩放；"
                      "用 --log debug 点四角确认后改 src/touch.c 校准开关");
        }
        return 0;
    }
    return -1;
}

int touch_init(void)
{
    if (open_by_name() != 0) {
        LOGW(TAG, "未找到触摸设备（gslX680），触摸功能关闭；界面其余功能不受影响");
        return -1;
    }
    return 0;
}

void touch_finish(void)
{
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
}

void *touch_thread(void *arg)
{
    struct pollfd pfd;

    (void)arg;
    if (s_fd < 0) return NULL;

    pfd.fd = s_fd;
    pfd.events = POLLIN;
    LOGT(TAG, "触摸线程启动（%s）", s_name);

    while (!g_quit) {
        struct input_event ev;
        ssize_t n;
        int r = poll(&pfd, 1, 200);

        if (r <= 0) continue;

        n = read(s_fd, &ev, sizeof ev);
        if (n != (ssize_t)sizeof ev) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            LOGW(TAG, "读事件失败（n=%ld errno=%d），触摸线程退出",
                 (long)n, errno);
            break;
        }
        handle_event(&ev);
    }
    return NULL;
}

#else /* 主机自测：没有 evdev，整模块退化为空实现 */

int touch_init(void)                    { return -1; }
void touch_finish(void)                 { }
void *touch_thread(void *arg)           { (void)arg; return NULL; }

#endif /* BOARD */
