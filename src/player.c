/*
 * player.c —— 播放引擎：WAV 读取 → 变速重采样 → 效果链 → 音量 → 输出
 *
 * 关键设计决定（都写在这里，避免后来者重复讨论）：
 *   1. v1 只吃 WAV（PCM 16bit）。MP3 会让解码进关键路径，且在板子上
 *      还需要额外库（boardGuide.md §3.3）。素材要求由测试文档写死。
 *   2. 变速用「线性插值改重采样步长」，音调随动。变速不变调要 WSOLA，
 *      留给 M5 评估，不做进 v1 的必经路径。
 *   3. sink 抽象成两种实现，音频写入是唯一与硬件相关的代码，
 *      BOARD=0 时空 sink 按墙上时钟推进，保证状态机/时序在主机上可验证。
 *   4. 单写者：参数只从 ctrl（params_get）读，绝不回写；状态机请求由
 *      player_request() 投递，播放线程自己落地。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "player.h"
#include "effect.h"
#include "ctrl.h"
#include "common.h"
#include "app.h"

#if defined(BOARD) && BOARD
#include <alsa/asoundlib.h>
#endif

/* 板端（Linux）errno.h 里有 ESTRPIPE；开发机自测环境的头文件可能没有，
 * 兜底成 EPIPE 只为让语法检查/主机编译通过，板端行为不变。 */
#ifndef ESTRPIPE
#define ESTRPIPE EPIPE
#endif

#define TAG "player"

/* 源数据缓冲：4096 帧足够覆盖最大步长（2.0）下一次取数的需求 */
#define SRC_BUF_FRAMES 4096
/* 状态/波形刷新间隔（周期数）：512 帧 × 8 ≈ 93ms，对 5fps 显示足够 */
#define STATUS_EVERY_N_PERIODS 8

typedef struct {
    FILE *fp;
    int   rate;
    int   channels;
    int   bits;
    long  total_frames;
    long  frames_left;
} wav_t;

static wav_t   s_wav;
static int     s_wav_open;

static int16_t s_srcbuf[SRC_BUF_FRAMES * AUDIO_CHANNELS];
static int     s_src_len;          /* 缓冲内有效帧数 */
static double  s_src_pos;          /* 浮点读位置（帧） */
static int     s_src_eof;

static int16_t s_out[AUDIO_PERIOD_FRAMES * AUDIO_CHANNELS];
static int16_t s_scope[SCOPE_POINTS];

static int     s_rate = AUDIO_RATE_HZ;
static int     s_channels = AUDIO_CHANNELS;

/* 状态机 */
static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static play_state_t    s_req = PLAY_STOPPED;
static play_state_t    s_state = PLAY_STOPPED;
static int             s_reopen;        /* 1 = 需要重新打开文件（切歌） */
static int             s_track_index;

/* 计时（空 sink 用） */
static int64_t s_base_ms;
static long long s_out_frames;

#if defined(BOARD) && BOARD
static snd_pcm_t *s_pcm;
#endif

/* ================= WAV 解析 ================= */
static unsigned int rd_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned short rd_u16(const unsigned char *p)
{
    return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

/*
 * 解析 RIFF/WAVE 头并定位到 data 块起点。
 * 只接受 PCM(1) / 16bit / 1~2 声道，其余明确报错——素材问题要在上板前暴露。
 */
static int wav_parse(FILE *fp, wav_t *w)
{
    unsigned char hdr[12];
    unsigned char chunk[8];
    int got_fmt = 0;
    unsigned long pos = 12;

    memset(w, 0, sizeof *w);
    w->fp = fp;

    if (fread(hdr, 1, 12, fp) != 12) return -1;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return -1;

    for (;;) {
        unsigned int size;
        if (fread(chunk, 1, 8, fp) != 8) return -1;
        size = rd_u32(chunk + 4);
        pos += 8;

        if (memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[40];
            unsigned int take = size < sizeof fmt ? size : sizeof fmt;
            unsigned short audio_fmt, channels, bits;
            unsigned int rate;

            if (fread(fmt, 1, take, fp) != take) return -1;
            if (size > take) fseek(fp, (long)(size - take), SEEK_CUR);

            audio_fmt = rd_u16(fmt + 0);
            channels  = rd_u16(fmt + 2);
            rate      = rd_u32(fmt + 4);
            bits      = rd_u16(fmt + 14);

            if (audio_fmt != 1) return -2;         /* 非 PCM */
            if (bits != 16)     return -3;         /* 非 16bit */
            if (channels < 1 || channels > 2) return -4;

            w->rate     = (int)rate;
            w->channels = channels;
            w->bits     = bits;
            got_fmt = 1;
            pos += size;
            if (size & 1) { fseek(fp, 1, SEEK_CUR); pos++; }
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!got_fmt) return -1;
            w->total_frames = (long)(size / (w->channels * 2));
            w->frames_left  = w->total_frames;
            return 0;                              /* fp 现在正好在 PCM 起点 */
        } else {
            long skip = (long)size + (long)(size & 1);
            if (fseek(fp, skip, SEEK_CUR) != 0) return -1;
            pos += (unsigned long)skip;
        }
        if (pos > 64 * 1024 * 1024UL) return -1;   /* 防御：畸形头 */
    }
}

int player_probe_wav(const char *path, wav_info_t *info)
{
    FILE *fp;
    wav_t w;
    int rc;

    if (!path || !info) return -1;
    fp = fopen(path, "rb");
    if (!fp) return -1;
    rc = wav_parse(fp, &w);
    fclose(fp);
    if (rc != 0) return rc;

    info->rate     = w.rate;
    info->channels = w.channels;
    info->bits     = w.bits;
    info->frames   = w.total_frames;
    info->seconds  = w.rate > 0 ? (double)w.total_frames / w.rate : 0.0;
    return 0;
}

/* ================= sink ================= */
#if defined(BOARD) && BOARD
static int alsa_open(void)
{
    snd_pcm_hw_params_t *hw;
    unsigned int rate = (unsigned int)s_rate;
    snd_pcm_uframes_t period = AUDIO_PERIOD_FRAMES;
    snd_pcm_uframes_t buffer = AUDIO_BUFFER_FRAMES;

    if (snd_pcm_open(&s_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        LOGE(TAG, "ALSA 打开失败（设备可能被 Qt/madplay 占用，或 /dev/snd 不存在）");
        s_pcm = NULL;
        return -1;
    }
    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(s_pcm, hw);
    snd_pcm_hw_params_set_access(s_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(s_pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(s_pcm, hw, (unsigned int)s_channels);
    snd_pcm_hw_params_set_rate_near(s_pcm, hw, &rate, 0);
    snd_pcm_hw_params_set_period_size_near(s_pcm, hw, &period, 0);
    snd_pcm_hw_params_set_buffer_size_near(s_pcm, hw, &buffer);

    if (snd_pcm_hw_params(s_pcm, hw) < 0) {
        LOGE(TAG, "ALSA 参数设置失败（rate=%u period=%lu buffer=%lu）",
             rate, (unsigned long)period, (unsigned long)buffer);
        snd_pcm_hw_params_free(hw);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return -1;
    }
    snd_pcm_hw_params_free(hw);
    snd_pcm_prepare(s_pcm);

    /* 实际生效值可能被驱动就近调整，打出来才知道真实延迟预算 */
    LOGT(TAG, "ALSA 就绪：rate=%u period=%lu 帧(%.1fms) buffer=%lu 帧(%.1fms)",
         rate, (unsigned long)period, 1000.0 * period / rate,
         (unsigned long)buffer, 1000.0 * buffer / rate);
    return 0;
}

static void alsa_close(void)
{
    if (s_pcm) {
        snd_pcm_drain(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
    }
}

static int alsa_write(const int16_t *buf, int frames)
{
    int done = 0;
    while (done < frames) {
        snd_pcm_sframes_t n = snd_pcm_writei(s_pcm, buf + (size_t)done * s_channels,
                                             (snd_pcm_uframes_t)(frames - done));
        if (n == -EPIPE) {                    /* underrun */
            status_t *st = status_lock();
            st->underruns++;
            status_unlock();
            LOGW(TAG, "ALSA underrun，已重新 prepare");
            snd_pcm_prepare(s_pcm);
            continue;
        }
        if (n == -ESTRPIPE) {
            while (snd_pcm_resume(s_pcm) == -EAGAIN) os_sleep_ms(1);
            snd_pcm_prepare(s_pcm);
            continue;
        }
        if (n < 0) {
            LOGE(TAG, "ALSA 写失败: %s", snd_strerror((int)n));
            return -1;
        }
        done += (int)n;
    }
    return 0;
}
#endif /* BOARD */

/* 空 sink：按墙上时钟推进，模拟真实播放节奏（主机验证用） */
static void null_sink_pace(int frames)
{
    int64_t target, now;

    s_out_frames += frames;
    target = s_base_ms + (int64_t)(s_out_frames * 1000 / s_rate);
    now = now_ms();
    if (target > now) {
        int wait = (int)(target - now);
        os_sleep_ms(wait > 0 ? wait : 0);
    }
}

/* ================= 源读取与重采样 ================= */
static int src_refill(void)
{
    int consumed = (int)s_src_pos;
    int remain = s_src_len - consumed;
    int want, got;

    if (consumed > 0) {
        if (remain > 0)
            memmove(s_srcbuf, s_srcbuf + (size_t)consumed * s_wav.channels,
                    (size_t)remain * s_wav.channels * sizeof(int16_t));
        s_src_len = remain;
        s_src_pos -= consumed;
    }
    if (s_src_eof) return 0;

    want = SRC_BUF_FRAMES - s_src_len;
    if (want > s_wav.frames_left) want = (int)s_wav.frames_left;
    if (want <= 0) { s_src_eof = 1; return 0; }

    got = (int)fread(s_srcbuf + (size_t)s_src_len * s_wav.channels,
                     sizeof(int16_t) * s_wav.channels, (size_t)want, s_wav.fp);
    if (got < want) s_src_eof = 1;
    s_wav.frames_left -= got;

    /* 单声道素材复制成双声道，下游一律按立体声处理 */
    if (s_wav.channels == 1) {
        int i;
        for (i = s_src_len + got - 1; i >= s_src_len; i--) {
            s_srcbuf[i * 2 + 1] = s_srcbuf[i];
            s_srcbuf[i * 2]     = s_srcbuf[i];
        }
    }
    s_src_len += got;
    return got;
}

/*
 * 线性插值重采样。step = 源采样率 × 速度 / 输出采样率，
 * 一步完成「变速」与「采样率转换」两件事。
 * 返回实际产出的帧数（< frames 表示源已结束）。
 */
static int resample(int frames, double step)
{
    int produced = 0;

    while (produced < frames) {
        int i0, i1;
        double frac;
        int ch;

        while ((int)s_src_pos >= s_src_len - 1) {
            if (src_refill() <= 0 && (int)s_src_pos >= s_src_len - 1)
                return produced;                  /* 源耗尽 */
        }
        i0 = (int)s_src_pos;
        i1 = i0 + 1;
        if (i1 >= s_src_len) i1 = i0;
        frac = s_src_pos - i0;

        for (ch = 0; ch < s_channels; ch++) {
            int a = s_srcbuf[i0 * s_wav.channels + ch];
            int b = s_srcbuf[i1 * s_wav.channels + ch];
            double v = a + (b - a) * frac;
            s_out[produced * s_channels + ch] = (int16_t)v;
        }
        s_src_pos += step;
        produced++;
    }
    return produced;
}

/* ================= 输出处理 ================= */
static void apply_volume(int16_t *buf, int frames, int volume)
{
    float gain;
    int i, total;

    if (volume >= VOL_MAX) return;
    /* 平方曲线：小音量段更细腻，符合听感；100 时严格 unity */
    gain = (float)volume / (float)VOL_MAX;
    gain = gain * gain;

    total = frames * s_channels;
    for (i = 0; i < total; i++) {
        float v = buf[i] * gain;
        if (v > 32767.0f)  v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        buf[i] = (int16_t)v;
    }
}

static void fill_scope_from(const int16_t *buf, int frames)
{
    int i, stride = frames / SCOPE_POINTS;

    if (stride < 1) stride = 1;
    for (i = 0; i < SCOPE_POINTS; i++) {
        int idx = i * stride;
        if (idx >= frames) idx = frames - 1;
        s_scope[i] = buf[idx * s_channels];
    }
}

void player_fill_scope(int16_t *dst, int points)
{
    int i;
    if (!dst || points <= 0) return;
    for (i = 0; i < points && i < SCOPE_POINTS; i++) dst[i] = s_scope[i];
    for (; i < points; i++) dst[i] = 0;
}

/* ================= 文件打开 / 切歌 ================= */
static int track_open(int index)
{
    const char *path;
    int rc;

    if (g_app.track_count <= 0) return -1;
    if (index < 0) index = g_app.track_count - 1;
    if (index >= g_app.track_count) index = 0;
    path = g_app.track[index];

    s_wav.fp = fopen(path, "rb");
    if (!s_wav.fp) {
        LOGE(TAG, "打开曲目失败：%s", path);
        return -1;
    }
    rc = wav_parse(s_wav.fp, &s_wav);
    if (rc != 0) {
        LOGE(TAG, "WAV 头非法（rc=%d，需 PCM/16bit/1~2 声道）：%s", rc, path);
        fclose(s_wav.fp);
        s_wav.fp = NULL;
        return -1;
    }
    /* 源采样率与输出不同没关系，重采样的 step 会一并处理 */
    s_src_len = s_src_eof = 0;
    s_src_pos = 0.0;
    s_track_index = index;
    s_wav_open = 1;
    s_base_ms = now_ms();
    s_out_frames = 0;
    effect_reset();

    LOGT(TAG, "曲目 %d/%d：%s（%d Hz / %d ch / %.1f s）",
         index + 1, g_app.track_count, path,
         s_wav.rate, s_wav.channels, (double)s_wav.total_frames / s_wav.rate);
    return 0;
}

static void track_close(void)
{
    if (s_wav.fp) fclose(s_wav.fp);
    s_wav.fp = NULL;
    s_wav_open = 0;
    s_src_len = s_src_eof = 0;
    s_src_pos = 0.0;
}

/* ================= 对外接口 ================= */
int player_init(void)
{
    s_state = PLAY_STOPPED;
    if (effect_init(s_rate, s_channels) != 0) return -1;
    LOGD(TAG, "初始化完成：输出 %d Hz / %d ch / period %d 帧",
         s_rate, s_channels, AUDIO_PERIOD_FRAMES);
    return 0;
}

void player_finish(void)
{
    track_close();
    effect_finish();
#if defined(BOARD) && BOARD
    alsa_close();
#endif
}

void player_request(play_state_t st)
{
    pthread_mutex_lock(&s_mtx);
    s_req = st;
    pthread_mutex_unlock(&s_mtx);
}

play_state_t player_current_state(void)
{
    play_state_t s;
    pthread_mutex_lock(&s_mtx);
    s = s_state;
    pthread_mutex_unlock(&s_mtx);
    return s;
}

int player_track_index(void)
{
    return s_track_index;
}

int player_skip(int dir)
{
    int idx;
    pthread_mutex_lock(&s_mtx);
    idx = s_track_index + (dir >= 0 ? 1 : -1);
    if (g_app.track_count > 0) {
        if (idx < 0) idx = g_app.track_count - 1;
        if (idx >= g_app.track_count) idx = 0;
        s_track_index = idx;
        s_reopen = 1;
        if (s_req == PLAY_STOPPED) s_req = PLAY_PLAYING;
    } else {
        idx = -1;
    }
    pthread_mutex_unlock(&s_mtx);
    return idx;
}

/* 把当前状态写进快照（低频，避免锁竞争） */
static void publish_status(void)
{
    audio_params_t p;
    status_t *st;

    params_get(&p);
    st = status_lock();
    st->params       = p;
    st->state        = s_state;
    st->track_index  = s_track_index;
    st->track_count  = g_app.track_count;
    st->len_ms       = s_wav.rate > 0
                       ? (int)(s_wav.total_frames * 1000L / s_wav.rate) : 0;
    st->pos_ms       = s_wav.rate > 0
                       ? (int)((s_wav.total_frames - s_wav.frames_left) * 1000L / s_wav.rate)
                       : 0;
    player_fill_scope(st->scope, SCOPE_POINTS);
    status_unlock();
}

/* ================= 播放线程 ================= */
void *player_thread(void *arg)
{
    int periods = 0;
    (void)arg;

#if defined(BOARD) && BOARD
    if (alsa_open() != 0) {
        s_state = PLAY_ERROR;
        publish_status();
        LOGE(TAG, "音频设备不可用，播放线程退出（显示/网络/手势仍可用）");
        return NULL;
    }
#endif

    while (!g_quit) {
        play_state_t req;
        audio_params_t p;
        effect_cfg_t ecfg;
        int frames, reopen;
        double step;

        pthread_mutex_lock(&s_mtx);
        req = s_req;
        reopen = s_reopen;
        s_reopen = 0;
        pthread_mutex_unlock(&s_mtx);

        /* 播完后再收到 PLAY，按「从头播放」处理，而不是立刻又判一次 EOF */
        if (!reopen && req == PLAY_PLAYING && s_state == PLAY_EOF) reopen = 1;

        if (reopen) {
            track_close();
            if (g_app.track_count > 0 && track_open(s_track_index) != 0) {
                pthread_mutex_lock(&s_mtx);
                s_state = PLAY_ERROR;
                s_req = PLAY_STOPPED;
                pthread_mutex_unlock(&s_mtx);
                publish_status();
                continue;
            }
            s_state = PLAY_PLAYING;
            publish_status();
            continue;
        }

        switch (req) {
        case PLAY_STOPPED:
            if (s_state != PLAY_STOPPED) {
                track_close();
                s_state = PLAY_STOPPED;
                publish_status();
            }
            os_sleep_ms(20);
            continue;
        case PLAY_PAUSED:
            s_state = PLAY_PAUSED;
            os_sleep_ms(20);
            continue;
        case PLAY_EOF:
            os_sleep_ms(20);
            continue;
        default:
            break;
        }

        if (!s_wav_open) {
            if (g_app.track_count == 0) {
                s_state = PLAY_STOPPED;
                os_sleep_ms(50);
                continue;
            }
            if (track_open(s_track_index) != 0) {
                pthread_mutex_lock(&s_mtx);
                s_state = PLAY_ERROR;
                s_req = PLAY_STOPPED;
                pthread_mutex_unlock(&s_mtx);
                continue;
            }
        }

        params_get(&p);
        step = (double)s_wav.rate * (p.speed_x10 / 10.0) / (double)s_rate;

        frames = resample(AUDIO_PERIOD_FRAMES, step);
        if (frames <= 0) {
            pthread_mutex_lock(&s_mtx);
            s_state = PLAY_EOF;
            s_req = PLAY_EOF;
            pthread_mutex_unlock(&s_mtx);
            LOGT(TAG, "曲目播完（track %d）", s_track_index + 1);
            publish_status();
            continue;
        }

        ecfg.echo_on       = p.echo_on;
        ecfg.echo_delay_ms = p.echo_delay_ms;
        ecfg.echo_fb       = p.echo_fb;
        ecfg.eq_low        = p.eq_low;
        ecfg.eq_mid        = p.eq_mid;
        ecfg.eq_high       = p.eq_high;
        effect_set_cfg(&ecfg);
        effect_process(s_out, frames);

        apply_volume(s_out, frames, p.volume);

        fill_scope_from(s_out, frames);

#if defined(BOARD) && BOARD
        if (alsa_write(s_out, frames) != 0) {
            s_state = PLAY_ERROR;
            break;
        }
#else
        null_sink_pace(frames);
#endif

        s_state = PLAY_PLAYING;
        if (++periods % STATUS_EVERY_N_PERIODS == 0) publish_status();
    }

    publish_status();
    return NULL;
}
