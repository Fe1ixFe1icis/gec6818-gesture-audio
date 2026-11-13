/*
 * pitch.c —— 变调不变速（时域颗粒移调，M5Q-10）
 *
 * 见 pitch.h 的算法说明。关键推导：
 *   读头读的是 lag 帧前的历史，lag 每帧变化 (1 - ratio)。对输入正弦 f0：
 *   输出相位 = 2π f0 (n - lag(n)) / fs → 瞬时频率 = f0 (1 - d(lag)/dn)
 *             = f0 · ratio = f0 · 2^(semitones/12)。
 *   即「音高按 ratio 变化、输出帧率 1:1 不变」——变调不变速。
 */
#include "pitch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PITCH_HOP_FRAMES  2048    /* 读头 wrap 距离（≈46ms @44.1k） */
#define PITCH_MIN_GAP     64      /* 读头距写头最小间隔（线性插值余量） */
#define PITCH_BUF_EXTRA   64      /* 延迟线在 max lag 之外的余量 */

static float pitch_ratio(float semitones)
{
    return powf(2.0f, semitones / 12.0f);
}

static int pitch_buf_frames(int rate)
{
    (void)rate;
    /* cap = min_gap + hop + extra，保证读头最大 lag 始终落在已写历史内 */
    return PITCH_MIN_GAP + PITCH_HOP_FRAMES + PITCH_BUF_EXTRA;
}

void pitch_init(pitch_t *p, int channels, int rate)
{
    int cap = pitch_buf_frames(rate);

    memset(p, 0, sizeof *p);
    p->channels = channels;
    p->rate     = rate;
    p->cap      = cap;
    p->semitones = 0.0f;
    p->ratio    = 1.0f;
    p->buf = (int16_t *)calloc((size_t)cap * (size_t)channels, sizeof(int16_t));
    if (!p->buf)
        p->cap = 0;
    pitch_reset(p);
}

void pitch_finish(pitch_t *p)
{
    free(p->buf);
    p->buf = NULL;
    p->cap = 0;
    p->w   = 0;
    p->lag = 0.0f;
}

void pitch_reset(pitch_t *p)
{
    if (p->buf && p->cap > 0)
        memset(p->buf, 0, (size_t)p->cap * (size_t)p->channels * sizeof(int16_t));
    p->w = 0;
    /* 读头从 lag 区间中部起步：既不在 wrap 边界（权重为 0），也不贴写头 */
    p->lag = (float)PITCH_MIN_GAP + (float)PITCH_HOP_FRAMES * 0.5f;
}

void pitch_set_semitones(pitch_t *p, float semitones)
{
    if (semitones > PITCH_SEMITONES_MAX) semitones = PITCH_SEMITONES_MAX;
    if (semitones < PITCH_SEMITONES_MIN) semitones = PITCH_SEMITONES_MIN;
    p->semitones = semitones;
    p->ratio     = pitch_ratio(semitones);
}

int pitch_active(const pitch_t *p)
{
    /* 与 pitch_process 的旁通阈值一致：接近 0 视为关闭 */
    return p && (p->semitones > 0.01f || p->semitones < -0.01f);
}

/* 读 lag 帧前、指定声道的历史样本（线性插值）。lag ≥ 1。 */
static float pitch_read(const pitch_t *p, float lag, int ch)
{
    float pos  = (float)p->w - lag;
    int   i0   = (int)floorf(pos);
    float frac = pos - (float)i0;
    int   a    = i0 % p->cap;
    int   b    = (i0 + 1) % p->cap;
    float s0, s1;

    if (a < 0) a += p->cap;
    if (b < 0) b += p->cap;
    s0 = (float)p->buf[(size_t)a * (size_t)p->channels + (size_t)ch];
    s1 = (float)p->buf[(size_t)b * (size_t)p->channels + (size_t)ch];
    return s0 + (s1 - s0) * frac;
}

void pitch_process(pitch_t *p, int16_t *buf, int frames)
{
    const int   ch    = p->channels;
    const float hop   = (float)PITCH_HOP_FRAMES;
    const float lo    = (float)PITCH_MIN_GAP;
    const float hi    = lo + hop;          /* lag ∈ [lo, hi) */
    const float ratio = p->ratio;
    int i, c;

    if (!p->buf || p->cap <= 0 || !buf || frames <= 0) return;

    /* 无移调：旁通。直接返回，避免引入固定延迟与静态梳状滤波 */
    if (!pitch_active(p)) return;

    for (i = 0; i < frames; i++) {
        int base = i * ch;

        /* 1) 写入当前帧到延迟线 */
        for (c = 0; c < ch; c++)
            p->buf[(size_t)p->w * (size_t)ch + (size_t)c] = buf[base + c];
        if (++p->w >= p->cap) p->w = 0;

        /* 2) 推进读头（lag 每帧变化 1-ratio），wrap 回 [lo, hi) */
        p->lag += (1.0f - ratio);
        if (p->lag < lo)       p->lag += hop;
        else if (p->lag >= hi) p->lag -= hop;

        /* 3) 两个读头互补三角交叉淡化（wA + wB = 1） */
        {
            float pn   = (p->lag - lo) / hop;      /* 归一化 0..1 */
            float frac = pn - floorf(pn);
            float wA   = 1.0f - fabsf(2.0f * frac - 1.0f);
            float wB   = 1.0f - wA;
            float lagB = p->lag + hop * 0.5f;
            if (lagB >= hi) lagB -= hop;

            for (c = 0; c < ch; c++) {
                float a = pitch_read(p, p->lag, c);
                float b = pitch_read(p, lagB, c);
                float y = wA * a + wB * b;

                if (y > 32767.0f)      y = 32767.0f;
                else if (y < -32768.0f) y = -32768.0f;
                buf[base + c] = (int16_t)y;
            }
        }
    }
}
