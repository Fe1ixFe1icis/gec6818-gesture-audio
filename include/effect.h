/*
 * effect.h —— 数字音效链（M5 交付主体，此处先给可运行基线）
 *
 * 设计原则：
 *   1. 就地处理（in-place），避免每周期额外拷贝 → 省内存带宽
 *   2. init-process-reset 三段式接口，参数变更只走 set_cfg，不重建缓冲区
 *   3. 全关时走 bypass 快路径（EQ 增益全 0 直接返回），保证「关效果不掉音质」
 *
 * v1 基线：回声（环形缓冲） + 三段 biquad EQ（低架 / 峰值 / 高架）
 * M5 再补：回声反馈滤波、EQ 频点可调、变速不变调（时间伸缩）
 */
#ifndef EFFECT_H
#define EFFECT_H

#include <stdint.h>

typedef struct {
    int echo_on;
    int echo_delay_ms;
    int echo_fb;      /* 0..90 百分比 */
    int eq_low;       /* dB */
    int eq_mid;
    int eq_high;
} effect_cfg_t;

/* rate/channels 必须与 player 输出一致；重复调用视为重置 */
int  effect_init(int rate, int channels);
void effect_finish(void);
void effect_reset(void);

/* 参数变更：内部判定是否需要重算 biquad 系数与调整延迟线长度 */
void effect_set_cfg(const effect_cfg_t *cfg);

/* 就地处理 frames 帧（交织立体声，16bit） */
void effect_process(int16_t *buf, int frames);

/* 诊断：当前是否有任一效果处于生效状态（供显示与日志用） */
int  effect_active(void);

#endif /* EFFECT_H */
