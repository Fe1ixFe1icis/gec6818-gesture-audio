/*
 * resample.h —— 抗混叠变速重采样器（M5Q-01）
 *
 * 定位：player.c 与 tools/audio_quality.c 共用的**唯一**实现，保证离线测试
 *       测的就是板端真正跑的那份重采样代码（改一处、两边同步）。
 *
 * 一步完成「变速」与「采样率转换」两件事：
 *   step = 源采样率 × 速度 / 输出采样率
 *
 * 抗混叠策略：
 *   - step <= 1（升采样/等速）：纯窗口 sinc 插值，截止 = 源奈奎斯特；
 *   - step >  1（降采样）：把 sinc 核按 step 拉宽，等效截止 = 源采样率 / (2·step)，
 *     即把「会折回输出奈奎斯特以上」的内容先低通掉，再抽取。
 *
 * 这是内部模块（非 include/ 冻结接口），供 player.c 与测试工具共用。
 */
#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <stdint.h>

#define RS_TAPS   64          /* 每相位抽头数（偶数） */
#define RS_PHASES 256         /* 分数相位表分辨率 */

typedef struct {
    int    in_channels;
    double step;                          /* 当前步长 */
    double pos;                           /* 当前缓冲内的浮点源位置（帧） */
    double coeff[RS_PHASES][RS_TAPS];     /* 预计算分数相位系数（已归一化） */
} resampler_t;

void resampler_init(resampler_t *r, int in_channels, double step);

/* 修改步长（速度/采样率变化时调用），仅在 step 变化时重算系数表 */
void resampler_set_step(resampler_t *r, double step);

/*
 * 流式重采样：从 interleaved int16 `in`（in_channels 声道）产出最多 out_max 帧
 * 到 `out`。返回实际产出帧数；*consumed 输出「已完全消费、可被调用方丢弃」的
 * 输入帧数，并已把 r->pos 相应前移（调用方只需把 in+consumed 之后的数据 memmove
 * 到缓冲头即可继续喂下一块）。
 */
int resampler_process(resampler_t *r, const int16_t *in, int in_frames,
                      int16_t *out, int out_max, int *consumed);

/* 一次性整块重采样（测试工具用：源已全部在内存），签名与旧 resample_linear 一致 */
long resampler_block(const int16_t *src, long src_frames, int channels,
                     int16_t *dst, long dst_frames, double step);

#endif /* RESAMPLE_H */
