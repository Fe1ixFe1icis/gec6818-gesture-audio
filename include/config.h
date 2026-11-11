/*
 * config.h —— 全局编译期常量
 *
 * 纪律：本文件只放「参数与常量」，不放函数声明、不放可变量。
 *       所有可调数值集中在这里，避免散落在各 .c 里形成魔法数字。
 *       修改参数范围 = 修改接口（见 AGENT.md §1 接口冻结），需全员同意。
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---------------- 版本与标识 ---------------- */
#define APP_NAME        "gesture_audio"
#define APP_VERSION     "0.1.0-m1"      /* M1 基础框架 */

/* ---------------- 音频（见 boardGuide.md §3.3） ---------------- */
#define AUDIO_RATE_HZ          44100    /* 板端原生，避免软件转换 */
#define AUDIO_CHANNELS         2        /* 立体声交织 S16_LE */
#define AUDIO_PERIOD_FRAMES    512      /* ≈11.6ms @44.1k，延迟与稳定性折中 */
#define AUDIO_BUFFER_FRAMES    2048     /* 4 × period */
#define AUDIO_BYTES_PER_FRAME  (AUDIO_CHANNELS * 2)   /* 16bit × 声道数 */

/* ---------------- 可调参数范围（协议与 UI 共用） ---------------- */
#define VOL_MIN            0
#define VOL_MAX            100
#define VOL_STEP           5

#define SPEED_MIN_X10      5            /* 0.5 倍速 */
#define SPEED_MAX_X10      20           /* 2.0 倍速 */

#define ECHO_DELAY_MIN_MS  50
#define ECHO_DELAY_MAX_MS  800
#define ECHO_FB_MIN        0
#define ECHO_FB_MAX        90           /* 百分比，留 10% 余量防自激 */

#define EQ_GAIN_MIN_DB     (-12)
#define EQ_GAIN_MAX_DB     12

/* 默认值（开机态） */
#define DEF_VOLUME         60
#define DEF_SPEED_X10      10
#define DEF_ECHO_ON        0
#define DEF_ECHO_DELAY_MS  250
#define DEF_ECHO_FB        40
#define DEF_EQ_LOW         0
#define DEF_EQ_MID         0
#define DEF_EQ_HIGH        0

/* ---------------- 曲目 ---------------- */
#define MAX_TRACKS         8
#define TRACK_PATH_MAX     256

/* ---------------- 网络（protocol 见 projectBrief.md §6） ---------------- */
#define SERVER_PORT_DEFAULT     8888
#define SERVER_MAX_CLIENTS      4
#define SERVER_LINE_MAX         256
#define SERVER_STATUS_INTERVAL_MS 500   /* 周期上报状态 */

/* ---------------- 并发仲裁（boardGuide.md §5） ---------------- */
/* 远程指令到达后 3s 内，手势指令降权（丢弃），避免两边打架 */
#define ARB_NET_HOLD_MS    3000
/* 同源同类指令去抖窗口，防传感器抖动导致连击 */
#define ARB_DUP_GUARD_MS   150

/* ---------------- 传感器 ---------------- */
#define SENSOR_POLL_MS         20       /* 轮询周期，20ms → 采样预算达标 */
#define SENSOR_FILTER_WIN      5        /* 滑动平均窗口 */
#define SENSOR_GESTURE_HOLD_MS 400      /* 模拟器手势持续时长 */
#define SENSOR_DIST_MIN_MM     30
#define SENSOR_DIST_MAX_MM     600

/* ---------------- 控制队列 ---------------- */
#define CTRL_QUEUE_LEN     32

/* ---------------- 显示（boardGuide.md §3.1） ---------------- */
#define LCD_W              800
#define LCD_H              480
#define DISPLAY_FPS        5            /* 每帧 200ms 预算，整屏提交足够 */
#define DISPLAY_ASCII_MS   500          /* 主机模拟模式的刷新间隔 */
#define SCOPE_POINTS       128          /* 波形取样点数 */

#endif /* CONFIG_H */
