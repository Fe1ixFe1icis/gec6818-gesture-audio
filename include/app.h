/*
 * app.h —— 进程级运行上下文
 *
 * 只放「启动参数 + 全局退出标志」，不放业务状态（业务状态在 ctrl.h）。
 * 采用单个全局实例而非层层传参，是为了让线程入口签名保持简单；
 * 代价是模块间可隐式依赖启动参数，故字段一律只读（init 之后不再修改）。
 */
#ifndef APP_H
#define APP_H

#include "config.h"

typedef struct {
    /* 启动参数（main 解析后只读） */
    const char *track[MAX_TRACKS];      /* 曲目路径，按顺序播放 */
    int         track_count;
    int         port;                   /* TCP 监听端口 */
    int         sensor_backend;         /* SENSOR_BACKEND_* */
    int         display_enabled;        /* 0 = 不启动显示线程 */
    const char *dump_bmp;               /* 非空则渲染一帧写 BMP 后退出 */
    int         run_seconds;            /* >0 则到时自动退出（自动化测试用） */
    int         log_level;

    /* 运行期只读的环境信息 */
    int         board_build;            /* 1 = BOARD=1 交叉编译产物 */
} app_ctx_t;

extern app_ctx_t g_app;

/* 全局退出标志：volatile 保证跨线程可见性；写者只有一个（信号/主控） */
extern volatile int g_quit;

#endif /* APP_H */
