/*
 * resample.c —— 抗混叠变速重采样器实现（M5Q-01）
 *
 * 用窗口 sinc 核替代原 player.c 的线性插值，step > 1（降采样）时把核按 step
 * 拉宽做抗混叠低通，消除 2.0x 变速时的折回混叠（原线性插值在 8k/15k 素材上
 * 杂散约 −27 dBc / 折叠残留约 −9 dBFS，见 tools/audio_quality.c 基线报告）。
 */
#include "resample.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Blackman 窗（|x|<=1 内），旁瓣约 −58 dB，足够满足 −40 dB 抑制阈值 */
static double blackman(double x)
{
    double ax = x < 0.0 ? -x : x;
    if (ax > 1.0) return 0.0;
    return 0.42 + 0.5 * cos(M_PI * x) + 0.08 * cos(2.0 * M_PI * x);
}

static double sinc_pi(double x)
{
    if (x > -1e-12 && x < 1e-12) return 1.0;
    return sin(M_PI * x) / (M_PI * x);
}

/* 按当前 step 重建分数相位系数表（已按各相位和归一化 → 直流增益恒为 1） */
static void rebuild_coeff(resampler_t *r)
{
    const int half = RS_TAPS / 2;
    double L = r->step > 1.0 ? r->step : 1.0;   /* 抗混叠：step>1 时拉宽核 */
    int p, t;

    for (p = 0; p < RS_PHASES; p++) {
        double frac = (double)p / (double)RS_PHASES;
        double sum = 0.0;
        for (t = 0; t < RS_TAPS; t++) {
            double d = (double)(t - (half - 1)) - frac;   /* 与理想位置的距离 */
            double x = d / L;
            double w = sinc_pi(x) * blackman(d / (double)half);
            r->coeff[p][t] = w;
            sum += w;
        }
        if (sum != 0.0) {
            for (t = 0; t < RS_TAPS; t++) r->coeff[p][t] /= sum;
        }
    }
}

void resampler_init(resampler_t *r, int in_channels, double step)
{
    memset(r, 0, sizeof *r);
    r->in_channels = in_channels;
    r->pos = 0.0;
    resampler_set_step(r, step);
}

void resampler_set_step(resampler_t *r, double step)
{
    if (step < 1e-6) step = 1e-6;
    if (step == r->step) return;
    r->step = step;
    rebuild_coeff(r);
}

int resampler_process(resampler_t *r, const int16_t *in, int in_frames,
                      int16_t *out, int out_max, int *consumed)
{
    const int ch = r->in_channels;
    const int half = RS_TAPS / 2;
    int produced = 0;

    while (produced < out_max) {
        int i0 = (int)r->pos;
        double frac;
        int phase, base, t, c;

        if (i0 + half >= in_frames) break;      /* 源不够，等下一块 */

        frac = r->pos - (double)i0;
        phase = (int)(frac * (double)RS_PHASES);
        if (phase >= RS_PHASES) phase = RS_PHASES - 1;
        if (phase < 0) phase = 0;
        base = i0 - (half - 1);

        for (c = 0; c < ch; c++) {
            double acc = 0.0;
            for (t = 0; t < RS_TAPS; t++) {
                int idx = base + t;
                if (idx < 0) idx = 0;
                if (idx >= in_frames) idx = in_frames - 1;
                acc += (double)in[idx * ch + c] * r->coeff[phase][t];
            }
            if (acc > 32767.0) acc = 32767.0;
            else if (acc < -32768.0) acc = -32768.0;
            out[produced * ch + c] = (int16_t)acc;
        }
        r->pos += r->step;
        produced++;
    }

    *consumed = (int)r->pos - (half - 1);
    if (*consumed < 0) *consumed = 0;
    if (*consumed > in_frames) *consumed = in_frames;
    r->pos -= (double)(*consumed);
    return produced;
}

long resampler_block(const int16_t *src, long src_frames, int channels,
                     int16_t *dst, long dst_frames, double step)
{
    resampler_t r;
    int consumed = 0;
    resampler_init(&r, channels, step);
    return (long)resampler_process(&r, src, (int)src_frames, dst,
                                   (int)dst_frames, &consumed);
}
