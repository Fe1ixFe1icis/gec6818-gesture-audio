/*
 * log.c —— 日志实现
 *
 * 输出到 stdout（板端由串口 console 直接可见；需要留档时用 shell 重定向）。
 * 不做日志文件轮转：本项目的日志用于当场排查，长期记录由测试文档规定的
 * “会话开始/结束各重定向一次”解决，避免在嵌入式设备上引入额外 I/O 抖动。
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "common.h"
#include "params.h"
#include "app.h"

static int s_level = LOG_INFO;
/* 多线程同时打日志会串行(交错)成半行，读起来像乱码。
 * 一条日志必须在锁内一次性写完，这是排查问题的基本要求。 */
static pthread_mutex_t s_log_mtx = PTHREAD_MUTEX_INITIALIZER;

void log_set_level(int level)
{
    s_level = CLAMP(level, LOG_ERROR, LOG_DEBUG);
}

int log_get_level(void)
{
    return s_level;
}

/* 只取文件名，去掉编译机上的长路径（交叉编译时路径对板端无意义） */
static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    const char *q = strrchr(path, '\\');
    if (q && (!p || q > p)) p = q;
    return p ? p + 1 : path;
}

void log_write(int level, const char *tag, const char *file, int line,
               const char *fmt, ...)
{
    char stamp[16];
    struct tm tmv;
    time_t t;
    va_list ap;

    if (level > s_level) return;

    t = time(NULL);
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    snprintf(stamp, sizeof stamp, "%02d:%02d:%02d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    pthread_mutex_lock(&s_log_mtx);
    fprintf(stdout, "%s %-5s [%s] ", stamp,
            level == LOG_ERROR ? "ERROR" :
            level == LOG_WARN  ? "WARN"  :
            level == LOG_INFO  ? "INFO"  : "DEBUG",
            tag);

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);

    /* 只有 debug 级才打文件行号，info 级保持输出干净（串口带宽宝贵） */
    if (level == LOG_DEBUG)
        fprintf(stdout, "  (%s:%d)", base_name(file), line);

    fputc('\n', stdout);
    fflush(stdout);
    pthread_mutex_unlock(&s_log_mtx);
}

const char *gesture_name(int g)
{
    switch (g) {
    case GESTURE_UP:    return "UP";
    case GESTURE_DOWN:  return "DOWN";
    case GESTURE_LEFT:  return "LEFT";
    case GESTURE_RIGHT: return "RIGHT";
    case GESTURE_PUSH:  return "PUSH";
    case GESTURE_PULL:  return "PULL";
    case GESTURE_HOVER: return "HOVER";
    default:            return "NONE";
    }
}

const char *play_state_name(int s)
{
    switch (s) {
    case PLAY_PLAYING: return "PLAYING";
    case PLAY_PAUSED:  return "PAUSED";
    case PLAY_EOF:     return "EOF";
    case PLAY_ERROR:   return "ERROR";
    default:           return "STOPPED";
    }
}

const char *ctrl_src_name(int s)
{
    switch (s) {
    case SRC_GESTURE: return "gesture";
    case SRC_NET:     return "net";
    case SRC_BUTTON:  return "button";
    default:          return "system";
    }
}
