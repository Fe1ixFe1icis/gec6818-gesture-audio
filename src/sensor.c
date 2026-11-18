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

#if BOARD
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#endif

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

/* ================= PAJ7620U2 I2C 手势（M2-03） =================
 *
 * 总线：本板 J10 = I2C CH2 = /dev/i2c-2（与 mma8653/24c04 同总线），
 *       但代码不写死总线号，上电时按 0x73 逐条探测（只探这一个地址，
 *       不做全总线扫描，避免触碰 gslX680）。
 * 读法：必须用 I2C_RDWR 组合传输（写寄存器地址 + repeated START 读）。
 *       用 i2cget 的 SMBus 方式会复现本板实测的 "ack was not received"。
 * 数据手册：Part ID 0x00=0x20 / 0x01=0x76；手势标志 0x43（8 位）
 *       + 0x44 bit0(挥动)。
 */
#define PAJ7620_I2C_ADDR   0x73
#define PAJ7620_BANK_SEL   0xEF
#define PAJ7620_GESTURE    0x43    /* 0x43 手势标志，0x44 bit0 = 挥动 */
#define PAJ7620_BUS_MAX    3       /* /dev/i2c-0 .. /dev/i2c-3 */
#define PAJ7620_PROBE_TRY  3

#if BOARD
static const unsigned char PAJ7620_INIT[][2] = {
    /* 上电初始化（bank0 / bank1） */
    {0xEF,0x00}, {0x37,0x07}, {0x38,0x17}, {0x39,0x06}, {0x41,0x00},
    {0x42,0x00}, {0x46,0x2D}, {0x47,0x0F}, {0x48,0x3C}, {0x49,0x00},
    {0x4A,0x1E}, {0x4C,0x20}, {0x51,0x10}, {0x5E,0x10}, {0x60,0x27},
    {0x80,0x42}, {0x81,0x44}, {0x82,0x04}, {0x8B,0x01}, {0x90,0x06},
    {0x95,0x0A}, {0x96,0x0C}, {0x97,0x05}, {0x9A,0x14}, {0x9C,0x3F},
    {0xA5,0x19}, {0xCC,0x19}, {0xCD,0x0B}, {0xCE,0x13}, {0xCF,0x64},
    {0xD0,0x21}, {0xEF,0x01}, {0x02,0x0F}, {0x03,0x10}, {0x04,0x02},
    {0x25,0x01}, {0x27,0x39}, {0x28,0x7F}, {0x29,0x08}, {0x3E,0xFF},
    {0x5E,0x3D}, {0x65,0x96}, {0x67,0x97}, {0x69,0xCD}, {0x6A,0x01},
    {0x6D,0x2C}, {0x6E,0x01}, {0x72,0x01}, {0x73,0x35}, {0x74,0x00},
    {0x77,0x01},
    /* 手势模式 */
    {0xEF,0x00}, {0x41,0x00}, {0x42,0x00}, {0xEF,0x00}, {0x48,0x3C},
    {0x49,0x00}, {0x51,0x10}, {0x83,0x20}, {0x9F,0xF9}, {0xEF,0x01},
    {0x01,0x1E}, {0x02,0x0F}, {0x03,0x10}, {0x04,0x02}, {0x41,0x40},
    {0x43,0x30}, {0x65,0x96}, {0x66,0x00}, {0x67,0x97}, {0x68,0x01},
    {0x69,0xCD}, {0x6A,0x01}, {0x6B,0xB0}, {0x6C,0x04}, {0x6D,0x2C},
    {0x6E,0x01}, {0x74,0x00}, {0xEF,0x00}, {0x41,0xFF}, {0x42,0x01},
};

static int s_i2c_fd  = -1;
static int s_i2c_bus = -1;

/* 写一字节寄存器：单条 i2c_msg（寄存器地址 + 数据） */
static int paj_wr(int fd, unsigned char reg, unsigned char val)
{
    unsigned char buf[2] = { reg, val };
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data d;

    msg.addr = PAJ7620_I2C_ADDR;
    msg.flags = 0;
    msg.len = 2;
    msg.buf = buf;

    d.msgs = &msg;
    d.nmsgs = 1;
    return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

/* 读寄存器：两条 i2c_msg，中间 repeated START、不发 STOP（PAJ 必须） */
static int paj_rd(int fd, unsigned char reg, unsigned char *buf, int len)
{
    struct i2c_msg msg[2];
    struct i2c_rdwr_ioctl_data d;

    msg[0].addr = PAJ7620_I2C_ADDR;
    msg[0].flags = 0;
    msg[0].len = 1;
    msg[0].buf = &reg;
    msg[1].addr = PAJ7620_I2C_ADDR;
    msg[1].flags = I2C_M_RD;
    msg[1].len = len;
    msg[1].buf = buf;

    d.msgs = msg;
    d.nmsgs = 2;
    return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

/* 打开 /dev/i2c-N 并确认 0x73 是 PAJ7620U2（读 0x00 = 0x20）。
 * 本板第一次读可能 ACK 失败，重试几次。 */
static int paj_open_bus(int bus)
{
    char path[32];
    int fd, i;
    unsigned char id = 0;

    snprintf(path, sizeof path, "/dev/i2c-%d", bus);
    fd = open(path, O_RDWR);
    if (fd < 0) return -1;

    for (i = 0; i < PAJ7620_PROBE_TRY; i++) {
        if (paj_rd(fd, 0x00, &id, 1) == 0 && id == 0x20) return fd;
        os_sleep_ms(50);
    }
    close(fd);
    return -1;
}

static int paj_init_seq(int fd)
{
    int i;
    unsigned char cfg41 = 0, cfg42 = 0, drop = 0;

    for (i = 0; i < ARRAY_LEN(PAJ7620_INIT); i++) {
        if (paj_wr(fd, PAJ7620_INIT[i][0], PAJ7620_INIT[i][1]) < 0) {
            LOGE(TAG, "PAJ7620 初始化失败：reg 0x%02x", PAJ7620_INIT[i][0]);
            return -1;
        }
    }
    os_sleep_ms(10);

    /* 回读手势使能位，并清掉上电期间可能残留的标志 */
    if (paj_rd(fd, 0x41, &cfg41, 1) == 0 && paj_rd(fd, 0x42, &cfg42, 1) == 0)
        LOGD(TAG, "PAJ 配置回读：0x41=0x%02x 0x42=0x%02x", cfg41, cfg42);
    paj_rd(fd, PAJ7620_GESTURE, &drop, 1);
    paj_rd(fd, PAJ7620_GESTURE + 1, &drop, 1);

    return 0;
}

static int paj7620_open(void)
{
    int bus;

    for (bus = 0; bus <= PAJ7620_BUS_MAX; bus++) {
        int fd = paj_open_bus(bus);

        if (fd < 0) continue;
        if (paj_init_seq(fd) < 0) {
            close(fd);
            continue;
        }
        s_i2c_fd  = fd;
        s_i2c_bus = bus;
        return 0;
    }
    return -1;
}

/* 读手势标志并映射到项目语义：
 * 0x43 bit0 Up / bit1 Down / bit2 Left / bit3 Right /
 *      bit4 Forward(PUSH) / bit5 Backward(PULL)，bit6/7 旋转本项目不用；
 * 0x44 bit0 Wave → HOVER。
 */
static gesture_t paj7620_gesture(void)
{
    unsigned char f43 = 0, f44 = 0;
    gesture_t g = GESTURE_NONE;
    static unsigned long rd_err;

    if (s_i2c_fd < 0) return GESTURE_NONE;

    if (paj_rd(s_i2c_fd, PAJ7620_GESTURE, &f43, 1) < 0) {
        if (rd_err++ % 100 == 0)
            LOGW(TAG, "读取 0x43 失败（累计 %lu 次）", rd_err);
        return GESTURE_NONE;
    }
    if (paj_rd(s_i2c_fd, PAJ7620_GESTURE + 1, &f44, 1) < 0) {
        if (rd_err++ % 100 == 0)
            LOGW(TAG, "读取 0x44 失败（累计 %lu 次）", rd_err);
    }

    if (f43 || f44)
        LOGD(TAG, "手势标志 0x43=0x%02x 0x44=0x%02x", f43, f44);

    if      (f43 & 0x01) g = GESTURE_UP;
    else if (f43 & 0x02) g = GESTURE_DOWN;
    else if (f43 & 0x04) g = GESTURE_LEFT;
    else if (f43 & 0x08) g = GESTURE_RIGHT;
    else if (f43 & 0x10) g = GESTURE_PUSH;
    else if (f43 & 0x20) g = GESTURE_PULL;
    else if (f44 & 0x01) g = GESTURE_HOVER;

    return g;
}
#endif /* BOARD */

int sensor_init(int backend)
{
    s_inited = 1;
    s_t0 = now_ms();
    s_win_len = s_win_pos = 0;
    memset(s_win, 0, sizeof s_win);

    if (backend == SENSOR_BACKEND_I2C) {
#if BOARD
        if (paj7620_open() == 0) {
            s_backend = SENSOR_BACKEND_I2C;
            LOGT(TAG, "使用 I2C 后端：PAJ7620U2 @ /dev/i2c-%d（0x73）", s_i2c_bus);
        } else {
            LOGW(TAG, "I2C 后端不可用（0x73 无应答），降级为 sim");
            s_backend = SENSOR_BACKEND_SIM;
        }
#else
        LOGW(TAG, "I2C 后端仅在板端可用（BOARD=0），降级为 sim");
        s_backend = SENSOR_BACKEND_SIM;
#endif
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
#if BOARD
    if (s_i2c_fd >= 0) {
        close(s_i2c_fd);
        s_i2c_fd  = -1;
        s_i2c_bus = -1;
    }
#endif
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
#if BOARD
        g = paj7620_gesture();
#endif
        raw = -1;   /* PAJ7620 只给手势，不给距离（sensorSelection.md §5） */
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
