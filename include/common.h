/*
 * common.h —— 全模块公共设施：日志、时间、枚举名
 *
 * 日志约定：
 *   - 统一带「时间戳 级别 [模块] 内容」，板端可直接串口看，也可重定向进文件
 *   - 级别由 --log 控制，默认 info；调试期用 debug
 *   - 严禁在音频写入循环里打 info 级日志（会引入抖动）
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "oscompat.h"

#define ARRAY_LEN(a)        ((int)(sizeof(a) / sizeof((a)[0])))
#define CLAMP(v, lo, hi)    ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* ---------------- 日志 ---------------- */
enum {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3
};

void log_set_level(int level);
int  log_get_level(void);
void log_write(int level, const char *tag, const char *file, int line,
               const char *fmt, ...);

#define LOGT(tag, ...)      log_write(LOG_INFO,  (tag), __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(tag, ...)      log_write(LOG_WARN,  (tag), __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(tag, ...)      log_write(LOG_ERROR, (tag), __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(tag, ...)      log_write(LOG_DEBUG, (tag), __FILE__, __LINE__, __VA_ARGS__)

/* ---------------- 时间 ---------------- */
#define now_ms()            os_now_ms()

/* ---------------- 枚举转字符串（日志 / 协议 / UI 共用） ---------------- */
const char *gesture_name(int g);
const char *play_state_name(int s);
const char *ctrl_src_name(int s);

#endif /* COMMON_H */
