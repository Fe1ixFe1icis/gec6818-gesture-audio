/*
 * effect.c —— 回声 + 三段 EQ（M5Q-01…04 增强后的实现）
 *
 * 信号链：echo（反馈延迟） → EQ（低架 / 峰值 / 高架 biquad）
 * 顺序依据 projectBrief.md §4 的流水线定义，M5 细化时不要随意调换：
 * EQ 放在回声之后，反馈路径不会被 EQ 反复着色，听感更干净。
 *
 * M5Q 已落地：
 *   - 01 抗混叠重采样（在 player.c/resample.c，不在本文件）
 *   - 02 EQ 增益 30ms 斜坡 + 切系数保留 z1/z2（无咔哒）
 *   - 03 回声预分配延迟线、反馈 4kHz 阻尼、改延迟 20ms 交叉淡化、wet/dry
 *   - 04 soft_limit 软限幅替代硬 clamp（0.9FS 拐点，线性区零失真）
 *
 * 数值：全 float 单精度。板子有 VFPv4，44.1k 立体声下本链 < 100 cycles/采样
 *       （boardGuide.md §4.1），余量两个数量级，无需为性能改定点。
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effect.h"
#include "effect_ext.h"
#include "common.h"
#include "config.h"

#define TAG "effect"
#define EQ_RAMP_MS 30      /* M5Q-02：EQ 增益切换斜坡时长（20–50ms 取 30ms） */

/* ---------------- biquad ---------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;          /* 直接 II 型转置，每声道一份状态 */
} biquad_t;

static biquad_t s_eq[3][AUDIO_CHANNELS];   /* 0 低 / 1 中 / 2 高 */
static float    s_eq_gain[3];              /* 目标增益 dB（M5Q-02：斜坡终点） */
static float    s_eq_cur_gain[3];          /* 当前实际生效增益 dB（斜坡起点） */
static float    s_eq_step[3];              /* 每采样增益斜坡 dB（0 = 已到位） */
static int      s_eq_primed;               /* 是否已收到首次 set_cfg */

/* ---------------- 回声 ---------------- */
/* M5Q-03：延迟线按 **最大 800ms** 一次分配，改延迟只改读写指针 + 交叉淡化，
 * 不再 realloc / memset（旧实现每次改延迟都清空尾部，听感上是“回声断掉又接上”，
 * 且分配失败会半途改状态）。 */
#define ECHO_DAMP_HZ    4000.0f    /* 反馈路径一阶低通截止（任务要求 3–5kHz） */
#define ECHO_XFADE_MS   20         /* 改延迟时新旧抽头的交叉淡化时长 */
#define ECHO_WET_DEF    0.70f      /* 默认湿声比例（原实现写死 0.7） */
#define ECHO_DRY_DEF    1.00f      /* 默认干声比例 */

static int16_t *s_echo_buf;                /* 交织立体声延迟线，容量 = 800ms */
static int      s_echo_cap;                /* 容量（帧），固定不变 */
static int      s_echo_w;                  /* 写指针（帧） */
static int      s_echo_delay_ms = -1;      /* 当前目标延迟（ms） */
static int      s_echo_delay_f;            /* 当前目标延迟（帧） */
static int      s_echo_prev_f;             /* 交叉淡化起点延迟（帧） */
static int      s_echo_xf_pos;             /* 交叉淡化进度（帧） */
static int      s_echo_xf_len;             /* 交叉淡化总长（帧，0 = 未进行） */
static int      s_echo_was_on;             /* 上一轮 echo_on，用于上升沿清线 */
static float    s_echo_wet = ECHO_WET_DEF; /* 湿声比例（M5Q-03 可配） */
static float    s_echo_dry = ECHO_DRY_DEF; /* 干声比例（M5Q-03 可配） */
static float    s_echo_damp_a;             /* 反馈一阶低通系数 */
static float    s_echo_damp_z[AUDIO_CHANNELS];  /* 反馈低通状态，每声道一份 */

/* ---------------- 全局配置 ---------------- */
static int         s_rate = AUDIO_RATE_HZ;
static int         s_channels = AUDIO_CHANNELS;
static effect_cfg_t s_cfg;
static int         s_inited;

static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* M5Q-04 软限幅：|x| <= 拐点（0.9FS）时线性直通，之上用软拐点渐近饱和。
 * 输出严格落在 (-32768, 32767) 内，保证 int16 截断不会产生 ±32767/-32768
 * 的硬削波样本。线性区保证小信号零失真（THD、频响不受影响）。 */
static inline float soft_limit(float x)
{
    const float fs   = 32767.0f;
    const float knee = 0.90f * fs;    /* ≈ -0.9dBFS */
    const float w    = 0.10f * fs;    /* 拐点宽度 */
    float ax = x < 0.0f ? -x : x;

    if (ax <= knee)
        return x;                     /* 线性区 */

    {
        float t = (ax - knee) / w;
        float g = t / (1.0f + t);     /* 0..1，渐近但永不等于 1 */
        float y = knee + g * (fs - knee);
        return x < 0.0f ? -y : y;
    }
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
    float gain = s_eq_cur_gain[band];
    biquad_t f;

    memset(&f, 0, sizeof f);
    if (band == 0)      eq_low_shelf(&f, (float)s_rate, 200.0f, gain);
    else if (band == 1) eq_peaking(&f, (float)s_rate, 1000.0f, 0.7f, gain);
    else                eq_high_shelf(&f, (float)s_rate, 4000.0f, gain);

    for (ch = 0; ch < s_channels && ch < AUDIO_CHANNELS; ch++) {
        /* M5Q-02：切系数**保留** z1/z2（不清状态）。清状态会把滤波器内部
         * 能量一次性倾倒出来，正是"咔哒/爆音"的来源。 */
        float z1 = s_eq[band][ch].z1;
        float z2 = s_eq[band][ch].z2;
        s_eq[band][ch] = f;
        s_eq[band][ch].z1 = z1;
        s_eq[band][ch].z2 = z2;
    }
}

static inline float bq_process(biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

/* ---------------- 延迟线（M5Q-03） ---------------- */
static int echo_delay_frames(int delay_ms)
{
    int frames = (int)((long)s_rate * delay_ms / 1000);
    int maxf   = (int)((long)s_rate * ECHO_DELAY_MAX_MS / 1000);

    if (frames < 1) frames = 1;
    if (frames > maxf) frames = maxf;
    return frames;
}

/* 一次分配到最大延迟，之后永不 realloc。 */
static int echo_alloc_max(void)
{
    int frames = echo_delay_frames(ECHO_DELAY_MAX_MS);
    int16_t *p;

    p = (int16_t *)calloc((size_t)frames * s_channels, sizeof(int16_t));
    if (!p) {
        LOGE(TAG, "回声延迟线分配失败（%d 帧 × %d 声道）", frames, s_channels);
        return -1;
    }
    free(s_echo_buf);
    s_echo_buf = p;
    s_echo_cap = frames;
    LOGD(TAG, "回声延迟线 %d ms 容量 = %d 帧（%d 字节，一次性预分配）",
         ECHO_DELAY_MAX_MS, frames, frames * s_channels * 2);
    return 0;
}

/* 清空延迟线并复位回声内部状态（不改目标延迟与 wet/dry）。 */
static void echo_clear(void)
{
    if (s_echo_buf && s_echo_cap > 0)
        memset(s_echo_buf, 0,
               (size_t)s_echo_cap * s_channels * sizeof(int16_t));
    s_echo_w      = 0;
    s_echo_xf_pos = 0;
    s_echo_xf_len = 0;
    s_echo_prev_f = s_echo_delay_f;
    {
        int ch;
        for (ch = 0; ch < AUDIO_CHANNELS; ch++) s_echo_damp_z[ch] = 0.0f;
    }
}

/* 改目标延迟：只动指针状态，缓冲区内容原样保留；用 20ms 交叉淡化从旧抽头
 * 过渡到新抽头，避免回声相位跳变产生“咔/爆”。 */
static void echo_set_delay(int delay_ms)
{
    int frames = echo_delay_frames(delay_ms);

    if (frames == s_echo_delay_f) {
        s_echo_delay_ms = delay_ms;
        return;
    }

    LOGD(TAG, "回声延迟 %d ms → %d ms（%d 帧交叉淡化，不清延迟线）",
         s_echo_delay_ms, delay_ms, (int)((long)s_rate * ECHO_XFADE_MS / 1000));
    s_echo_delay_ms = delay_ms;
    s_echo_prev_f = s_echo_delay_f;
    s_echo_delay_f = frames;
    s_echo_xf_pos  = 0;
    s_echo_xf_len  = (int)((long)s_rate * ECHO_XFADE_MS / 1000);
    if (s_echo_xf_len < 1) s_echo_xf_len = 1;
}

/* ---------------- 对外接口 ---------------- */
int effect_init(int rate, int channels)
{
    int band, ch;

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

    /* 回声状态：先定目标延迟再分配/清线，reset 里要用到 s_echo_delay_f */
    s_echo_delay_ms = DEF_ECHO_DELAY_MS;
    s_echo_delay_f  = echo_delay_frames(DEF_ECHO_DELAY_MS);
    s_echo_prev_f   = s_echo_delay_f;
    s_echo_xf_pos   = 0;
    s_echo_xf_len   = 0;
    s_echo_was_on   = 0;
    s_echo_wet      = ECHO_WET_DEF;
    s_echo_dry      = ECHO_DRY_DEF;
    /* 一阶低通 a = 1 - exp(-2π fc/fs)，fc=4kHz @44.1k → ≈0.435 */
    s_echo_damp_a   = 1.0f - expf(-2.0f * 3.14159265f * ECHO_DAMP_HZ
                                  / (float)s_rate);

    for (band = 0; band < 3; band++) {
        s_eq_gain[band]     = 0.0f;
        s_eq_cur_gain[band] = 0.0f;
        s_eq_step[band]     = 0.0f;
    }
    s_eq_primed = 0;
    /* 初始化为单位增益（b0=1），保证首次 set_cfg 前的任何处理都等同旁通 */
    for (band = 0; band < 3; band++)
        for (ch = 0; ch < AUDIO_CHANNELS; ch++) {
            memset(&s_eq[band][ch], 0, sizeof s_eq[band][ch]);
            s_eq[band][ch].b0 = 1.0f;
        }
    if (echo_alloc_max() != 0)
        return -1;
    effect_reset();
    return 0;
}

void effect_finish(void)
{
    free(s_echo_buf);
    s_echo_buf = NULL;
    s_echo_cap = s_echo_w = 0;
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
    /* M5Q-03：切歌清延迟线是必要的（旧音频不能串到新曲目），
     * 但**改延迟**不再走这里——见 echo_set_delay()。 */
    echo_clear();

    /* M5Q-02：切歌/复位后没有信号连续性，增益无需斜坡，直接落到目标并重算
     * 系数（z1/z2 刚已清零，重算后保持零态）。 */
    for (band = 0; band < 3; band++) {
        if (s_eq_step[band] != 0.0f || s_eq_cur_gain[band] != s_eq_gain[band]) {
            s_eq_cur_gain[band] = s_eq_gain[band];
            s_eq_step[band]     = 0.0f;
            eq_recalc(band);
        }
    }
}

void effect_set_cfg(const effect_cfg_t *cfg)
{
    int want_delay, band;

    if (!cfg || !s_inited) return;
    s_cfg.echo_on = cfg->echo_on ? 1 : 0;
    s_cfg.echo_fb = clamp_i(cfg->echo_fb, ECHO_FB_MIN, ECHO_FB_MAX);
    s_cfg.eq_low  = clamp_i(cfg->eq_low,  EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
    s_cfg.eq_mid  = clamp_i(cfg->eq_mid,  EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);
    s_cfg.eq_high = clamp_i(cfg->eq_high, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB);

    want_delay = clamp_i(cfg->echo_delay_ms, ECHO_DELAY_MIN_MS, ECHO_DELAY_MAX_MS);
    /* M5Q-03：只改指针 + 交叉淡化，不重新分配、不清延迟线 */
    if (want_delay != s_echo_delay_ms)
        echo_set_delay(want_delay);
    s_cfg.echo_delay_ms = s_echo_delay_ms;

    for (band = 0; band < 3; band++) {
        float target = (band == 0) ? (float)s_cfg.eq_low :
                       (band == 1) ? (float)s_cfg.eq_mid :
                                     (float)s_cfg.eq_high;

        if (!s_eq_primed) {
            /* 首次：无历史信号，直接到位 */
            s_eq_gain[band]     = target;
            s_eq_cur_gain[band] = target;
            s_eq_step[band]     = 0.0f;
            eq_recalc(band);
            continue;
        }
        if (s_eq_gain[band] == target)
            continue;                 /* 目标未变：斜坡继续或已停，不动 */

        s_eq_gain[band] = target;
        {
            float ramp = (float)s_rate * (EQ_RAMP_MS / 1000.0f);
            s_eq_step[band] = (target - s_eq_cur_gain[band]) / ramp;
            if (s_eq_step[band] == 0.0f)
                eq_recalc(band);      /* 目标恰等于当前（几乎不会）：直接到位 */
        }
        LOGD(TAG, "EQ band%d 目标 %+.1f dB（%d ms 斜坡）",
             band, target, EQ_RAMP_MS);
    }
    if (!s_eq_primed)
        s_eq_primed = 1;
}

/* M5Q-03：wet/dry 内部扩展（声明在 src/effect_ext.h，不进 include/ 冻结接口）。
 * 板端使用默认 0.70/1.00；离线工具与自检用它验证“可配”。 */
void effect_echo_set_mix(float wet, float dry)
{
    if (wet < 0.0f) wet = 0.0f;
    if (wet > 1.0f) wet = 1.0f;
    if (dry < 0.0f) dry = 0.0f;
    if (dry > 1.0f) dry = 1.0f;
    s_echo_wet = wet;
    s_echo_dry = dry;
    LOGD(TAG, "回声 wet/dry = %.2f / %.2f", wet, dry);
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
    int i, ch, band;
    int eq_on = (s_cfg.eq_low || s_cfg.eq_mid || s_cfg.eq_high) ? 1 : 0;
    float fb  = (float)s_cfg.echo_fb / 100.0f;

    if (!s_inited || !buf || frames <= 0) return;

    /* EQ 关闭：把仍在斜坡的增益直接落到目标并重算系数（旁通期间无需斜坡），
     * 同时清滤波器状态，避免下次开启时系数/状态与目标不一致产生瞬态。 */
    if (!eq_on) {
        for (band = 0; band < 3; band++)
            for (ch = 0; ch < AUDIO_CHANNELS; ch++) {
                s_eq[band][ch].z1 = 0.0f;
                s_eq[band][ch].z2 = 0.0f;
            }
        for (band = 0; band < 3; band++) {
            if (s_eq_cur_gain[band] != s_eq_gain[band] || s_eq_step[band] != 0.0f) {
                s_eq_cur_gain[band] = s_eq_gain[band];
                s_eq_step[band]     = 0.0f;
                eq_recalc(band);   /* z1/z2 已清，重算后保持零态 */
            }
        }
        if (!s_cfg.echo_on) {
            s_echo_was_on = 0;                /* 全关：旁通快路径 */
            return;
        }
    }

    /* 1) 回声（M5Q-03：预分配延迟线 + 反馈一阶低通 + 改延迟交叉淡化） */
    if (s_cfg.echo_on && s_echo_buf && s_echo_cap > 0) {
        const int cap = s_echo_cap;
        const float damp_a = s_echo_damp_a;
        const float wet = s_echo_wet;
        const float dryg = s_echo_dry;

        /* 从关到开：延迟线里是上一轮开关前的旧音频，清掉，避免“旧声音回放”。
         * 这是**开关**路径，不是改延迟路径；改延迟由 echo_set_delay() 负责，不清线。 */
        if (!s_echo_was_on) {
            echo_clear();
            s_echo_was_on = 1;
        }

        for (i = 0; i < frames; i++) {
            int base = i * s_channels;
            int w = s_echo_w;
            int idx_new = w - s_echo_delay_f;
            int idx_old = w - s_echo_prev_f;

            if (idx_new < 0) idx_new += cap;
            if (idx_old < 0) idx_old += cap;
            idx_new *= s_channels;
            idx_old *= s_channels;

            for (ch = 0; ch < s_channels; ch++) {
                float dry = (float)buf[base + ch];
                float dly = (float)s_echo_buf[idx_new + ch];
                float z, out, fbval;

                /* 改延迟时对旧/新抽头做 20ms 交叉淡化，避免相位跳变爆音 */
                if (s_echo_xf_len > 0) {
                    float t   = (float)s_echo_xf_pos / (float)s_echo_xf_len;
                    float old = (float)s_echo_buf[idx_old + ch];
                    dly = old + (dly - old) * t;
                }

                /* 反馈阻尼一阶低通：每次重复都更暗，去掉“金属/硬”拖尾 */
                z = s_echo_damp_z[ch] + damp_a * (dly - s_echo_damp_z[ch]);
                s_echo_damp_z[ch] = z;

                out = dryg * dry + wet * dly;
                buf[base + ch] = (int16_t)soft_limit(out);

                /* 反馈 = 干声 + fb × 阻尼后的回声；软限幅防 int16 溢出 */
                fbval = dry + fb * z;
                s_echo_buf[w * s_channels + ch] = (int16_t)soft_limit(fbval);
            }
            if (++s_echo_w >= cap) s_echo_w = 0;
            if (s_echo_xf_len > 0 && ++s_echo_xf_pos >= s_echo_xf_len) {
                s_echo_xf_len = 0;
                s_echo_prev_f = s_echo_delay_f;
            }
        }
    } else {
        s_echo_was_on = 0;
    }

    /* 2) EQ：三段串联，逐声道独立状态 */
    if (eq_on) {
        for (i = 0; i < frames; i++) {
            int base = i * s_channels;
            /* M5Q-02：增益斜坡，每采样推进；到位即重算系数（保留 z1/z2） */
            for (band = 0; band < 3; band++) {
                if (s_eq_step[band] == 0.0f) continue;
                {
                    float next = s_eq_cur_gain[band] + s_eq_step[band];
                    float tgt  = s_eq_gain[band];
                    if ((s_eq_step[band] > 0.0f && next >= tgt) ||
                        (s_eq_step[band] < 0.0f && next <= tgt)) {
                        s_eq_cur_gain[band] = tgt;
                        s_eq_step[band]     = 0.0f;
                    } else {
                        s_eq_cur_gain[band] = next;
                    }
                }
                eq_recalc(band);
            }
            for (ch = 0; ch < s_channels; ch++) {
                float x = (float)buf[base + ch];
                x = bq_process(&s_eq[0][ch], x);
                x = bq_process(&s_eq[1][ch], x);
                x = bq_process(&s_eq[2][ch], x);
                buf[base + ch] = (int16_t)soft_limit(x);
            }
        }
    }
}
