/*
 * oscompat.h —— 极薄的操作系统差异层
 *
 * 定位：**只为开发机自测服务**。板端（BOARD=1）与 Ubuntu 虚拟机上的
 *       主机自测（BOARD=0）都走 POSIX 分支；只有 Windows 开发机
 *       （本仓库当前的远程开发环境，MinGW + winpthreads）才走 _WIN32 分支，
 *       目的是让 AI / 开发者在没有板子时也能编译并跑通逻辑层。
 *
 * 纪律：
 *   1. 本文件只做「类型别名 + 薄封装」，不实现业务逻辑，不做重定向宏。
 *   2. 任何模块都不得直接 include <winsock2.h>，一律走这里，保证板端零影响。
 *   3. 板端编译时 _WIN32 分支整体不参与，产物与「纯 Linux 代码」等价。
 */
#ifndef OSCOMPAT_H
#define OSCOMPAT_H

#include <stdint.h>
#include <stddef.h>

/* ---------------- Windows 开发机自测分支（板端不参与） ---------------- */
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <signal.h>
#include <time.h>

typedef SOCKET os_sock_t;
#define OS_SOCK_INVALID INVALID_SOCKET

static inline int os_net_init(void)
{
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
}
static inline void os_net_cleanup(void) { WSACleanup(); }
static inline int os_sock_close(os_sock_t s)
{
    return (s == OS_SOCK_INVALID) ? 0 : closesocket(s);
}
/* 非阻塞 / 被信号打断的判定统一出口 */
static inline int os_sock_would_block(void)
{
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
}
static inline void os_sleep_ms(int ms) { Sleep((DWORD)ms); }
static inline int64_t os_now_ms(void)
{
    return (int64_t)GetTickCount64();          /* 单调，毫秒 */
}
static inline int os_signal_install(void (*fn)(int))
{
    return signal(SIGINT, fn) == SIG_ERR ? -1 : 0;
}

/* ---------------- POSIX 分支（Ubuntu / 板端） ---------------- */
#else

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

typedef int os_sock_t;
#define OS_SOCK_INVALID (-1)

static inline int os_net_init(void) { return 0; }
static inline void os_net_cleanup(void) { }
static inline int os_sock_close(os_sock_t s)
{
    return (s < 0) ? 0 : close(s);
}
static inline int os_sock_would_block(void)
{
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
}
static inline void os_sleep_ms(int ms)
{
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
static inline int64_t os_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static inline int os_signal_install(void (*fn)(int))
{
    struct sigaction sa;
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGINT, &sa, NULL);
}

#endif /* _WIN32 */

/* 绝对时间点（供 pthread_cond_timedwait 用），单位：毫秒 since epoch */
static inline void os_realtime_after_ms(struct timespec *ts, int ms)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* Windows 时间：100ns since 1601；转成 ns since 1970 */
    u.QuadPart -= 116444736000000000ULL;
    {
        int64_t ns = (int64_t)u.QuadPart * 100 + (int64_t)ms * 1000000;
        ts->tv_sec  = (time_t)(ns / 1000000000);
        ts->tv_nsec = (long)(ns % 1000000000);
    }
#else
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_nsec -= 1000000000L; ts->tv_sec++; }
#endif
}

#endif /* OSCOMPAT_H */
