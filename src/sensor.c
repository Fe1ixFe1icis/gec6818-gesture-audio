/*
 * sensor.c —— 手势 / 测距采集（M2 交付主体，此处为骨架 + 模拟后端）
 *
 * 后端：
 *   SIM  —— 脚本化时间线，每 3s 产生一个手势，测距按三角波在 100~400mm 摆动。
 *           确定性设计，便于测试文档写出「第几秒该看到什么」的判定标准。
 *   I2C  —— 真实传感器（型号待定，见 boardGuide.md §3.5）。当前为占位：
 *           探测 /dev/i2c-N，失败即降级 SIM 并在日志里说清楚原因。
 *
 * 手势 → 指令映射（M4 会随识别算法一起细化，改动需同步更新测试文档）：
 *   UP    音量 +5        DOWN  音量 -5
 *   RIGHT 下一首         LEFT  上一首
 *   PUSH  播放/暂停切换   PULL  速度 0.5~2.0 循环 +0.2
 *   HOVER 回声开关切换
 */
#include <stdio.h>
#include <string.h>

#include "sensor.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"

#define TAG "sensor"

/* 模拟器：每个手势占 3s，其中前 400ms 视为「手势发生」 */
#define SIM_SLOT_MS 3000

static int      s_backend = SENSOR_BACKEND_SIM;
static int      s_inited;
static int64_t  s_t0;

/* 测距滑动平均窗（boardGuide.md §4.3：窗口 ≤5 帧） */
static int      s_win[SENSOR_FILTER_WIN];
static int      s_win_len;
static int      s_win_pos;

/* 模拟器脚本：按顺序循环 */
static const gesture_t SIM_SCRIPT[] = {
    GESTURE_UP, GESTURE_RIGHT, GESTURE_DOWN, GESTURE_LEFT,
    GESTURE_PUSH, GESTURE_PULL, GESTURE_HOVER
};

static gesture_t s_prev = GESTURE_NONE;

const char *sensor_backend_name(int backend)
{
    return backend == SENSOR_BACKEND_I2C ? "i2c" : "sim";
}

static int dist_filter(int raw)
{
    int i, sum = 0;

    s_win[s_win_pos] = raw;
    s_win_pos = (s_win_pos + 1) % SENSOR_FILTER_WIN;
    if (s_win_len < SENSOR_FILTER_WIN) s_win_len++;

    for (i = 0; i < s_win_len; i++) sum += s_win[i];
    return sum / s_win_len;
}

static int i2c_probe(void)
{
    /* M2 填实：i2cdetect -l 确认总线号后，按型号初始化并读第一帧。
     * 当前只探测设备节点是否存在，避免在没接传感器时刷屏报错。 */
    FILE *fp = fopen("/dev/i2c-1", "rb");
    if (!fp) return -1;
    fclose(fp);
    return 0;
}

int sensor_init(int backend)
{
    s_inited = 1;
    s_t0 = now_ms();
    s_win_len = s_win_pos = 0;
    memset(s_win, 0, sizeof s_win);

    if (backend == SENSOR_BACKEND_I2C) {
        if (i2c_probe() == 0) {
            s_backend = SENSOR_BACKEND_I2C;
            LOGT(TAG, "使用 I2C 后端（M2 待填实读寄存器逻辑）");
        } else {
            LOGW(TAG, "I2C 后端不可用（未发现 /dev/i2c-1 或未接传感器），降级为 sim");
            s_backend = SENSOR_BACKEND_SIM;
        }
    } else {
        s_backend = SENSOR_BACKEND_SIM;
        LOGT(TAG, "使用 sim 后端：每 %d ms 一个手势，脚本长度 %d",
             SIM_SLOT_MS, ARRAY_LEN(SIM_SCRIPT));
    }
    return s_backend;
}

void sensor_finish(void)
{
    s_inited = 0;
}

gesture_t sensor_poll(int *distance_mm)
{
    int raw = -1;
    gesture_t g = GESTURE_NONE;

    if (!s_inited) {
        if (distance_mm) *distance_mm = -1;
        return GESTURE_NONE;
    }

    if (s_backend == SENSOR_BACKEND_SIM) {
        int64_t el  = now_ms() - s_t0;
        int     slot = (int)(el / SIM_SLOT_MS);
        int     phase = (int)(el % SIM_SLOT_MS);
        int     tri;

        if (phase < SENSOR_GESTURE_HOLD_MS)
            g = SIM_SCRIPT[slot % ARRAY_LEN(SIM_SCRIPT)];

        /* 三角波：4s 一个来回，100 ↔ 400mm（先升后降，不能出现负值） */
        {
            int ph = (int)(el % 4000);
            tri = (ph < 2000) ? (ph * 300 / 2000) : ((4000 - ph) * 300 / 2000);
        }
        raw = 100 + tri;
    } else {
        /* M2：读 I2C 寄存器，返回手势与测距 */
        raw = -1;
    }

    if (distance_mm) *distance_mm = raw >= 0 ? dist_filter(raw) : -1;
    return g;
}

/* 手势 → 控制指令（相对量在此换算成绝对值，因为 CMD_* 带的是目标值） */
static void dispatch_gesture(gesture_t g)
{
    audio_params_t p;

    params_get(&p);
    switch (g) {
    case GESTURE_UP:
        ctrl_post_cmd(CMD_VOL, SRC_GESTURE,
                      CLAMP(p.volume + VOL_STEP, VOL_MIN, VOL_MAX), 0, 0);
        break;
    case GESTURE_DOWN:
        ctrl_post_cmd(CMD_VOL, SRC_GESTURE,
                      CLAMP(p.volume - VOL_STEP, VOL_MIN, VOL_MAX), 0, 0);
        break;
    case GESTURE_RIGHT:
        ctrl_post_cmd(CMD_NEXT, SRC_GESTURE, 0, 0, 0);
        break;
    case GESTURE_LEFT:
        ctrl_post_cmd(CMD_PREV, SRC_GESTURE, 0, 0, 0);
        break;
    case GESTURE_PUSH:
        ctrl_post_cmd(CMD_TOGGLE, SRC_GESTURE, 0, 0, 0);
        break;
    case GESTURE_PULL: {
        int next = p.speed_x10 + 2;
        if (next > SPEED_MAX_X10) next = SPEED_MIN_X10;
        ctrl_post_cmd(CMD_SPEED, SRC_GESTURE, next, 0, 0);
        break;
    }
    case GESTURE_HOVER:
        ctrl_post_cmd(CMD_ECHO, SRC_GESTURE, !p.echo_on, p.echo_delay_ms, p.echo_fb);
        break;
    default:
        return;
    }
    LOGD(TAG, "手势 %s → 已投递指令", gesture_name(g));
}

void *sensor_thread(void *arg)
{
    (void)arg;
    LOGT(TAG, "采集线程启动，轮询周期 %d ms", SENSOR_POLL_MS);

    while (!g_quit) {
        int dist = -1;
        gesture_t g = sensor_poll(&dist);

        {
            status_t *st = status_lock();
            if (dist >= 0) st->distance_mm = dist;
            if (g != GESTURE_NONE) st->gesture = g;
            status_unlock();
        }

        /* 只在「手势发生的那一刻」投递一次，避免按住 400ms 触发 20 次 */
        if (g != GESTURE_NONE && g != s_prev) dispatch_gesture(g);
        s_prev = g;

        os_sleep_ms(SENSOR_POLL_MS);
    }
    return NULL;
}
