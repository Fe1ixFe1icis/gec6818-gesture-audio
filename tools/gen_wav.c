/*
 * gen_wav.c —— 测试素材生成器（主机侧工具，不进板端产物）
 *
 * 生成一段确定性的 WAV：每 2 秒一个循环块——
 *   前 1 秒：双音（440 Hz + 880 Hz），用于听音高、验回声与 EQ
 *   后 1 秒：静音，用于看波形间隙、验 EOF 与切歌边界
 *
 * 为什么固定 44.1kHz / 立体声 / 16bit：
 *   player.c 的 wav_parse() 只接受 PCM(1) / 16bit / 1~2 声道（boardGuide.md §3.3
 *   也建议板端派生率避免软件转换）。素材与播放器不一致时，问题会表现成
 *   「没声音」，排查成本极高，所以素材参数写死在这里，不在命令行放宽。
 *
 * 用法：
 *   cc -O2 -o gen_wav tools/gen_wav.c -lm
 *   ./gen_wav test_44100.wav          # 默认 8 秒
 *   ./gen_wav test_44100.wav 30       # 30 秒（长跑用）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define RATE      44100
#define CHANNELS  2
#define BITS      16
#define BLOCK_MS  2000                 /* 一个循环块：1s 双音 + 1s 静音 */
#define TONE_MS   1000

static const double kPI2 = 6.283185307179586;

typedef struct {
    FILE *fp;
    unsigned long frames;              /* 已写帧数，用于回填 data 长度 */
    unsigned long data_bytes;
} wav_writer_t;

static void wr_u16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void wr_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* 先写占位头，数据写完后回填尺寸（长度只有最后才知道） */
static int wav_open(wav_writer_t *w, const char *path)
{
    unsigned char hdr[44];
    unsigned int byte_rate = (unsigned int)(RATE * CHANNELS * (BITS / 8));
    unsigned short block_align = (unsigned short)(CHANNELS * (BITS / 8));

    w->fp = fopen(path, "wb");
    if (!w->fp) return -1;

    memcpy(hdr + 0, "RIFF", 4);
    wr_u32(hdr + 4, 0);                       /* 待回填 */
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr_u32(hdr + 16, 16);                     /* PCM 的 fmt 块长度 */
    wr_u16(hdr + 20, 1);                      /* audio_format = PCM */
    wr_u16(hdr + 22, CHANNELS);
    wr_u32(hdr + 24, RATE);
    wr_u32(hdr + 28, byte_rate);
    wr_u16(hdr + 32, block_align);
    wr_u16(hdr + 34, BITS);
    memcpy(hdr + 36, "data", 4);
    wr_u32(hdr + 40, 0);                      /* 待回填 */

    if (fwrite(hdr, 1, sizeof hdr, w->fp) != sizeof hdr) return -1;
    w->frames = 0;
    w->data_bytes = 0;
    return 0;
}

static int wav_close(wav_writer_t *w)
{
    unsigned char patch[4];
    unsigned int riff_size;

    if (!w->fp) return -1;

    riff_size = 36u + (unsigned int)w->data_bytes;
    wr_u32(patch, riff_size);
    fseek(w->fp, 4, SEEK_SET);
    fwrite(patch, 1, 4, w->fp);

    wr_u32(patch, (unsigned int)w->data_bytes);
    fseek(w->fp, 40, SEEK_SET);
    fwrite(patch, 1, 4, w->fp);

    fclose(w->fp);
    w->fp = NULL;
    return 0;
}

/* 写一块：帧数由调用方给，样本值由 gen 函数决定 */
static int wav_write_block(wav_writer_t *w, const short *samples, int frames)
{
    size_t bytes = (size_t)frames * CHANNELS * sizeof(short);

    if (fwrite(samples, 1, bytes, w->fp) != bytes) return -1;
    w->frames += (unsigned long)frames;
    w->data_bytes += (unsigned long)bytes;
    return 0;
}

/* 双音：两路正弦等幅相加，峰值控制在 0.5 满量程，留 EQ 抬升余量 */
static void fill_tone(short *dst, int frames, long long start_frame)
{
    int i, ch;

    for (i = 0; i < frames; i++) {
        long long n = start_frame + i;
        double t = (double)n / (double)RATE;
        double v = 8000.0 * sin(kPI2 * 440.0 * t)
                 + 8000.0 * sin(kPI2 * 880.0 * t);
        short s = (short)(v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : v));
        for (ch = 0; ch < CHANNELS; ch++)
            dst[i * CHANNELS + ch] = s;
    }
}

int main(int argc, char **argv)
{
    const char *path;
    int seconds = 8;
    int frames_per_block, tone_frames, silence_frames;
    int blocks, b;
    int16_t *tone = NULL, *silence = NULL;
    wav_writer_t w;
    long long pos = 0;

    if (argc < 2) {
        fprintf(stderr, "用法: %s <输出.wav> [总时长秒数，默认 8]\n", argv[0]);
        return 2;
    }
    path = argv[1];
    if (argc >= 3) {
        seconds = atoi(argv[2]);
        if (seconds <= 0) {
            fprintf(stderr, "时长必须为正整数秒：%s\n", argv[2]);
            return 2;
        }
    }

    frames_per_block = RATE * BLOCK_MS / 1000;
    tone_frames      = RATE * TONE_MS / 1000;
    silence_frames   = frames_per_block - tone_frames;
    blocks           = (seconds * 1000 + BLOCK_MS - 1) / BLOCK_MS;

    tone = malloc(sizeof(int16_t) * tone_frames * CHANNELS);
    silence = calloc((size_t)silence_frames * CHANNELS, sizeof(int16_t));
    if (!tone || !silence) {
        fprintf(stderr, "内存不足（tone=%d 帧，silence=%d 帧）\n",
                tone_frames, silence_frames);
        free(tone);
        free(silence);
        return 1;
    }

    if (wav_open(&w, path) != 0) {
        fprintf(stderr, "无法写入：%s\n", path);
        free(tone);
        free(silence);
        return 1;
    }

    for (b = 0; b < blocks; b++) {
        long long limit = (long long)seconds * RATE;
        int want_tone = tone_frames;

        if (pos + want_tone > limit) want_tone = (int)(limit - pos);   /* 尾部收口 */
        if (want_tone <= 0) break;

        fill_tone(tone, want_tone, pos);
        pos += want_tone;
        if (wav_write_block(&w, tone, want_tone) != 0) break;

        /* 静音段按剩余时长截断，保证总时长精确等于 seconds 秒 */
        {
            int want = silence_frames;
            if (pos + want > limit) want = (int)(limit - pos);
            if (want > 0) {
                if (wav_write_block(&w, silence, want) != 0) break;
                pos += want;
            }
        }
    }

    if (wav_close(&w) != 0) {
        fprintf(stderr, "写盘失败：%s\n", path);
        free(tone);
        free(silence);
        return 1;
    }

    printf("已生成 %s：%d Hz / %d ch / %d bit / %.2f s（每 %d ms：%d ms 双音 + %d ms 静音）\n",
           path, RATE, CHANNELS, BITS,
           (double)w.frames / RATE, BLOCK_MS, TONE_MS, BLOCK_MS - TONE_MS);

    free(tone);
    free(silence);
    return 0;
}
