/*
 * pitch.h —— 变调不变速（音高移调，M5Q-10 / G2「变调」落地）
 *
 * 定位：effect.c 与 tools/audio_quality.c 共用的内部模块（非 include/ 冻结接口），
 *       把 projectBrief §4 效果链里的「变调」占位填实。与 resample.c 同级：
 *       只做「音高变化、时长不变」；「变速」仍在 player.c/resample.c。
 *
 * 算法：时域颗粒移调（双读头 + 互补三角交叉淡化）。
 *   - 延迟线里读头按 ratio = 2^(semitones/12) 追赶/落后写头，读出波形被
 *     压缩/拉伸 → 音高按 ratio 变化，而输出仍是 1:1（时长不变）。
 *   - 两个读头相距 hop/2，用互补三角窗交叉淡化；读头越过写头（wrap）时其
 *     权重恰好为 0，掩盖相位跳变。
 *   这是相位声码器之外的轻量做法，CPU/内存低，代价是轻微「晃动」感
 *   （granular warble）。boardGuide §3.3 建议 v1 砍变调；本模块是低成本可选
 *   实现，默认关闭（semitones=0），答辩时如实说明取舍。
 */
#ifndef PITCH_H
#define PITCH_H

#include <stdint.h>

#define PITCH_SEMITONES_MAX   12.0f
#define PITCH_SEMITONES_MIN  (-12.0f)

typedef struct {
    int      channels;
    int      rate;
    float    semitones;        /* 当前移调量（半音），0 = 旁通 */
    float    ratio;            /* 2^(semitones/12) */
    int16_t *buf;              /* 延迟线（交织） */
    int      cap;              /* 容量（帧） */
    int      w;                /* 写指针（帧，0..cap-1） */
    float    lag;              /* 主读头落后写头的帧数（float） */
} pitch_t;

void pitch_init(pitch_t *p, int channels, int rate);
void pitch_finish(pitch_t *p);
void pitch_reset(pitch_t *p);
void pitch_set_semitones(pitch_t *p, float semitones);
int  pitch_active(const pitch_t *p);

/* 就地处理 frames 帧（交织 int16），时长 1:1，仅改音高。 */
void pitch_process(pitch_t *p, int16_t *buf, int frames);

#endif /* PITCH_H */
