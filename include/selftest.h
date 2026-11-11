/*
 * selftest.h —— 板端/主机通用的自检入口
 *
 * 目的：让「现场测试机」在**不接音频输入、不接传感器**的情况下，
 *       用一条命令确认核心逻辑（参数 clamp / 消息队列 / 效果链 / WAV 解析 /
 *       协议解析）是好的，从而把「框架问题」与「硬件问题」分开定位。
 *
 * 约定：返回失败用例数（0 = 全通过），main 用它当进程退出码。
 */
#ifndef SELFTEST_H
#define SELFTEST_H

int selftest_run(const char *wav_path);

#endif /* SELFTEST_H */
