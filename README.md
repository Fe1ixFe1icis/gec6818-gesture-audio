# gec6818-gesture-audio

智能手势控制音频播放器 · 基于 GEC6818 开发板的 Linux 嵌入式项目（粤嵌实习 · 2026-11）

隔空手势控制音乐播放：音量 / 变速 / 切歌 / 音效，LCD 实时反馈，PC 端 TCP 远程控制。

**当前进度：M1 首版基线（v0.1.0-m1）** —— 接口、构建、日志、并发仲裁、四模块骨架、主机模拟后端、板端测试文档齐备；
D1/D2/D3（EOF 后切歌停摆、PAUSE 不上报、ERROR 不上报）已修复并通过主机回归 20/20；
传感器真机、LCD 中文字库、MP3 解码按里程碑推进。

## 文档索引

| 文件 | 内容 |
|---|---|
| `projectBrief.md` | 项目简介：背景、目标、架构、技术栈、通信协议、里程碑、模块分工 |
| `boardGuide.md` | **开发板参考文档**：硬件规格、启动链、设备节点、LCD/触摸/音频/GPIO/I2C/UART 编程要点、自检清单、故障排查 |
| `testGuide.md` | **板端测试文档**：现场测试机（连着板子那台）按 T0–T7 分层执行，含用例表、预期输出、故障对照 |
| `boardSnapshot.md` | **本板实测快照**：ABI（软浮点/glibc 2.23）、fb/ALSA 设备、eth0 PHY 不可用、部署通道结论 |
| `sensorSelection.md` | **M2 传感器选型记录**：PAJ7620U2 对比/映射/接线/验收/采购，测距后置 |
| `ubuntuArmBuild.md` | **导入 Ubuntu 做 ARM 交叉编译的实施方案**：打包/传输、工具链与 ALSA 头、`make verify`、NFS 上板、T0–T3 首跑、故障对照 |
| `devFlow.md` | 开发流程与纪律：分支模型、提交规范、板子占用、NFS 同步、红线 |
| `gitGuide.md` | **Git 操作指南**：初始化、首次提交、分支、保护规则、日常循环、故障回滚 |
| `AGENT.md` | AI 协作规范（铁律：AI 不代执行任何 git 操作；操作分级与留痕要求） |
| `agentLogs/` | AI 操作留痕，`YYYY-MM-DD.md` 一天一个，按 L0–L4 分级记录，供审计对账 |

## 目录结构

```
include/          公共头文件（M1 后冻结）
  config.h        全局常量：采样率、周期、参数范围、端口、仲裁窗口
  params.h        共享数据结构：audio_params_t / ctrl_msg_t / status_t
  ctrl.h          参数存储 + 控制队列 + 状态快照的接口
  sensor.h player.h effect.h display.h server.h
  common.h        日志、时间、枚举名
  oscompat.h      仅开发机自测用的薄兼容层（板端不参与编译）
  app.h selftest.h
src/              板端源码
  main.c          主控：启动参数、线程装配、**并发仲裁**
  ctrl.c          参数/队列/状态（三把独立锁）
  sensor.c        手势与测距（sim 后端可用；i2c 为 M2 占位）
  player.c        WAV 读取 → 变速重采样 → 效果链 → 音量 → ALSA/空 sink
  effect.c        回声（延迟线）+ 三段 EQ（biquad）
  display.c       LCD 800×480 双缓冲 + 5×7 点阵字库；主机侧可导 BMP
  server.c        TCP 服务器：协议解析、状态周期广播
  selftest.c      内置 20 项自检（不依赖声卡/LCD/传感器）
client/           PC 客户端（M6）
tools/
  gen_wav.c       测试素材生成器（1s 双音 + 1s 静音循环）
  netctl.py       命令行 TCP 客户端（现场测试用）
  armbuild.sh     Ubuntu 侧一键交叉编译 + ABI 校验（BOARD=1）
  pack_for_ubuntu.ps1  Windows 侧打包成可导入 Ubuntu 的源码 tar.gz
  player_regress.py    主机侧 D1/D2/D3 状态机回归（自带 WAV 合成）
  prep_demo_wav.py     大 WAV 裁剪/降混/重采样（串口部署前压缩体积）
  make_serial_upload.ps1  无 rz/rb 时生成 base64 串口上传脚本
  i2c_probe.c     单地址 I2C 探测（M2 确认 0x73，避免全总线盲扫）
```

## 编译

```bash
# 主机自测（无板子也能跑逻辑层）
make

# 板端（Ubuntu 18.04 集成机，交叉编译）
# 本板实测：软浮点 ABI（/lib/ld-linux.so.3，glibc 2.23）→ 用 arm-linux-
make BOARD=1 CROSS=arm-linux-
make BOARD=1 CROSS=arm-linux- READELF=readelf verify   # T0：架构/解释器/依赖
```

**从 Windows 导入 Ubuntu 一条龙**（打包 → 传输 → 交叉编译 → 校验 → NFS 上板）：

```powershell
# Windows 仓库根目录
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1
```

```bash
# Ubuntu 内（解包后）
bash tools/armbuild.sh
```

完整步骤、工具链选择、ALSA 头缺失的处理、故障对照见 **`ubuntuArmBuild.md`**。

产物：`build/gesture_audio`。板端版链接 ALSA（`-lasound`），主机版无硬件依赖。

> ⚠️ **工具链前缀必须与板端 loader 一致**。本板实测是 `/lib/ld-linux.so.3`
> （软浮点，glibc 2.23），对应 `arm-linux-gcc`（`arm-none-linux-gnueabi`）；
> 用 `gnueabihf` 编出的程序会要求 `/lib/ld-linux-armhf.so.3`，在本板报
> `No such file or directory`。详见 `boardGuide.md §2.5`。

## 运行

```bash
# 逻辑自检（不需要声卡、LCD、传感器）
./build/gesture_audio --selftest /mnt/nfs/audio/test_44100.wav

# 板端正式运行
./build/gesture_audio --sensor sim --log info /mnt/nfs/audio/test_44100.wav

# 检查界面版式（渲染一帧写 BMP，可拷回电脑看）
./build/gesture_audio --dump-bmp /tmp/ui.bmp

# 远程控制
python3 tools/netctl.py <板子IP> 8888 "VOL 70" "SPEED 1.5" "STATUS"
```

常用选项：`--port` / `--sensor sim|i2c` / `--no-display` / `--run <秒>` /
`--log error|warn|info|debug` / `--selftest` / `--dump-bmp`，`--help` 看全部。

## 架构要点（读代码前先看这三条）

1. **单写者**：手势、网络、按键都只是**指令生产者**，投递到唯一消费队列；主控线程
   做仲裁后写参数存储；播放/显示只读。这样"两路同时改音量"不会打架。
2. **接口先行**：`include/` 下的结构体与 `projectBrief.md §6` 的协议在 M1 后冻结，
   改动需开 `proto/xxx` 分支并全员同意。
3. **主机可测**：所有板端接口（ALSA / framebuffer / I2C）都有 `BOARD=0` 退化路径，
   保证没板子时也能编译、跑通全链路并做自检——这也是 `testGuide.md` 能分层定位问题的前提。

调试产物不走 git，通过 NFS 上板：

```bash
mount -t nfs -o nolock,vers=3 <集成机IP>:/home/gec6818/nfs /mnt/nfs
```

## 协作约定（摘要）

- 分支：`main` 稳定（禁止直接 push）、`dev` 集成、`feat/<模块>` 开发，PR 至少 1 人 Review
- 提交信息：`<type>(<scope>): <描述>`，如 `feat(effect): 实现回声延迟环形缓冲区`
- 不提交编译产物与音频素材（见 `.gitignore`）

## 注意

> 本仓库的 **所有 git 操作由人类执行**。AI 只提供命令指南，不代执行（详见 `AGENT.md`）。
