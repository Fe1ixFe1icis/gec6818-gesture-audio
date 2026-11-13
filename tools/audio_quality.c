/*
 * audio_quality.c —— 离线音质测试工具（M5Q-07）
 *
 * 目标：不依赖板子，一键量化「播放链路」的客观音质指标，作为 M5Q-01/02/04
 *       改代码前 / 改代码后的"尺子"。同一份报告，改前改后可逐项对比。
 *
 * 覆盖（对应 taskBoard.md §3.4）：
 *   - 重采样器（链接真实 src/resample.c 的抗混叠窗口 sinc，M5Q-01 共用）：
 *     频响、混叠分量、抗混叠抑制
 *   - 效果链（链接真实 src/effect.c）：THD+N、削波、EQ 切换爆音、回声衰减/
 *     反馈阻尼/改延迟爆音/90% 反馈稳定性/wet-dry（M5Q-02/03/04）
 *   - 信号：正弦（单频/扫频）、脉冲、粉噪
 *
 * 重采样器直接链接 src/resample.c：与板端 player.c 共用同一实现，保证
 * "改前/改后可比"——本工具测的就是板端真正跑的那份抗混叠重采样。
 *
 * 编译（开发机，MinGW 或 Linux 主机）：
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter \
 *       -Iinclude -Isrc -DBOARD=0 -D_GNU_SOURCE -pthread \
 *       tools/audio_quality.c src/effect.c src/resample.c src/log.c \
 *       -o build/audio_quality -lm
 *   或：make audio_quality
 *
 * 运行：
 *   ./build/audio_quality [--outdir build/aq_report]
 *   退出码 = 失败项数量（0 = 全部通过）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "effect.h"     /* 真实效果链（链接 src/effect.c） */
#include "effect_ext.h" /* M5Q-03 内部扩展：wet/dry（非冻结接口） */
#include "resample.h"   /* 真实抗混叠重采样（链接 src/resample.c，M5Q-01 共用） */
#include "config.h"     /* AUDIO_RATE_HZ 等 */
#include "common.h"     /* log 宏（链接 src/log.c，本工具设成 ERROR 静默） */

#ifdef _WIN32
#include <direct.h>
#define MKDIR_P(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR_P(p) mkdir(p, 0755)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RATE        44100      /* 播放器输出采样率 */
#define CH          2          /* 立体声 */
#define RATE_SRC    48000      /* demo 素材采样率，用于 2.0x 混叠用例 */
#define FFT_N       32768      /* FFT 点数（2 的幂） */

/* ---------------- 验收阈值（与 taskBoard §3.4 对齐） ---------------- */
#define LIM_ALIAS_DBC      -40.0   /* 混叠/杂散分量上限 dBc（M5Q-01） */
#define LIM_SUPPRESS_DBFS  -40.0   /* 上奈奎斯特分量抑制 dBFS（M5Q-01） */
#define LIM_ROLLOFF_20K    -6.0    /* 20k 频响衰减上限 dB（M5Q-01，粗糙度） */
#define LIM_THD_PCT        1.0     /* THD+N 上限 %（M5Q-04） */
#define LIM_CLIP_SAMPLES   0       /* 硬削波样本数上限（M5Q-04） */
#define LIM_ECHO_MIN       3       /* 最少可闻回声次数（M5Q-03） */
#define LIM_CLICK_DBFS     -60.0   /* EQ 切换 click 峰值上限 dBFS（M5Q-02） */
#define LIM_ECHO_DAMP_DB   -3.0    /* 8k 每遍额外衰减下限 dB（M5Q-03 反馈阻尼） */
#define LIM_ECHO_SWITCH_DBFS -15.0 /* 改延迟时一阶差分峰值上限 dBFS（M5Q-03） */
#define LIM_HPF_DB         -12.0   /* 10Hz 处衰减下限 dB（M5Q-05） */
#define LIM_DC_LSB         1       /* DC 阻断后残留上限（LSB，M5Q-05） */
#define LIM_PRESET_RATIO   1.5     /* 预设切换差分峰值 / 稳态基线上限（M5Q-06） */
#define LIM_PITCH_CENTS    20.0    /* 变调基频允许偏差上限（音分，M5Q-10） */

static int g_fails = 0;

static void report(const char *name, int pass, const char *metric,
                   const char *limit, const char *detail)
{
    printf("  [%s] %-22s %-26s (limit %s)  %s\n",
           pass ? "PASS" : "FAIL", name, metric, limit, detail);
    if (!pass) g_fails++;
}

static int16_t to_i16(double v)
{
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    return (int16_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

/* ================= WAV 写盘（16bit PCM，仅报告/检查用） ================= */
typedef struct {
    FILE *fp;
    uint32_t data_bytes;
} wavw_t;

static void put16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}
static void put32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static int wav_open(wavw_t *w, const char *path, int rate, int ch)
{
    w->fp = fopen(path, "wb");
    if (!w->fp) return -1;
    fwrite("RIFF", 1, 4, w->fp); put32(w->fp, 0);
    fwrite("WAVE", 1, 4, w->fp);
    fwrite("fmt ", 1, 4, w->fp); put32(w->fp, 16);
    put16(w->fp, 1); put16(w->fp, (uint16_t)ch);
    put32(w->fp, (uint32_t)rate);
    put32(w->fp, (uint32_t)(rate * ch * 2));
    put16(w->fp, (uint16_t)(ch * 2)); put16(w->fp, 16);
    fwrite("data", 1, 4, w->fp); put32(w->fp, 0);
    w->data_bytes = 0;
    return 0;
}
static void wav_write(wavw_t *w, const int16_t *buf, long frames, int ch)
{
    size_t bytes = (size_t)frames * (size_t)ch * 2;
    fwrite(buf, 1, bytes, w->fp);
    w->data_bytes += (uint32_t)bytes;
}
static void wav_close(wavw_t *w)
{
    uint32_t riff = 36u + w->data_bytes;
    fseek(w->fp, 4, SEEK_SET); put32(w->fp, riff);
    fseek(w->fp, 40, SEEK_SET); put32(w->fp, w->data_bytes);
    fclose(w->fp);
}

/* ================= 信号生成（输出立体声 int16） ================= */
static void gen_sine(int16_t *buf, long frames, double freq, double rate,
                     double amp)
{
    long i;
    for (i = 0; i < frames; i++) {
        int16_t s = to_i16(amp * sin(2.0 * M_PI * freq * (double)i / rate));
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
    }
}

static void gen_impulse(int16_t *buf, long frames, double amp)
{
    memset(buf, 0, (size_t)frames * CH * sizeof(int16_t));
    buf[0] = buf[1] = to_i16(amp);
}

static void gen_chirp(int16_t *buf, long frames, double f0, double f1,
                      double rate, double amp)
{
    long i;
    double phase = 0.0;
    double k = (f1 - f0) / (double)frames;   /* 每帧频率增量（线性扫频） */
    for (i = 0; i < frames; i++) {
        double f = f0 + k * (double)i;
        phase += 2.0 * M_PI * f / rate;
        int16_t s = to_i16(amp * sin(phase));
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
    }
}

/* ================= 重采样器 =================
 * 直接链接 src/resample.c 的 resampler_block()，与板端 player.c 共用同一实现，
 * 保证本工具测的就是真正跑的那份抗混叠重采样（M5Q-01）。 */

/* ================= 频谱工具 ================= */
static double goertzel_mag(const double *x, long n, double freq, double rate)
{
    double w = 2.0 * M_PI * freq / rate;
    double cw = 2.0 * cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    long i;

    for (i = 0; i < n; i++) {
        s0 = x[i] + cw * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    {
        double re = s1 - s2 * cos(w);
        double im = s2 * sin(w);
        return sqrt(re * re + im * im);
    }
}

/* 就地 radix-2 FFT（re/im 各 n 点，n 为 2 的幂） */
static void fft_radix2(double *re, double *im, int n, int inv)
{
    int i, j, len;

    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / (double)len * (inv ? -1.0 : 1.0);
        double wr = cos(ang), wi = sin(ang);
        for (i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            int half = len / 2;
            for (j = 0; j < half; j++) {
                int a = i + j, b = i + j + half;
                double vr = re[b] * cr - im[b] * ci;
                double vi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - vr; im[b] = im[a] - vi;
                re[a] = re[a] + vr; im[a] = im[a] + vi;
                {
                    double ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = ncr;
                }
            }
        }
    }
    if (inv) for (i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

/* 左声道抽成 double + Hann 窗，返回实际样本数（n <= cap） */
static long window_left(const int16_t *buf, long frames, double *out, int cap)
{
    long i, n = frames < cap ? frames : cap;
    for (i = 0; i < n; i++) {
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
        out[i] = w * ((double)buf[i * CH] / 32768.0);
    }
    return n;
}

/* 找峰值 bin（忽略 DC），返回 bin 下标 */
static int peak_bin(const double *re, const double *im, int n)
{
    int half = n / 2, i, best = 1;
    double best_v = 0.0;
    for (i = 1; i < half; i++) {
        double m = re[i] * re[i] + im[i] * im[i];
        if (m > best_v) { best_v = m; best = i; }
    }
    return best;
}

/* 判断 bin 是否在「基频 × m」附近（±2 bin），用于排除基频与谐波 */
static int near_harmonic(int bin, int fund, int m)
{
    int target = fund * m;
    return bin >= target - 2 && bin <= target + 2;
}

/* 最强杂散分量（dBc）：除基频与 2..10 次谐波外的最强 bin */
static double spurious_dbc(const double *re, const double *im, int n, int fund)
{
    int half = n / 2, i, m;
    double fund_p = re[fund] * re[fund] + im[fund] * im[fund];
    double spur = 0.0;

    if (fund_p <= 0.0) return 0.0;
    for (i = 1; i < half; i++) {
        int skip = 0;
        for (m = 1; m <= 10; m++)
            if (near_harmonic(i, fund, m)) { skip = 1; break; }
        if (skip) continue;
        {
            double p = re[i] * re[i] + im[i] * im[i];
            if (p > spur) spur = p;
        }
    }
    return 10.0 * log10(spur / fund_p);
}

/* THD+N（%）：基频功率附近 ±2 bin，全带噪声（含谐波与泄漏） */
static double thd_plus_n_pct(const double *re, const double *im, int n, int fund)
{
    int half = n / 2, i;
    double fund_p = 0.0, total_p = 0.0;

    for (i = 1; i < half; i++) {
        double p = re[i] * re[i] + im[i] * im[i];
        total_p += p;
        if (near_harmonic(i, fund, 1)) fund_p += p;
    }

    if (fund_p <= 0.0) return 0.0;
    return sqrt((total_p - fund_p) / fund_p) * 100.0;
}

/* ================= 效果链辅助 ================= */
static void effect_eq(int low, int mid, int high)
{
    effect_cfg_t c;
    memset(&c, 0, sizeof c);
    c.echo_delay_ms = DEF_ECHO_DELAY_MS;
    c.eq_low = low; c.eq_mid = mid; c.eq_high = high;
    effect_set_cfg(&c);
}

static void effect_echo(int delay_ms, int fb)
{
    effect_cfg_t c;
    memset(&c, 0, sizeof c);
    c.echo_on = 1;
    c.echo_delay_ms = delay_ms;
    c.echo_fb = fb;
    effect_set_cfg(&c);
}

/* 左声道 [lo, hi] 帧内的峰值（越界自动收紧） */
static int peak_in(const int16_t *buf, long lo, long hi)
{
    long i;
    int mx = 0;

    if (lo < 0) lo = 0;
    for (i = lo; i <= hi; i++) {
        int a = buf[i * CH];
        if (a < 0) a = -a;
        if (a > mx) mx = a;
    }
    return mx;
}

/* 左声道 [lo, hi) 的 RMS（归一化到 ±1.0） */
static double rms_left(const int16_t *buf, long lo, long hi)
{
    long i, n = 0;
    double s = 0.0;

    if (lo < 0) lo = 0;
    for (i = lo; i < hi; i++) {
        double v = (double)buf[i * CH] / 32768.0;
        s += v * v;
        n++;
    }
    return n ? sqrt(s / (double)n) : 0.0;
}

/* 生成 2s 正弦 → 设置 EQ 预设/增益 → 处理后返回稳态增益（dB，相对输入）。
 * 跳过前 0.5s，避开预设交叉淡化与滤波器建立过程。 */
static double eq_gain_db(double freq, int low, int mid, int high, int preset)
{
    const long frames = RATE * 2;
    const long skip = RATE / 2;
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    double rms_in, rms_out, db;

    gen_sine(buf, frames, freq, RATE, 8000.0);
    effect_eq_set_preset(preset);                /* 先选频点（内部接口） */
    effect_reset();                              /* 清状态，避免上一次预设残留 */
    effect_eq(low, mid, high);
    effect_process(buf, (int)frames);

    rms_in  = 8000.0 / 32768.0 / sqrt(2.0);
    rms_out = rms_left(buf, skip, frames);
    db = (rms_out > 0.0) ? 20.0 * log10(rms_out / rms_in) : -999.0;
    free(buf);
    return db;
}

/* ================= 测试用例 ================= */

/* 频响：48k 源 @1.0x → 44.1k，测各频率通过量（量化"发糊/发刺"滚降） */
static void test_freq_response(const char *outdir)
{
    static const double freqs[] = { 1000, 2000, 4000, 6000, 8000,
                                    10000, 12000, 15000, 18000, 20000 };
    long src_frames = RATE_SRC;                 /* 1 秒源 */
    double step = (double)RATE_SRC / (double)RATE;   /* ≈1.088 */
    int16_t *src = malloc((size_t)src_frames * CH * sizeof(int16_t));
    int16_t *dst = malloc((size_t)(src_frames + 8) * CH * sizeof(int16_t));
    double *x = malloc((size_t)(src_frames + 8) * sizeof(double));
    char path[512];
    wavw_t w;
    size_t i;
    double roll20 = 0.0;

    printf("\n== 频响（48k -> 44.1k, 1.0x，窗口 sinc 抗混叠）==\n");
    for (i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
        double f = freqs[i];
        double amp = 16384.0;                    /* -6dBFS */
        long n, m;
        double g, resp_db;

        gen_sine(src, src_frames, f, RATE_SRC, amp);
        m = resampler_block(src, src_frames, CH, dst, src_frames, step);
        n = window_left(dst, m, x, src_frames);
        g = goertzel_mag(x, n, f, RATE);         /* 输出频率 = 源频率（1.0x） */
        {
            /* Hann 窗相干增益 0.5：|X| = A*N/4 → A = 4*|X|/N（归一化幅值） */
            double a_in = amp / 32768.0;
            double a_out = 4.0 * g / (double)n;
            resp_db = 20.0 * log10(a_out / a_in);
        }
        printf("    %5.0f Hz : %+6.2f dB\n", f, resp_db);
        if (f == 20000.0) roll20 = resp_db;
    }

    /* 写出一个扫频 WAV 供人工/频谱图核对 */
    if (snprintf(path, sizeof path, "%s%caq_sweep.wav", outdir,
                 (outdir[0] ? '/' : '\0')) > 0) {
        gen_chirp(src, src_frames, 1000.0, 20000.0, RATE_SRC, 12000.0);
        if (wav_open(&w, path, RATE_SRC, CH) == 0) {
            wav_write(&w, src, src_frames, CH);
            wav_close(&w);
        }
    }

    report("freq_resp_20k", roll20 > LIM_ROLLOFF_20K, "20k 响应",
           ">-6 dB", "");
    free(src); free(dst); free(x);
}

/* 混叠分量：48k 源 @2.0x，1k/8k 正弦，最强杂散应 < -40dBc */
static void test_alias(const char *outdir, double f_src, const char *name)
{
    long src_frames = RATE_SRC * 2;              /* 2 秒源 */
    double step = (double)RATE_SRC * 2.0 / (double)RATE;   /* 2.0x ≈2.177 */
    int16_t *src = malloc((size_t)src_frames * CH * sizeof(int16_t));
    int16_t *dst = malloc((size_t)(src_frames + 8) * CH * sizeof(int16_t));
    double *re = malloc((size_t)FFT_N * sizeof(double));
    double *im = malloc((size_t)FFT_N * sizeof(double));
    double *x = malloc((size_t)FFT_N * sizeof(double));
    char path[512];
    wavw_t w;
    long m, n;
    int fund, i;
    double sdb;
    double f_out;

    (void)outdir;

    /* 让输出基频落在 FFT 整数 bin 上：否则 Hann 窗泄漏会把基频邻近 ±3 bin
     * 抬到 ~-33dBc，污染「最强杂散」口径，与真实混叠/镜像分量无法区分。 */
    {
        double k = floor(f_src * 2.0 * (double)FFT_N / (double)RATE + 0.5);
        f_src = k * (double)RATE / (2.0 * (double)FFT_N);
    }
    f_out = f_src * 2.0;                          /* 输出基频（对齐后） */

    gen_sine(src, src_frames, f_src, RATE_SRC, 16384.0);
    m = resampler_block(src, src_frames, CH, dst, src_frames, step);
    n = window_left(dst, m, x, FFT_N);

    memset(im, 0, (size_t)FFT_N * sizeof(double));
    for (i = 0; i < n; i++) re[i] = x[i];
    for (; i < FFT_N; i++) re[i] = 0.0;
    fft_radix2(re, im, FFT_N, 0);

    fund = peak_bin(re, im, FFT_N);
    sdb = spurious_dbc(re, im, FFT_N, fund);

    if (snprintf(path, sizeof path, "%s%caq_%s.wav", outdir,
                 (outdir[0] ? '/' : '\0'), name) > 0) {
        if (wav_open(&w, path, RATE, CH) == 0) {
            wav_write(&w, dst, m, CH);
            wav_close(&w);
        }
    }
    {
        char detail[64];
        snprintf(detail, sizeof detail, "fsrc=%.1f fout=%.1fHz 杂散 %+.1f dBc",
                 f_src, f_out, sdb);
        report(name, sdb < LIM_ALIAS_DBC, "杂散 dBc", "<-40 dB", detail);
    }
    free(src); free(dst); free(re); free(im); free(x);
}

/* 抗混叠抑制：上奈奎斯特分量（15k@48k 2.0x → 别名 14.1k）应被压到 < -40dBFS */
static void test_suppress(const char *outdir)
{
    long src_frames = RATE_SRC;
    double step = (double)RATE_SRC * 2.0 / (double)RATE;
    double f_src = 15000.0;
    double f_alias = (double)RATE - 2.0 * f_src; /* 30k 折回 → 14.1k */
    int16_t *src = malloc((size_t)src_frames * CH * sizeof(int16_t));
    int16_t *dst = malloc((size_t)(src_frames + 8) * CH * sizeof(int16_t));
    double *x = malloc((size_t)(src_frames + 8) * sizeof(double));
    long m, n;
    double g, amp_norm, dbfs;
    char detail[96];

    (void)outdir;
    gen_sine(src, src_frames, f_src, RATE_SRC, 16384.0);
    m = resampler_block(src, src_frames, CH, dst, src_frames, step);
    n = window_left(dst, m, x, (int)(src_frames + 8));
    g = goertzel_mag(x, n, f_alias, RATE);
    amp_norm = 4.0 * g / (double)n;              /* 归一化幅值（Hann 相干增益 0.5） */
    dbfs = 20.0 * log10(amp_norm);

    snprintf(detail, sizeof detail, "别名 %.0fHz 残留 %.1f dBFS", f_alias, dbfs);
    report("alias_suppress_15k", dbfs < LIM_SUPPRESS_DBFS,
           "上奈奎斯特抑制", "<-40 dBFS", detail);
    free(src); free(dst); free(x);
}

/* THD+N：1kHz @ -12dBFS，EQ 中频 +6dB（只测链路线性度，不触发削波） */
static void test_thd(const char *outdir)
{
    long frames = FFT_N;
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    double *re = malloc((size_t)FFT_N * sizeof(double));
    double *im = malloc((size_t)FFT_N * sizeof(double));
    double *x = malloc((size_t)FFT_N * sizeof(double));
    int i, fund;
    double f, thd;
    char detail[64];

    (void)outdir;
    /* 用 FFT 整 bin 频率（≈999.95Hz）落在 bin 中心，避免频谱泄漏抬高 THD+N 本底 */
    f = 743.0 * RATE / (double)FFT_N;
    gen_sine(buf, frames, f, RATE, 4096.0);        /* -12dBFS */
    effect_reset();
    effect_eq(0, 6, 0);
    effect_process(buf, (int)frames);

    {
        long n = window_left(buf, frames, x, FFT_N);
        memset(im, 0, (size_t)FFT_N * sizeof(double));
        for (i = 0; i < n; i++) re[i] = x[i];
        for (; i < FFT_N; i++) re[i] = 0.0;
        fft_radix2(re, im, FFT_N, 0);
        fund = peak_bin(re, im, FFT_N);
        thd = thd_plus_n_pct(re, im, FFT_N, fund);
    }
    snprintf(detail, sizeof detail, "THD+N %.4f %%", thd);
    report("thd_1k", thd < LIM_THD_PCT, "THD+N", "<1 %", detail);
    free(buf); free(re); free(im); free(x);
}

/* M5Q-02：EQ 增益切换无 click。DC 输入下把低架从 0 切到 +12dB，测输出一阶
 * 差分峰值。瞬时切系数+清状态会在一拍内把 DC 从 4000 顶到 ~15920（差分
 * ~11920），平滑斜坡则差分峰值仅 ~17（<-60dBFS），两者量级差数百倍。 */
static void test_eq_click(const char *outdir)
{
    long frames = RATE;                          /* 1 秒 */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long i;
    int max_step = 0;
    char detail[96];

    (void)outdir;
    for (i = 0; i < frames * CH; i++) buf[i] = 4000;   /* 恒定 DC */

    effect_reset();
    effect_eq(0, 0, 0);                          /* 前半：EQ 关（旁通） */
    effect_process(buf, (int)(frames / 2));

    effect_eq(12, 0, 0);                         /* 后半：低架 +12dB 切换 */
    effect_process(buf + (long)(frames / 2) * CH, (int)(frames - frames / 2));

    for (i = 1; i < frames * CH; i++) {
        int d = buf[i] - buf[i - 1];
        if (d < 0) d = -d;
        if (d > max_step) max_step = d;
    }
    {
        double dbfs = (max_step > 0)
                      ? 20.0 * log10((double)max_step / 32768.0)
                      : -1000.0;
        snprintf(detail, sizeof detail, "切换差分峰值 %d（%.1f dBFS）",
                 max_step, dbfs);
        report("eq_click_switch", dbfs < LIM_CLICK_DBFS, "切换无爆音",
               "<-60 dBFS", detail);
    }
    free(buf);
}

/* 削波：-6dBFS 1kHz 正弦 + 三段 EQ +12dB，硬削波（钉在满幅）样本应 = 0。
 * 选 -6dBFS 输入：信号本身不触满幅，只有 EQ 的 +12dB 把它顶到削波。 */
static void test_clip(const char *outdir)
{
    long frames = RATE;                          /* 1 秒 */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long i, clips = 0;
    char detail[64];

    (void)outdir;
    gen_sine(buf, frames, 1000.0, RATE, 16384.0);
    effect_reset();
    effect_eq(12, 12, 12);
    effect_process(buf, (int)frames);

    for (i = 0; i < frames * CH; i++)
        if (buf[i] >= 32767 || buf[i] <= -32768) clips++;

    snprintf(detail, sizeof detail, "硬削波 %ld/%ld 样本", clips, frames * CH);
    report("clip_fullscale", clips <= LIM_CLIP_SAMPLES, "硬削波样本",
           "=0", detail);
    free(buf);
}

/* 回声衰减：脉冲 → 回声 on(250ms/40%)，数回声次数与衰减 */
static void test_echo_decay(const char *outdir)
{
    long frames = RATE * 2;                      /* 2 秒，够 4+ 次回声 */
    int delay_samples = DEF_ECHO_DELAY_MS * RATE / 1000;
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    int peak[16];
    int n = 0, i, win = 64;
    double ratio_db = 0.0;
    char detail[96];

    (void)outdir;
    gen_impulse(buf, frames, 32767.0);
    effect_reset();
    effect_echo(DEF_ECHO_DELAY_MS, DEF_ECHO_FB);
    effect_process(buf, (int)frames);

    /* 在每个延迟整数倍附近找峰值；peak[0] 是原始脉冲，peak[1..] 才是回声 */
    for (i = 0; i < 16 && (long)i * delay_samples + win < frames; i++) {
        long center = (long)i * delay_samples;
        long lo = center - win < 0 ? 0 : center - win;
        long hi = center + win;
        long k;
        int mx = 0;
        for (k = lo; k <= hi; k++) {
            int a = buf[k * CH] < 0 ? -buf[k * CH] : buf[k * CH];
            if (a > mx) mx = a;
        }
        if (mx < 33) break;                      /* <-60dBFS 视为不可闻 */
        peak[n++] = mx;
    }
    if (n >= 3 && peak[1] > 0) {
        double r = (double)peak[2] / (double)peak[1];
        if (r > 0.0) ratio_db = 20.0 * log10(r);
    }
    snprintf(detail, sizeof detail, "回声 %d 次，衰减 %.1f dB/次",
             n - 1, ratio_db);
    report("echo_decay", n - 1 >= LIM_ECHO_MIN, "回声次数", ">=3", detail);
    free(buf);
}

/* M5Q-03 阻尼：100ms 延迟、fb=60，比较 1k 与 8k 短音的第 1→2 次回声峰值比。
 * 4kHz 一阶低通对 8k 每遍约 -9.5dB，对 1k 几乎无损；若反馈无阻尼，两者
 * 比值应基本相同（damp≈0dB）。 */
static void test_echo_damping(const char *outdir)
{
    const int delay_ms = 100;
    const long frames = RATE;                    /* 1 秒，够 3 次回声 */
    const double freqs[2] = { 1000.0, 8000.0 };
    double ratio[2] = { 0.0, 0.0 };
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long burst = RATE * 30 / 1000;               /* 30ms 短音，不碰下一次回声 */
    int k;
    char detail[128];

    (void)outdir;
    for (k = 0; k < 2; k++) {
        long i, d = (long)delay_ms * RATE / 1000;
        long span = RATE * 40 / 1000;
        int p1, p2;

        memset(buf, 0, (size_t)frames * CH * sizeof(int16_t));
        for (i = 0; i < burst; i++) {
            double env = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(burst - 1));
            int16_t s = to_i16(12000.0 * env
                               * sin(2.0 * M_PI * freqs[k] * (double)i / RATE));
            buf[i * CH] = buf[i * CH + 1] = s;
        }
        effect_reset();
        effect_echo(delay_ms, 60);
        effect_process(buf, (int)frames);

        p1 = peak_in(buf, d - 64, d + span);
        p2 = peak_in(buf, 2 * d - 64, 2 * d + span);
        ratio[k] = (p1 > 0) ? (double)p2 / (double)p1 : 0.0;
    }
    {
        double damp_db = (ratio[0] > 0.0 && ratio[1] > 0.0)
                         ? 20.0 * log10(ratio[1] / ratio[0]) : 0.0;
        snprintf(detail, sizeof detail,
                 "1k 每遍 ×%.2f，8k 每遍 ×%.2f，阻尼 %+.1f dB/遍",
                 ratio[0], ratio[1], damp_db);
        report("echo_damping", ratio[0] > 0.45 && damp_db < LIM_ECHO_DAMP_DB,
               "8k 反馈阻尼", "<-3 dB/遍", detail);
    }
    free(buf);
}

/* M5Q-03 改延迟无爆音：1s 连续 1234Hz（非整数周期/ms，改延迟必产生相位跳变）
 * → 200ms 切到 275ms。测切换后 300ms 内的一阶差分峰值；硬跳变会产生
 * 近满幅台阶（>-12dBFS），20ms 交叉淡化应保持在自然波形斜率量级。 */
static void test_echo_delay_switch(const char *outdir)
{
    const long frames = RATE * 2;
    const long half = frames / 2;
    const double f = 1234.0;
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long i;
    int base = 0, sw = 0;
    char detail[128];

    (void)outdir;
    gen_sine(buf, frames, f, RATE, 8000.0);

    effect_reset();
    effect_echo(200, 40);
    effect_process(buf, (int)half);

    effect_echo(275, 40);                        /* 75ms 延迟跳变 → 相位 0.55 周 */
    effect_process(buf + half * CH, (int)(frames - half));

    for (i = half - RATE * 3 / 10; i < half; i++) {
        int d = buf[i * CH] - buf[(i - 1) * CH];
        if (d < 0) d = -d;
        if (d > base) base = d;
    }
    for (i = half; i < half + RATE * 3 / 10; i++) {
        int d = buf[i * CH] - buf[(i - 1) * CH];
        if (d < 0) d = -d;
        if (d > sw) sw = d;
    }
    {
        double dbfs = (sw > 0) ? 20.0 * log10((double)sw / 32768.0) : -1000.0;
        snprintf(detail, sizeof detail,
                 "切换差分 %d（%.1f dBFS），稳态基线 %d", sw, dbfs, base);
        report("echo_delay_switch", dbfs < LIM_ECHO_SWITCH_DBFS,
               "改延迟无爆音", "<-15 dBFS", detail);
    }
    free(buf);
}

/* M5Q-03 90% 反馈不自激：脉冲 → 5 秒、fb=90、250ms。有阻尼时环路增益
 * <1（4k 低通对高频额外衰减），末段应明显低于第 2 次回声且无硬削波。 */
static void test_echo_self_osc(const char *outdir)
{
    const long frames = RATE * 5;
    long d = 250L * RATE / 1000;
    int16_t *buf = calloc((size_t)frames * CH, sizeof(int16_t));
    int e2, last, clips = 0;
    long i;
    char detail[128];

    (void)outdir;
    buf[0] = buf[1] = 30000;
    effect_reset();
    effect_echo(250, 90);
    effect_process(buf, (int)frames);

    e2   = peak_in(buf, 2 * d - 64, 2 * d + 64);
    last = peak_in(buf, frames - RATE / 10, frames - 1);
    for (i = 0; i < frames * CH; i++)
        if (buf[i] >= 32767 || buf[i] <= -32768) clips++;

    snprintf(detail, sizeof detail,
             "第2次回声 %d，末 100ms 峰值 %d，硬削波 %d", e2, last, clips);
    report("echo_self_osc", e2 > 33 && last * 2 < e2 && clips == 0,
           "90% 反馈不自激", "衰减且无硬削", detail);
    free(buf);
}

/* M5Q-03 wet/dry 可配：wet=0 时回声位置必须为静音，恢复默认 0.70 后
 * 同一位置应有明显回声。验证内部 setter 真的生效（不改冻结结构体）。 */
static void test_echo_mix(const char *outdir)
{
    const long frames = RATE / 2;
    long d = 100L * RATE / 1000;
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    int echo_off, echo_on;
    char detail[96];

    (void)outdir;
    memset(buf, 0, (size_t)frames * CH * sizeof(int16_t));
    buf[0] = buf[1] = 20000;
    effect_reset();
    effect_echo(100, 30);
    effect_echo_set_mix(0.0f, 1.0f);             /* 全干 */
    effect_process(buf, (int)frames);
    echo_off = peak_in(buf, d - 32, d + 32);

    memset(buf, 0, (size_t)frames * CH * sizeof(int16_t));
    buf[0] = buf[1] = 20000;
    effect_reset();
    effect_echo_set_mix(0.70f, 1.00f);           /* 恢复默认 */
    effect_process(buf, (int)frames);
    echo_on = peak_in(buf, d - 32, d + 32);

    snprintf(detail, sizeof detail, "wet=0 回声 %d，wet=0.7 回声 %d",
             echo_off, echo_on);
    report("echo_wet_mix", echo_off < 33 && echo_on > 5000,
           "wet/dry 可配", "0 静音 / 默认有声", detail);
    free(buf);
}

/* M5Q-05：10Hz 亚声衰减。用高架 -12dB 把效果链激活（它在 10Hz 近似 0dB），
 * 避免低架增益污染低频测量。 */
static void test_hpf(const char *outdir)
{
    const long frames = RATE * 3;
    const long skip   = RATE;                    /* 1s 后进入稳态 */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    double db10, db1k;
    char detail[128];

    (void)outdir;
    gen_sine(buf, frames, 10.0, RATE, 8000.0);
    effect_eq_set_preset(EFFECT_EQ_PRESET_STANDARD);
    effect_reset();
    effect_eq(0, 0, -12);                        /* 激活链路；高架在 10Hz≈0dB */
    effect_process(buf, (int)frames);
    db10 = 20.0 * log10(rms_left(buf, skip, frames)
                        / (8000.0 / 32768.0 / sqrt(2.0)));

    gen_sine(buf, frames, 1000.0, RATE, 8000.0);
    effect_reset();
    effect_eq(0, 0, -12);
    effect_process(buf, (int)frames);
    db1k = 20.0 * log10(rms_left(buf, skip, frames)
                        / (8000.0 / 32768.0 / sqrt(2.0)));

    snprintf(detail, sizeof detail, "10Hz %.1f dB，1kHz %+.2f dB", db10, db1k);
    report("hpf_10hz", db10 < LIM_HPF_DB && fabs(db1k) < 0.5,
           "10Hz 衰减", "<-12 dB", detail);
    free(buf);
}

/* M5Q-05：DC 阻断。DC 4000 + 三段 +12dB（最坏情况），稳态残留应 <= 1 LSB。 */
static void test_dc_block(const char *outdir)
{
    const long frames = RATE * 2;
    const long skip   = RATE;                    /* 测最后 1s */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long i;
    int mx = 0;
    char detail[96];

    (void)outdir;
    for (i = 0; i < frames * CH; i++) buf[i] = 4000;
    effect_eq_set_preset(EFFECT_EQ_PRESET_STANDARD);
    effect_reset();
    effect_eq(12, 12, 12);
    effect_process(buf, (int)frames);

    for (i = skip * CH; i < frames * CH; i++) {
        int a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > mx) mx = a;
    }
    snprintf(detail, sizeof detail, "稳态最大残留 %d LSB", mx);
    report("dc_offset", mx <= LIM_DC_LSB, "DC 残留", "<=1 LSB", detail);
    free(buf);
}

/* M5Q-06：三段可听辨且互不重叠。standard 频点 120Hz / 1k Q0.7 / 6kHz，
 * 各段 +12dB 时：本段显著提升，其余两段测量点保持在 ±1.5dB 内。 */
static void test_eq_bands(const char *outdir)
{
    double low60  = eq_gain_db(60.0,   12, 0, 0, EFFECT_EQ_PRESET_STANDARD);
    double low1k  = eq_gain_db(1000.0, 12, 0, 0, EFFECT_EQ_PRESET_STANDARD);
    double mid1k  = eq_gain_db(1000.0, 0, 12, 0, EFFECT_EQ_PRESET_STANDARD);
    double mid60  = eq_gain_db(60.0,   0, 12, 0, EFFECT_EQ_PRESET_STANDARD);
    double mid10k = eq_gain_db(10000.0,0, 12, 0, EFFECT_EQ_PRESET_STANDARD);
    double high10k= eq_gain_db(10000.0,0, 0, 12, EFFECT_EQ_PRESET_STANDARD);
    double high1k = eq_gain_db(1000.0, 0, 0, 12, EFFECT_EQ_PRESET_STANDARD);
    char detail[160];

    (void)outdir;
    snprintf(detail, sizeof detail, "60Hz %+.1f / 1k %+.2f dB", low60, low1k);
    report("eq_low_band", low60 > 7.0 && fabs(low1k) < 1.5,
           "低架 +12dB", "本段>7dB 其余<1.5dB", detail);

    snprintf(detail, sizeof detail, "1k %+.1f / 60Hz %+.2f / 10k %+.2f dB",
             mid1k, mid60, mid10k);
    report("eq_mid_band", mid1k > 9.0 && fabs(mid60) < 1.5 && fabs(mid10k) < 1.5,
           "峰 +12dB", "本段>9dB 其余<1.5dB", detail);

    snprintf(detail, sizeof detail, "10k %+.1f / 1k %+.2f dB", high10k, high1k);
    report("eq_high_band", high10k > 8.0 && fabs(high1k) < 1.5,
           "高架 +12dB", "本段>8dB 其余<1.5dB", detail);
}

/* M5Q-06：内部预设有效且切换无爆音。standard→vocal，1kHz 正弦、三段 +6dB；
 * 交叉淡化期间一阶差分峰值不应超过稳态基线的 1.5 倍。 */
static void test_eq_preset_switch(const char *outdir)
{
    const long frames = RATE * 2;
    const long half   = RATE;
    const long win    = RATE / 10;               /* 100ms */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    long i;
    int base = 0, sw = 0;
    int base2 = 0, sw2 = 0;
    double std80, voc80, std8k, demo8k, voicing;
    int n = effect_eq_preset_count();
    char detail[220];

    (void)outdir;
    std80  = eq_gain_db(80.0, 12, 0, 0, EFFECT_EQ_PRESET_STANDARD);
    voc80  = eq_gain_db(80.0, 12, 0, 0, EFFECT_EQ_PRESET_VOCAL);
    std8k  = eq_gain_db(8000.0, 0, 0, 12, EFFECT_EQ_PRESET_STANDARD);
    demo8k = eq_gain_db(8000.0, 0, 0, 12, EFFECT_EQ_PRESET_DEMO);
    voicing = fabs(std80 - voc80);
    if (fabs(std8k - demo8k) > voicing) voicing = fabs(std8k - demo8k);

    gen_sine(buf, frames, 80.0, RATE, 8000.0);
    effect_eq_set_preset(EFFECT_EQ_PRESET_STANDARD);
    effect_reset();
    effect_eq(12, 0, 0);
    effect_process(buf, (int)half);
    effect_eq_set_preset(EFFECT_EQ_PRESET_VOCAL);
    effect_process(buf + half * CH, (int)(frames - half));

    for (i = half - win; i < half; i++) {
        int d = buf[i * CH] - buf[(i - 1) * CH];
        int d2 = buf[i * CH] - 2 * buf[(i - 1) * CH] + buf[(i - 2) * CH];
        if (d < 0) d = -d;
        if (d2 < 0) d2 = -d2;
        if (d > base) base = d;
        if (d2 > base2) base2 = d2;
    }
    for (i = half; i < half + win; i++) {
        int d = buf[i * CH] - buf[(i - 1) * CH];
        int d2 = buf[i * CH] - 2 * buf[(i - 1) * CH] + buf[(i - 2) * CH];
        if (d < 0) d = -d;
        if (d2 < 0) d2 = -d2;
        if (d > sw) sw = d;
        if (d2 > sw2) sw2 = d2;
    }

    snprintf(detail, sizeof detail,
             "预设 %d 套（%s/%s/%s），80Hz 差异 %.1f dB，8k 差异 %.1f dB，"
             "切换差分 %d/基线 %d，二阶差分 %d/基线 %d",
             n, effect_eq_preset_name(0), effect_eq_preset_name(1),
             effect_eq_preset_name(2), fabs(std80 - voc80),
             fabs(std8k - demo8k), sw, base, sw2, base2);
    report("eq_preset_switch",
           n == 3 && voicing > 1.0 && base > 0 &&
           sw < (int)(base * LIM_PRESET_RATIO) && sw2 < base2 * 4 + 4,
           "预设切换无爆音", "一阶<1.5× 二阶<4×基线", detail);
    free(buf);
}

/* ================= M5Q-10：变调不变速 ================= */

/* 取左声道频谱峰值基频（Hz）。跳过低频区，返回最强 bin 对应频率。 */
static double measure_peak_hz(const int16_t *buf, long frames)
{
    double *re = malloc(FFT_N * sizeof(double));
    double *im = malloc(FFT_N * sizeof(double));
    double *x  = malloc(FFT_N * sizeof(double));
    long n;
    int i, bin;
    double hz;

    memset(re, 0, FFT_N * sizeof(double));
    memset(im, 0, FFT_N * sizeof(double));
    n = window_left(buf, frames, x, FFT_N);
    for (i = 0; i < n; i++) re[i] = x[i];
    fft_radix2(re, im, FFT_N, 0);
    bin = peak_bin(re, im, FFT_N);
    hz = (double)bin * (double)RATE / (double)FFT_N;
    free(re); free(im); free(x);
    return hz;
}

static long count_clip(const int16_t *buf, long frames)
{
    long i, c = 0;
    for (i = 0; i < frames; i++) {
        int a = buf[i * CH];
        if (a >= 32767 || a <= -32768) c++;
    }
    return c;
}

/* 1000Hz 正弦经 --pitch 后，基频应为 1000 × 2^(半音/12)；且无硬削波。
 * 时长由 effect_process 就地保证 1:1（变调不变速）。 */
static void test_pitch_shift(const char *outdir)
{
    static const double cases[] = { 12.0, -12.0, 7.0 };
    const long frames = RATE * 2;
    const long skip   = RATE / 2;      /* 跳过延迟线填充与交叉淡化建立 */
    int16_t *buf = malloc((size_t)frames * CH * sizeof(int16_t));
    char detail[256];
    int i, all_ok = 1;
    long clips = 0;
    double worst = 0.0;

    printf("\n== 变调不变速（--pitch，granular time-domain）==\n");
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        double semi   = cases[i];
        double ratio  = pow(2.0, semi / 12.0);
        double expect = 1000.0 * ratio;
        double meas, cents;

        gen_sine(buf, frames, 1000.0, RATE, 16000.0);
        effect_pitch_set_semitones((float)semi);
        effect_reset();
        effect_process(buf, (int)frames);

        meas  = measure_peak_hz(buf + skip * CH, frames - skip);
        cents = 1200.0 * (log(meas / expect) / log(2.0));
        if (cents < 0.0) cents = -cents;
        if (cents > worst) worst = cents;
        if (cents > LIM_PITCH_CENTS) all_ok = 0;

        clips += count_clip(buf + skip * CH, frames - skip);
        printf("    %+5.1f 半音 : 期望 %6.1f Hz  实测 %6.1f Hz  偏差 %5.1f 音分\n",
               semi, expect, meas, cents);
    }

    snprintf(detail, sizeof detail,
             "3 组移调最大偏差 %.1f 音分，削波样本 %ld",
             worst, clips);
    report("pitch_shift", all_ok && clips == 0,
           "变调不变速", "偏差<20音分 且无削波", detail);
    (void)outdir;
    free(buf);
}

/* ================= main ================= */
int main(int argc, char **argv)
{
    const char *outdir = ".";
    int i;

    log_set_level(LOG_ERROR);                    /* 静默 effect/log 内部日志 */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--outdir") && i + 1 < argc) outdir = argv[++i];
        else {
            fprintf(stderr, "用法: %s [--outdir <dir>]\n", argv[0]);
            return 2;
        }
    }
    MKDIR_P(outdir);                             /* 已存在会失败，忽略 */

    if (effect_init(RATE, CH) != 0) {
        fprintf(stderr, "效果链初始化失败\n");
        return 1;
    }

    printf("========================================================\n");
    printf(" 离线音质报告（M5Q-07） rate=%d ch=%d\n", RATE, CH);
    printf("========================================================\n");

    test_freq_response(outdir);
    test_alias(outdir, 1000.0, "alias_1k_2x");
    test_alias(outdir, 8000.0, "alias_8k_2x");
    test_suppress(outdir);
    test_thd(outdir);
    test_eq_click(outdir);
    test_clip(outdir);
    test_hpf(outdir);
    test_dc_block(outdir);
    test_eq_bands(outdir);
    test_eq_preset_switch(outdir);
    test_echo_decay(outdir);
    test_echo_damping(outdir);
    test_echo_delay_switch(outdir);
    test_echo_self_osc(outdir);
    test_echo_mix(outdir);
    test_pitch_shift(outdir);

    effect_finish();

    printf("--------------------------------------------------------\n");
    printf(" 结果：%s（%d 项失败）\n",
           g_fails == 0 ? "全部通过" : "存在失败", g_fails);
    return g_fails;
}
