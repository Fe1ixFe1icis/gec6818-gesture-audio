/*
 * sensor.h —— 传感器采集模块（M2 交付，此处先立骨架）
 *
 * 后端抽象的原因（boardGuide.md §3.5 / §3.7）：
 *   - 板端传感器型号尚未定案（I2C 手势 / GPIO 超声 / UART 模块三条路径）
 *   - AI 侧无硬件，必须能在主机上跑通「采集 → 手势判定 → 投递指令」全链路
 * 因此 v1 提供 SIM（脚本化模拟）与 I2C（占位，待型号确认）两种后端，
 * 上层逻辑与后端无关；M2 把 I2C 后端填实即可，不动其它模块。
 */
#ifndef SENSOR_H
#define SENSOR_H

#include "params.h"

typedef enum {
    SENSOR_BACKEND_SIM = 0,   /* 主机/板端通用的脚本化模拟 */
    SENSOR_BACKEND_I2C = 1    /* 真实 I2C 手势传感器（M2 填实） */
} sensor_backend_t;

/*
 * 初始化。失败不致命：打印告警后自动降级为 SIM 后端，
 * 保证「板子上没接传感器」时其它模块仍可演示。
 * 返回实际生效的后端（>=0），<0 表示彻底失败。
 */
int  sensor_init(int backend);
void sensor_finish(void);

/* 线程入口，由 main 创建（内部按 SENSOR_POLL_MS 轮询并投递手势指令） */
void *sensor_thread(void *arg);

/* 单次采样：返回手势（GESTURE_NONE 表示本周期无事件），并写测距值 */
gesture_t sensor_poll(int *distance_mm);

const char *sensor_backend_name(int backend);

#endif /* SENSOR_H */
