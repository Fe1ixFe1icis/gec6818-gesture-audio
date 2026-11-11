/*
 * ctrl.h —— 参数存储 / 控制消息队列 / 状态快照
 *
 * 三者都由本模块统一加锁管理，其他模块**不得**自己定义全局参数副本，
 * 否则会出现「显示的音量和实际播放的音量不一致」这类并行 bug。
 *
 * 线程模型（单写者原则，boardGuide.md §5）：
 *   生产者：sensor（手势）、server（网络）、main（命令行/按键）→ ctrl_post()
 *   消费者：main 主循环 → ctrl_take() → 仲裁 → params_apply() → player_request()
 *   读者：player（每周期取参数）、display（每帧取状态）、server（周期上报）
 */
#ifndef CTRL_H
#define CTRL_H

#include "params.h"

int  ctrl_init(void);
void ctrl_finish(void);

/* ---- 消息队列（有界，满则丢弃并计数，绝不阻塞生产者） ---- */
int  ctrl_post(const ctrl_msg_t *msg);
int  ctrl_post_cmd(ctrl_id_t id, ctrl_src_t src, int a, int b, int c);
int  ctrl_take(ctrl_msg_t *out, int timeout_ms);   /* 0 = 取到，-1 = 超时 */
int  ctrl_queue_len(void);
unsigned long ctrl_dropped(void);

/* ---- 参数存储 ---- */
void params_get(audio_params_t *out);
void params_set(const audio_params_t *in);
void params_reset_default(void);

/*
 * 把一条指令翻译成参数变更并写入存储（含 clamp）。
 * 返回 0 = 已应用；-1 = 该指令不是参数类（不处理）；-2 = 参数非法被拒。
 * changed 可传 NULL；非空时置 1。
 */
int  params_apply(const ctrl_msg_t *msg, int *changed);

/* 单值设置（内部会 clamp），返回 clamp 后的值 */
int  params_set_volume(int v);
int  params_set_speed_x10(int v);
int  params_set_eq(int band, int gain_db);
unsigned int params_version(void);      /* 每次变更 +1，显示据此判断是否需要重绘 */

/* ---- 状态快照 ---- */
/* 读：整块拷贝，调用方拿不到锁外的悬空指针 */
void status_get(status_t *out);
/* 写：短暂持锁修改，临界区里不要做慢操作（不要 memcpy 大数组以外的事） */
status_t *status_lock(void);
void      status_unlock(void);

#endif /* CTRL_H */
