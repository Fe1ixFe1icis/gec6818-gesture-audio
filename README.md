# gec6818-gesture-audio

智能手势控制音频播放器 · 基于 GEC6818 开发板的 Linux 嵌入式项目（粤嵌实习 · 2026-11）

隔空手势控制音乐播放：音量 / 变速 / 切歌 / 音效，LCD 实时反馈，PC 端 TCP 远程控制。

## 文档索引

| 文件 | 内容 |
|---|---|
| `projectBrief.md` | 项目简介：背景、目标、架构、技术栈、通信协议、里程碑、模块分工 |
| `boardGuide.md` | **开发板参考文档**：硬件规格、启动链、设备节点、LCD/触摸/音频/GPIO/I2C/UART 编程要点、自检清单、故障排查 |
| `devFlow.md` | 开发流程与纪律：分支模型、提交规范、板子占用、NFS 同步、红线 |
| `gitGuide.md` | **Git 操作指南**：初始化、首次提交、分支、保护规则、日常循环、故障回滚 |
| `AGENT.md` | AI 协作规范（铁律：AI 不代执行任何 git 操作；操作分级与留痕要求） |
| `agentLogs/` | AI 操作留痕，`YYYY-MM-DD.md` 一天一个，按 L0–L4 分级记录，供审计对账 |

## 目录结构（规划）

```
include/     公共头文件、共享参数结构体（M1 结束后冻结）
src/         板端源码
  sensor.c   传感器采集（I2C 手势 / 距离，滑动平均滤波）
  player.c   播放控制（状态机、变速、解码调度）
  effect.c   数字音效（回声 / 三段 EQ / 变调）
  display.c  LCD 界面绘制
  server.c   TCP 服务器、协议解析、状态上报
  main.c     主控与模块组装
client/      PC 客户端
tools/       主机端离线验证工具（如 WAV 音效测试）
```

## 编译环境

- 板端：GEC6818（ARM Cortex-A53），Linux
- 交叉编译：Ubuntu 18.04 + `arm-linux-gnueabi-gcc`
- 依赖：ALSA（`libasound2-dev`）、`madplay`（MP3 播放）

```bash
# 交叉编译（示例，Makefile 随 M1 落地）
make CROSS=arm-linux-gnueabi-
```

调试产物不走 git，通过 NFS 上板：`mount -t nfs -o nlock <集成机IP>:/home/gec6818/nfs /mnt/nfs`

## 协作约定（摘要）

- 分支：`main` 稳定（禁止直接 push）、`dev` 集成、`feat/<模块>` 开发，PR 至少 1 人 Review
- 提交信息：`<type>(<scope>): <描述>`，如 `feat(effect): 实现回声延迟环形缓冲区`
- 不提交编译产物与音频素材（见 `.gitignore`）

## 注意

> 本仓库的 **所有 git 操作由人类执行**。AI 只提供命令指南，不代执行（详见 `AGENT.md`）。
