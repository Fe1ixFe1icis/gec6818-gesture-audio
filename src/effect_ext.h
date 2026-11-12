/*
 * effect_ext.h —— 效果链**内部扩展**接口（非冻结）
 *
 * 为什么单独放 src/ 而不是 include/：
 *   include/ 下的头文件在 M1 后冻结（AGENT.md §1），effect_cfg_t 属于共享
 *   参数结构，新增字段要走 proto/ 评审。M5Q-03 的 wet/dry 与 M5Q-06 的
 *   EQ 预设只用于板端内部选择与离线工具验证，因此以内部头文件提供接口，
 *   板端不改协议、不改 UI。
 *
 * 使用者：
 *   - tools/audio_quality.c：验证 wet/dry、EQ 三频与预设切换
 *   - src/main.c：`--eq-preset` 命令行选择（不进协议）
 * 板端 player 不调用这些函数，默认值等价旧行为。
 */
#ifndef EFFECT_EXT_H
#define EFFECT_EXT_H

/* 设置回声 wet（湿声）/ dry（干声）比例，范围各 0..1，越界自动截断。
 * 默认 wet=0.70、dry=1.00；仅在 echo_on 时对输出生效。
 * 注意：这是内部接口，不属于 include/effect.h 的冻结契约。 */
void effect_echo_set_mix(float wet, float dry);

/* ---------------- M5Q-06：EQ 内部预设 ----------------
 * 预设只改三段 EQ 的频点/Q，不改增益（增益仍走 effect_cfg_t / 协议）。
 * 切换带 30ms 新旧滤波器交叉淡化，避免爆音。
 * 注意：同样是内部接口，板端默认 standard，不对外暴露协议字段。 */
enum {
    EFFECT_EQ_PRESET_STANDARD = 0,   /* 120Hz / 1kHz Q0.7 / 6kHz */
    EFFECT_EQ_PRESET_VOCAL    = 1,   /* 160Hz / 1.2kHz Q1.0 / 5kHz */
    EFFECT_EQ_PRESET_DEMO     = 2    /* 100Hz / 1kHz Q0.7 / 8kHz */
};

int         effect_eq_preset_count(void);
const char *effect_eq_preset_name(int preset);
int         effect_eq_preset(void);
void        effect_eq_set_preset(int preset);

#endif /* EFFECT_EXT_H */
