/*
 * player.h —— 播放引擎（M3 交付主体）
 *
 * 流水线（projectBrief.md §4）：
 *   WAV 读取 → 变速重采样 → 效果链(effect.c) → 音量 → 输出 sink
 * 各级独立、可旁通；v1 用 WAV 而非 MP3，把解码从关键路径上移除
 * （boardGuide.md §3.3 的明确建议），M3 之后再决定是否引入 libmad。
 *
 * sink 抽象：
 *   BOARD=1 → ALSA（/dev/snd/pcmC0D0p），写循环处理 xrun
 *   BOARD=0 → 空 sink + 按时钟节拍推进，用于主机端验证时序与状态机
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "params.h"

/* WAV 素材信息，供自检与上板前素材体检使用 */
typedef struct {
    int    rate;
    int    channels;
    int    bits;
    long   frames;      /* 总帧数 */
    double seconds;
} wav_info_t;

int  player_init(void);
void player_finish(void);

/* 线程入口：解码/重采样/效果/输出循环，直到曲目结束或收到退出 */
void *player_thread(void *arg);

/* 主控 → 播放线程的状态机请求（线程安全） */
void        player_request(play_state_t st);
play_state_t player_current_state(void);

/* 切歌：dir = +1 下一首 / -1 上一首；返回新的曲目索引，失败 -1 */
int  player_skip(int dir);
int  player_track_index(void);

/* 只解析 WAV 头，不打开播放：上板前先确认素材参数，避免「没声音」排查半天 */
int  player_probe_wav(const char *path, wav_info_t *info);

/* 把当前输出缓冲的一段样本写入状态快照的 scope（供显示画波形） */
void player_fill_scope(int16_t *dst, int points);

#endif /* PLAYER_H */
