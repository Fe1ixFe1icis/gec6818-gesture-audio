/*
 * effect_ext.h —— 效果链**内部扩展**接口（非冻结）
 *
 * 为什么单独放 src/ 而不是 include/：
 *   include/ 下的头文件在 M1 后冻结（AGENT.md §1），effect_cfg_t 属于共享
 *   参数结构，新增字段要走 proto/ 评审。M5Q-03 的 wet/dry 只用于板端内部
 *   预设与离线工具验证，因此以内部头文件提供 setter，板端不改协议、不改 UI。
 *
 * 使用者：
 *   - tools/audio_quality.c：验证 wet/dry 可配、回声开关行为
 *   - 自检代码（如需要）
 * 板端 player 不调用它，默认值 0.70 / 1.00 与旧实现一致。
 */
#ifndef EFFECT_EXT_H
#define EFFECT_EXT_H

/* 设置回声 wet（湿声）/ dry（干声）比例，范围各 0..1，越界自动截断。
 * 默认 wet=0.70、dry=1.00；仅在 echo_on 时对输出生效。
 * 注意：这是内部接口，不属于 include/effect.h 的冻结契约。 */
void effect_echo_set_mix(float wet, float dry);

#endif /* EFFECT_EXT_H */
