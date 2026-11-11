/*
 * params.h —— 共享数据结构（**接口冻结候选，M1 结论后不得擅自改**）
 *
 * 三类数据各司其职，不要混用：
 *   1. audio_params_t  可调参数（音量/变速/音效），唯一写者是主控线程
 *   2. ctrl_msg_t      控制指令消息，生产者（手势 / 网络 / 按键）投递，主控消费
 *   3. status_t        只读状态快照，播放/传感器线程写，显示/网络读
 *
 * 为什么参数与指令要分开：
 *   指令是「事件」（要排队、要仲裁、要防抖），参数是「当前值」（要随时可读、
 *   可被显示和播放线程高频采样）。用同一套结构会把连续量与离散事件搅在一起。
 */
#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include "config.h"

/* ---------------- 播放状态机 ---------------- */
typedef enum {
    PLAY_STOPPED = 0,     /* 未加载 / 已停止 */
    PLAY_PLAYING,         /* 正常推进 */
    PLAY_PAUSED,          /* 暂停，位置保留 */
    PLAY_EOF,             /* 当前曲目播完 */
    PLAY_ERROR            /* 致命错误（无文件 / 设备占用） */
} play_state_t;

/* ---------------- 可调参数 ---------------- */
typedef struct {
    int   volume;         /* VOL_MIN..VOL_MAX (%) */
    int   speed_x10;      /* 0.5..2.0 倍速，放大 10 倍存整数避免浮点比较 */
    int   echo_on;        /* 0/1 */
    int   echo_delay_ms;  /* ECHO_DELAY_MIN_MS..MAX */
    int   echo_fb;        /* 0..90 (%) */
    int   eq_low;         /* -12..+12 dB */
    int   eq_mid;
    int   eq_high;
} audio_params_t;

/* ---------------- 控制指令 ---------------- */
typedef enum {
    CMD_NONE = 0,
    CMD_PLAY,             /* 从头/继续播放 */
    CMD_PAUSE,
    CMD_RESUME,
    CMD_TOGGLE,           /* 播放/暂停切换 */
    CMD_NEXT,
    CMD_PREV,
    CMD_VOL,              /* a = 目标音量 */
    CMD_SPEED,            /* a = speed_x10 */
    CMD_ECHO,             /* a = on/off, b = delay_ms, c = feedback */
    CMD_EQ,               /* a = 0 低 / 1 中 / 2 高, b = gain dB */
    CMD_STATUS_REQ,       /* 请求立即上报一次状态 */
    CMD_QUIT              /* 退出主循环 */
} ctrl_id_t;

typedef enum {
    SRC_SYSTEM = 0,       /* 内部 / 命令行 */
    SRC_GESTURE,          /* 手势传感器 */
    SRC_NET,              /* PC 客户端 */
    SRC_BUTTON            /* 板载按键（预留） */
} ctrl_src_t;

/* 手势语义（M4 再细化识别算法，此处先冻结「语义集合」） */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_UP,
    GESTURE_DOWN,
    GESTURE_LEFT,
    GESTURE_RIGHT,
    GESTURE_PUSH,
    GESTURE_PULL,
    GESTURE_HOVER
} gesture_t;

typedef struct {
    ctrl_id_t   id;
    ctrl_src_t  src;
    int         a, b, c;      /* 含义随 id 而定，见上方注释 */
    int64_t     ts_ms;        /* 产生时刻，用于仲裁与去抖 */
} ctrl_msg_t;

/* 参数指令的合法取值范围（供协议层校验与 UI 提示共用） */
typedef struct {
    int min, max;
} range_t;

/* ---------------- 状态快照 ---------------- */
/*
 * 注意：整块结构会被加锁整体拷贝，字段只增不改语义；
 *       新增字段放在 scope 之前，避免破坏已有访问代码的偏移假设。
 */
typedef struct {
    audio_params_t params;        /* 当前生效参数（快照，只读） */
    play_state_t   state;
    int            track_index;   /* 0 基 */
    int            track_count;
    int            pos_ms;        /* 当前播放位置 */
    int            len_ms;        /* 当前曲目总长 */
    gesture_t      gesture;       /* 最近一次手势 */
    int            distance_mm;   /* 测距值，-1 表示无效 */
    unsigned long  underruns;     /* ALSA 欠载次数 */
    int            clients;       /* 在线客户端数 */
    int            fps;           /* 显示实际帧率（诊断用） */
    unsigned int   frame_no;      /* 显示帧号 */
    int16_t        scope[SCOPE_POINTS];  /* 波形取样，单声道 */
} status_t;

#endif /* PARAMS_H */
