/*
 * effect.c —— 回声 + 三段 EQ 基线实现
 *
 * 信号链：echo（反馈延迟） → EQ（低架 / 峰值 / 高架 biquad）
 * 顺序依据 projectBrief.md §4 的流水线定义，M5 细化时不要随意调换：
 * EQ 放在回声之后，反馈路径不会被 EQ 反复着色，听感更干净。
 *
 * 数值：全 float 单精度。板子有 VFPv4，44.1k 立体声下本链 < 100 cycles/采样
 *       （boardGuide.md §4.1），余量两个数量级，无需为性能改定点。
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effect.h"
#include "common.h"
#include "config.h"

#define TAG "effect"

/* ---------------- biquad ---------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;          /* 直接 II 型转置，每声道一份状态 */
} biquad_t;

static biquad_t s_eq[3][AUDIO_CHANNELS];   /* 0 低 / 1 中 / 2 高 */
static float    s_eq_gain[3];              /* 上次生效的增益，用于判断是否重算 */

/* ---------------- 回声 ---------------- */
static int16_t *s_echo_buf;                /* 交织立体声延迟线 */
static int      s_echo_len;                /* 帧数 */
static int      s_echo_pos;
static int      s_echo_delay_ms = -1;      /* 上次的延迟长度 */

/* ---------------- 全局配置 ---------------- */
static int         s_rate = AUDIO_RATE_HZ;
static int         s_channels = AUDIO_CHANNELS;
static effect_cfg_t s_cfg;
static int         s_inited;

static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---------------- biquad 系数（RBJ Audio EQ Cookbook） ---------------- */
static void eq_low_shelf(biquad_t *f, float rate, float f0, float gain_db)
{
    float A     = powf(10.0f, gain_db / 40.0f);
    float w0    = 2.0f * 3.14159265f * f0 / rate;
    float cosw  = cosf(w0);
    float sinw  = sinf(w0);
    float alpha = sinw / 2.0f * sqrtf(2.0f);   /* S = 1 */
    float beta  = 2.0f * sqrtf(A) * alpha;
    float a0;

    /* S = 1 的低架滤波器 */
    f->b0 =        A * ((A + 1.0f) - (A - 1.0f) * cosw + beta);
    f->b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    f->b2 =        A * ((A + 1.0f) - (A - 1.0f) * cosw - beta);
    a0    =             (A + 1.0f) + (A - 1.0f) * cosw + beta;
    f->a1 = -2.0f *     ((A - 1.0f) + (A + 1.0f) * cosw);
    f->a2 =             (A + 1.0f) + (A - 1.0f) * cosw - beta;

    f->b0 /= a0; f->b1 /= a0; f->b2 /= a0; f->a1 /= a0; f->a2 /= a0;
}

static void eq_high_shelf(biquad_t *f, float rate, float f0, float gain_db)
{
    float A     = powf(10.0f, gain_db / 40.0f);
    float w0    = 2.0f * 3.14159265f * f0 / rate;
    float cosw  = cosf(w0);
    float sinw  = sinf(w0);
    float alpha = sinw / 2.0f * sqrtf(2.0f);
    float beta  = 2.0f * sqrtf(A) * alpha;
    float a0;

    f->b0 =        A * ((A + 1.0f) + (A - 1.0f) * cosw + beta);
    f->b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    f->b2 =        A * ((A + 1.0f) + (A - 1.0f) * cosw - beta);
    a0    =             (A + 1.0f) - (A - 1.0f) * cosw + beta;
    f->a1 =  2.0f *     ((A - 1.0f) - (A + 1.0f) * cosw);
    f->a2 =             (A + 1.0f) - (A - 1.0f) * cosw - beta;

    f->b0 /= a0; f->b1 /= a0; f->b2 /= a0; f->a1 /= a0; f->a2 /= a0;
}

static void eq_peaking(biquad_t *f, float rate, float f0, float q, float gain_db)
{
    float A     = powf(10.0f, gain_db / 40.0f);
    float w0    = 2.0f * 3.14159265f * f0 / rate;
    float cosw  = cosf(w0);
    float sinw  = sinf(w0);
    float alpha = sinw / (2.0f * q);
    float a0    = 1.0f + alpha / A;

    f->b0 = (1.0f + alpha * A) / a0;
    f->b1 = (-2.0f * cosw) / a0;
    f->b2 = (1.0f - alpha * A) / a0;
    f->a1 = (-2.0f * cosw) / a0;
    f->a2 = (1.0f - alpha / A) / a0;
}

static void eq_recalc(int band)
{
    int ch;
    float gain = s_eq_gain[band];
    biquad_t f;

    memset(&f, 0, sizeof f);
    if (band == 0)      eq_low_shelf(&f, (float)s_rate, 200.0f, gain);
    else if (band == 1) eq_peaking(&f, (float)s_rate, 1000.0f, 0.7f, gain);
    else                eq_high_shelf(&f, (float)s_rate, 4000.0f, gain);

    for (ch = 0; ch < s_channels && ch < AUDIO_CHANNELS; ch++) {
        s_eq[band][ch] = f;          /* 保留系数，清状态防爆音 */
        s_eq[band][ch].z1 = 0.0f;
        s_eq[band][ch].z2 = 0.0f;
    }
    LOGD(TAG, "EQ band%d 重算，增益 %+.1f dB", band, gain);
}

static inline float bq_process(biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

/* ---------------- 延迟线 ---------------- */
static int echo_alloc(int delay_ms)
{
    int frames = (int)((long)s_rate * delay_ms / 1000);
    int16_t *p;

    if (frames < 1) frames = 1;
    p = (int16_t *)realloc(s_echo_buf,
                           (size_t)frames * s_channels * sizeof(int16_t));
    if (!p) {
        LOGE(TAG, "回声延迟线分配失败（%d 帧 × %d 声道）", frames, s_channels);
        return -1;
    }
    s_echo_buf   = p;
    s_echo_len   = frames;
    s_echo_pos   = 0;
    s_echo_delay_ms = delay_ms;
    memset(s_echo_buf, 0, (size_t)frames * s_channels * sizeof(int16_t));
    LOGD(TAG, "回声延迟线 %d ms = %d 帧（%d 字节）", delay_ms, frames,
         frames * s_channels * 2);
    return 0;
}

/* ---------------- 对外接口 ---------------- */
int effect_init(int rate, int channels)
{
    int band;

    if (rate <= 0 || channels <= 0 || channels > AUDIO_CHANNELS) {
        LOGE(TAG, "非法参数 rate=%d channels=%d", rate, channels);
        return -1;
    }
    s_rate     = rate;
    s_channels = channels;
    s_inited   = 1;

    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.echo_delay_ms = DEF_ECHO_DELAY_MS;
    s_cfg.echo_fb       = DEF_ECHO_FB;

    for (band = 0; band < 3; band++) {
        s_eq_gain[band] = 100.0f;     /* 逼迫首次 set_cfg 重算 */
    }
    effect_reset();
    return echo_alloc(s_cfg.echo_delay_ms);
}

void effect_finish(void)
{
    free(s_echo_buf);
    s_echo_buf = NULL;
    s_echo_len = s_echo_pos = 0;
    s_inited = 0;
}

void effect_reset(void)
{
    int band, ch;

    /* 只清滤波器状态与延迟线，**不清系数**。
     * 早先的写法是 memset 整个 biquad，结果 s_eq_gain 缓存仍认为「已生效」，
     * 切歌后 set_cfg 判定无需重算 → b0..a2 全为 0 → EQ 从此静音。
     * 该缺陷由 selftest #19 捕获，勿改回。 */
    for (band = 0; band < 3; band++)
        for (ch = 0; ch < AUDIO_CHANNELS; ch++) {
            s_eq[band][ch].z1 = 0.0f;
            s_eq[band][ch].z2 = 0.0f;
        }
    if (s_echo_buf && s_echo_len)
        memset(s_echo_buf, 0, (size_t)s_echo_len * s_channels * sizeof(int16_t));
    s_echo_pos = 0;
}

void effect_set_cfg(const effect_cfg_t *cfg)
{
    int want_delay;

    if (!cfg || !s_inited) return;
    s_cfg.echo_on = cfg->echo_on ? 1 : 0;
    s_cfg.echo_fb = clamp_i(cfg->echo_fb, ECHO_FB_MIN, ECHO_FB_MAX);
    s_cfg.eq_low  = clamp_i(cfg->eq_low,  EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
    s_cfg.eq_mid  = clamp_i(cfg->eq_mid,  EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
    s_cfg.eq_high = clamp_i(cfg->eq_high, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);

    want_delay = clamp_i(cfg->echo_delay_ms, ECHO_DELAY_MIN_MS, ECHO_DELAY_MAX_MS);
    if (want_delay != s_echo_delay_ms) {
        if (echo_alloc(want_delay) < 0)
            s_echo_delay_ms = want_delay;   /* 分配失败也不反复重试 */
    }
    s_cfg.echo_delay_ms = s_echo_delay_ms;

    if (s_eq_gain[0] != (float)s_cfg.eq_low)  { s_eq_gain[0] = (float)s_cfg.eq_low;  eq_recalc(0); }
    if (s_eq_gain[1] != (float)s_cfg.eq_mid)  { s_eq_gain[1] = (float)s_cfg.eq_mid;  eq_recalc(1); }
    if (s_eq_gain[2] != (float)s_cfg.eq_high) { s_eq_gain[2] = (float)s_cfg.eq_high; eq_recalc(2); }
}

int effect_active(void)
{
    if (!s_inited) return 0;
    if (s_cfg.echo_on) return 1;
    if (s_cfg.eq_low || s_cfg.eq_mid || s_cfg.eq_high) return 1;
    return 0;
}

void effect_process(int16_t *buf, int frames)
{
    int i, ch;
    int eq_on = (s_cfg.eq_low || s_cfg.eq_mid || s_cfg.eq_high) ? 1 : 0;
    float fb  = (float)s_cfg.echo_fb / 100.0f;
    const float wet = 0.7f;

    if (!s_inited || !buf || frames <= 0) return;
    if (!s_cfg.echo_on && !eq_on) return;      /* bypass 快路径 */

    /* 1) 回声 */
    if (s_cfg.echo_on && s_echo_buf && s_echo_len > 0) {
        for (i = 0; i < frames; i++) {
            int base = i * s_channels;
            for (ch = 0; ch < s_channels; ch++) {
                int idx = s_echo_pos * s_channels + ch;
                float dry = (float)buf[base + ch];
                float dly = (float)s_echo_buf[idx];
                float out = dry + wet * dly;

                if (out > 32767.0f)  out = 32767.0f;
                if (out < -32768.0f) out = -32768.0f;
                buf[base + ch] = (int16_t)out;

                /* 反馈路径带回饱和保护，禁止 fb 调到 100% 自激 */
                {
                    float fbval = dry + fb * dly;
                    if (fbval > 32767.0f)  fbval = 32767.0f;
                    if (fbval < -32768.0f) fbval = -32768.0f;
                    s_echo_buf[idx] = (int16_t)fbval;
                }
            }
            if (++s_echo_pos >= s_echo_len) s_echo_pos = 0;
        }
    }

    /* 2) EQ：三段串联，逐声道独立状态 */
    if (eq_on) {
        for (i = 0; i < frames; i++) {
            int base = i * s_channels;
            for (ch = 0; ch < s_channels; ch++) {
                float x = (float)buf[base + ch];
                x = bq_process(&s_eq[0][ch], x);
                x = bq_process(&s_eq[1][ch], x);
                x = bq_process(&s_eq[2][ch], x);
                if (x > 32767.0f)  x = 32767.0f;
                if (x < -32768.0f) x = -32768.0f;
                buf[base + ch] = (int16_t)x;
            }
        }
    }
}
