/*
 * server.h —— TCP 服务器（M6 交付主体）
 *
 * 协议见 projectBrief.md §6（**冻结**）：行文本，指令一词一行。
 *   控制：PLAY / PAUSE / RESUME / NEXT / PREV / VOL n / SPEED f /
 *         ECHO on|off delay fb / EQ low|mid|high gain / STATUS / QUIT / HELP
 *   应答：OK ... / ERR <原因>；状态行：STATUS vol=.. speed=.. state=.. ...
 *
 * 职责边界（重要）：
 *   服务器只做「解析 → 投递 ctrl 消息 → 回执」，**不直接改参数、不直接调播放器**。
 *   参数改动的唯一通道是主控线程的仲裁，否则双通道并发会互相覆盖。
 */
#ifndef SERVER_H
#define SERVER_H

#include "params.h"

int  server_init(int port);
void server_finish(void);

/* 线程入口：select 多路复用，接受连接 / 收指令 / 周期广播状态 */
void *server_thread(void *arg);

int  server_client_count(void);

/*
 * 解析一行指令。返回：
 *   0  成功，*out 已填充（id/src/a/b/c）
 *  -1  语法错误（未知指令 / 缺参数）
 *  -2  参数越界或非法值
 * err 用于回给客户端的可读原因，长度 >= 64 建议。
 */
int  server_parse_line(const char *line, ctrl_msg_t *out, char *err, int errlen);

/* 组一条 STATUS 行（协议唯一出口，显示与日志复用同一格式） */
const char *server_format_status(char *buf, int len);

#endif /* SERVER_H */
