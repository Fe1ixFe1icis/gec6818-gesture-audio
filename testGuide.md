# 板端测试文档（testGuide.md）

> 适用：智能手势控制音频播放器（`projectBrief.md`）· 版本 **V1.0** · 2026-11-11
> 执行者：现场测试机（连着 GEC6818 那台）。分层执行 T0 → T7，每层通过再进下一层。
> 定位：**主板之前先证明"程序本身没坏"**，把硬件问题与代码问题分开，避免上板后瞎试。

## 0. 标记与判定口径

本文每条结论都带标记，含义固定：

| 标记 | 含义 |
|---|---|
| ✅ | 主机（`BOARD=0`，Windows/Linux 均可）已实测通过，附实测值 |
| ⏳ | 必须真板（ALSA / framebuffer / I2C / NFS）才能确认，本机未验证 |
| ⚠️ | 已知限制或缺陷，见 §5；不算"测试失败"，但不能当通过 |

判定口径：
- **T1 自检必须全 PASS**（T1 不通过就别上板，代码有问题）。
- **T3/T4/T5/T6 中出现"预期不符"记为用例失败**，记下现象与日志，不要现场改代码。
- ⏳ 项本机没有结论，**不得**把本机结果当成板端已通过。

## 1. 前置条件

| 项 | 要求 |
|---|---|
| 板子 | GEC6818，已能串口/网络登录（见 `boardGuide.md §2`） |
| 集成机 | Ubuntu 18.04 + `arm-linux-gcc`（**本板软浮点**；`-dumpmachine` = `arm-none-linux-gnueabi`，见 `boardGuide.md §2.5`） |
| 共享目录 | 集成机 `~/nfs` 已导出，板子已挂载 `/mnt/nfs` |
| 素材 | 一份 44.1kHz / 16bit / 1~2 声道 WAV（§2） |
| 客户端 | 任意 PC 上的 Python 3 + `tools/netctl.py` |
| 报备 | 已在工作群报备占用时段（默认 40 分钟），离开恢复网络并报"板子空闲" |

### 1.1 编译

```bash
# 集成机（板端，推荐；本板软浮点 → arm-linux-）
make BOARD=1 CROSS=arm-linux-
# 产物 build/gesture_audio，链接 ALSA

# 集成机（主机侧自测，验证代码再上板）
make && make tools
```

⚠️ **Windows 开发机**：若 PATH 里没有 `sh.exe`（Git Bash 未装/未加 PATH），
`mingw32-make` 会在 `SHELL := sh` 上直接失败（`CreateProcess(NULL, rm -rf build, ...) failed`）。
这种机器请用附录 A 的单行 gcc 命令，或把 `C:\Program Files\Git\bin` 加进 PATH 后再用 make。

### 1.2 上板部署

> **本板 eth0 实测不可用**（`Cannot attach to PHY -19`，PHY ID 全 0），NFS/scp 走不通。
> 当前用 **TF 卡 / U 盘 / 串口 Zmodem** 把 `gapp.tgz` 送上去，详见 `ubuntuArmBuild.md §7`。

```bash
# 集成机：把构建产物、libasound、配置树、素材打成一个包
cd ~/gec6818
mkdir -p ~/stage/{bin,usr/lib,usr/share,audio}
cp build/gesture_audio ~/stage/bin/
cp ~/alsa-lib-1.1.3/install/usr/lib/libasound.so.2.0.0 ~/stage/usr/lib/
ln -sf libasound.so.2.0.0 ~/stage/usr/lib/libasound.so.2
cp -a ~/alsa-lib-1.1.3/install/usr/share/alsa ~/stage/usr/share/
make tools && ./build/gen_wav ~/stage/audio/test_44100.wav 8
cd ~/stage && tar czf gapp.tgz .

# 板子上（TF 卡示例）
mkdir -p /tmp/gapp && cd /tmp/gapp
cp /mnt/sd/gapp.tgz . && gzip -d gapp.tgz && tar xf gapp.tar
export LD_LIBRARY_PATH=/tmp/gapp/usr/lib
export ALSA_CONFIG_PATH=/tmp/gapp/usr/share/alsa/alsa.conf
./bin/gesture_audio --selftest audio/test_44100.wav
```

> NFS 参数必须是 `nolock,vers=3`，写成 `nlock` 会挂载失败（`boardGuide.md §2.4` 校注）；
> 但本板当前 PHY 不可用，NFS 只能等换板/修好后再用。

## 2. 测试素材

播放器只吃 **PCM(1) / 16bit / 1~2 声道** 的 WAV（`player.c: wav_parse()` 的硬性限制），
采样率任意（重采样会一并处理）。素材不符合时表现为"没声音"，最难排查，所以先体检。

```bash
# 生成确定性素材：每 2 秒 = 1 秒双音(440+880Hz) + 1 秒静音
./build/gen_wav test_44100.wav 8      # 8 秒，常规用例
./build/gen_wav test_2s.wav 2         # 2 秒，用于快速看 EOF
./build/gen_wav test_30s.wav 30       # 长跑用
```

实测输出（✅）：`已生成 test_44100.wav：44100 Hz / 2 ch / 16 bit / 8.00 s（每 2000 ms：1000 ms 双音 + 1000 ms 静音）`

为什么这么设计：双音段能听出 EQ/回声变化，静音段能看清波形间隙和切歌边界；
时长精确，T3 的 EOF 判定才有基准。

## 3. 用例

### T0 产物静态检查（⏳ 待 ARM 产物；VM 交叉编译后回填）

```bash
readelf -h build/gesture_audio | grep -E "Class:|Machine:"
readelf -l build/gesture_audio | grep interpreter
readelf -d build/gesture_audio | grep NEEDED
readelf --version-info build/gesture_audio | grep -o 'GLIBC_[0-9.]*' | sort -u
# 或一键
make BOARD=1 CROSS=arm-linux- READELF=readelf verify
```

| 检查 | 命令 | 预期（本板实测） |
|---|---|---|
| 二进制架构 | `readelf -h build/gesture_audio \| grep Machine` | `ARM`（x86 说明编错了工具链） |
| 动态链接器 | `readelf -l build/gesture_audio \| grep interpreter` | **`/lib/ld-linux.so.3`**（软浮点；`armhf` 会是 `ld-linux-armhf.so.3`，本板跑不了） |
| 依赖库 | `readelf -d build/gesture_audio \| grep NEEDED` | 含 `libasound.so.2`、`libpthread`、`libm`、`libc` |
| GLIBC 需求 | `readelf --version-info …` | 最大值 ≤ **2.23**（本板 `/lib/libc.so.6 -> libc-2.23.so`） |
| 文件类型 | `file build/gesture_audio` | `ELF 32-bit LSB executable, ARM, EABI5` |

> 本板不要检查 `Tag_ABI_VFP_args`：软浮点对象通常**没有**这个标记，缺失是正常的；
> 真正的判据是解释器路径 `ld-linux.so.3` 与板端一致。

判定：任一项不符 → 工具链/ABI 问题，**不要**上板试。完整流程见 `ubuntuArmBuild.md §6`。

### T1 内置自检（✅ 本机 + 板端均 20/20）

```bash
./gesture_audio --selftest                     # 19 通过 / 0 失败 / 1 跳过
./gesture_audio --selftest audio/test_44100.wav # 20 通过 / 0 失败 / 0 跳过
```

**板端实测（2026-11-11，`/tmp/gapp`）**：`[PASS] 20 WAV 素材体检`、
`结果：20 通过 / 0 失败 / 0 跳过`。串口上显示成 `锛0 ...` 是终端编码问题，
见 `boardGuide.md §2.2.1`：SecureCRT 字符编码要改成 **UTF-8**。

覆盖：参数默认值与 clamp、队列满丢最旧/ FIFO / 超时、指令→参数翻译、协议解析、
回声时序、EQ 旁通与增益、WAV 头体检。**不碰声卡/LCD/传感器**，所以主机也能跑。

判定依据（实测输出，✅）：

```
[PASS] 01 参数默认值 … [PASS] 19 EQ +12dB 使 1kHz 能量明显增大
[PASS] 20 WAV 素材体检
----- 结果：20 通过 / 0 失败 / 0 跳过 -----
结论：逻辑层正常，可继续做硬件验证
```

退出码 = 失败条数。**非 0 就不要继续往下测**，先修代码。

### T2 显示（LCD / 终端）

#### T2.1 主机版式检查（✅ 本机通过）

```bash
./gesture_audio --dump-bmp ui.bmp
```

预期：`已导出界面预览：ui.bmp`；文件 800×480、24bit、`1152054` 字节。
拷回电脑目视核对：顶部标题栏 + 左侧 VOLUME 大数字与横条 + 右侧 PARAMETERS（SPEED/ECHO/EQ×3）
+ 底部 TRK 进度/手势/距离 + 波形区。

✅ 实测：尺寸与字节数一致，版式无重叠，音量数字正常显示。

> 这条能查出"两栏面板互相盖住"这类纯布局问题（历史上曾出现右侧面板压住音量数字）。

#### T2.2 终端仪表行（✅ 本机通过）

```bash
./gesture_audio --run 3 --log info audio/test_44100.wav
```

预期：每 500ms 一行 `[UI]`，字段与 STATUS 一致：

```
[UI] PLAYING ---- vol=65 spd=1.0 echo=off/250ms/40 eq=+0/+0/+0 00:01/00:08 trk=1/1 net=0 und=0 ges=UP dist=317mm
```

#### T2.3 真机 LCD（✅ 2026-11-12 01:04 通过）

```bash
./gesture_audio --sensor sim --log info audio/test_44100.wav
```

**板端实测**：

```
01:04:41 INFO  [display] framebuffer 就绪：800x480 line=3200 map=4608000
01:04:41 INFO  [display] 显示线程启动，刷新间隔 200 ms（framebuffer）
```

判定：`framebuffer 就绪` 行出现、`line=3200`（800×4）、`map=4608000`（800×480×4×?)；
屏幕 5fps 刷新、无撕裂/残影，参数变化后 200ms 内可见。
若日志出现 `framebuffer 不可用，显示线程降级为终端输出`，说明 `/dev/fb0` 打不开
（权限或镜像无 fb），记录该行原文。

### T3 播放与音效

#### T3.1 全链路状态推进（✅ 本机通过）

```bash
./gesture_audio --run 10 --no-display --log info audio/test_44100.wav
```

预期（实测）：

```
[player] 曲目 1/1：audio/test_44100.wav（44100 Hz / 2 ch / 8.0 s）
[main] 达到 --run 10 秒，自动退出
运行结束：抑制手势 N 次，队列丢弃 0 条
```

判定：`曲目 … 打开` 有输出、`队列丢弃 0`、无 `PLAY_ERROR`。

#### T3.2 真机出声（✅ 2026-11-12 01:04 通过，30s und=0）

```bash
./bin/gesture_audio --sensor sim --log info audio/test_44100.wav
# 另一个终端
aplay -l
```

**实测记录（COM4，耳机输出）**：

- 曲目正常打开并推进：`曲目 1/1：audio/test_44100.wav（44100 Hz / 2 ch / 8.0 s）`；
- 耳机可闻双音与静音段；效果器听感可辨；
- 手势链路生效：UP/DOWN 使 `vol` 60→65→70，PULL 使 `spd` 在 0.5/1.9 间切换，
  NEXT/PAUSE/PLAY 在日志中出现并切歌/暂停/恢复；
- 30 秒稳定播放实测：

  ```
  [player] ALSA 设备：plughw:0,0
  [player] ALSA 就绪：rate=44100 period=512 帧(11.6ms) buffer=2050 帧(46.5ms)
  underrun count: 0
  运行结束：抑制手势 0 次，队列丢弃 0 条
  ```

- 结论：`default` 在本板打不开，代码回退到 `plughw:0,0`；**30 秒 0 underrun**。
  之前切换/启动瞬间出现的 `ALSA underrun` 未在稳定播放中复现，暂不改 period。

判定：耳机可闻、`ALSA 就绪` 行存在、30 秒 `underrun = 0`、`队列丢弃 0`、无 `ERROR`。

#### T3.3 EOF 判定（✅ 本机通过）

用 2 秒素材跑 4 秒，在 EOF 前查询状态：

```bash
./build/gen_wav test_2s.wav 2
./gesture_audio --run 4 --no-display --port 8888 test_2s.wav
python tools/netctl.py <板子IP> STATUS
```

预期（实测）：`state=EOF pos=2000 len=2000`，日志出现 `曲目播完（track 1）`。

⚠️ 主机上**不能用 8 秒素材直接等 EOF**：sim 后端每 3 秒来一个手势，其中 RIGHT/LEFT 会切歌重开，
EOF 永远等不到。必须用"素材时长 < 首个切歌手势（3s）"的短素材，或在远程把 `SPEED` 拉高提前播完。

#### T3.4 EOF 后重新播放（✅ 本机通过，D1 已修）

```bash
# 2 秒素材，等 EOF 后发 NEXT
python tools/netctl.py <板子IP> NEXT
python tools/netctl.py <板子IP> STATUS
```

预期：`state=PLAYING` 且 `pos` 从 0 开始增长。

2026-11-11 修复后实测（`tools/player_regress.py` D1 用例）：

```
reached EOF : EOF
NEXT reply  : OK NEXT
[PASS] D1 EOF 后 NEXT 恢复播放  state=PLAYING pos=650
```

修复内容见 §5 D1。

#### T3.5 变速（⚠️ 判据是"时间对"，不是"进度条对"）

```bash
python tools/netctl.py <板子IP> "SPEED 2.0" STATUS
```

预期：`speed=2.00`；8 秒素材约 4 秒播完。

⚠️ `STATUS` 的 `pos/len` 是**源时间**：1.5 倍速时 pos 增长也快 1.5 倍，
8 秒素材在 5.3 秒就 `pos=8000`。看到"进度条跳得快"属正常，不要记为缺陷。

#### T3.6 暂停 / 恢复（✅ 本机通过，D2 已修）

```bash
python tools/netctl.py <板子IP> "PAUSE"
sleep 2
python tools/netctl.py <板子IP> STATUS      # 位置应冻结、状态应为 PAUSED
python tools/netctl.py <板子IP> "RESUME"
```

2026-11-11 修复后实测（`tools/player_regress.py` D2 用例）：

```
before PAUSE: state=PLAYING pos=2321
PAUSE reply : OK PAUSE
[PASS] D2 PAUSE 上报 PAUSED  PAUSED/PAUSED
[PASS] D2 暂停期间 pos 冻结  pos 2321 -> 2321
[PASS] D2 RESUME 恢复 PLAYING  state=PLAYING pos=4364
```

判定：暂停期间 `pos` 不动且 `state=PAUSED`；`RESUME` 后继续推进。修复内容见 §5 D2。

⚠️ 主机版还有"追帧"现象：暂停期间墙钟继续走、空 sink 不补偿，`RESUME` 后会全速播一小段把差补回来
（实测 pos 一次跳 ~1–4 秒）。板上由 ALSA 设备节拍控制，预期不会这样，板端需复测确认。

#### T3.7 回声 / EQ（⏳ 听感，板端）

```bash
python tools/netctl.py <板子IP> "ECHO on 250 40" "EQ mid 6" "EQ low -6" STATUS
```

预期：回声有明显"第二次响"（约 250ms 后）；中频提升后双音更亮，低频衰减后低沉；
`echo=on,250,40`、`eq=-6,6,0`。

> **M5Q-03 主机侧已量化（2026-11-12）**：回声延迟线改为预分配 800ms、改延迟走 20ms
> 交叉淡化（不再 realloc/清零）、反馈路径加 4kHz 一阶低通、wet/dry 默认 0.70/1.00。
> `make audio_quality` 四项 PASS：`echo_damping` −6.3 dB/遍、`echo_delay_switch`
> −25.8 dBFS、`echo_self_osc`（fb=90）末段衰减且零硬削、`echo_wet_mix` 0/14000。
> **板端听感与改延迟复测仍需按本节在 COM3 上执行并补记录。**

### T4 TCP 协议

#### T4.1 指令与回执（✅ 本机通过）

```bash
python tools/netctl.py <板子IP> "HELP" "VOL 70" "VOL 0" "VOL 100" \
        "SPEED 1.5" "SPEED 2.0" "SPEED 2.5" "ECHO on 250 40" "ECHO off 50 0" \
        "EQ low 12" "EQ high -12" "EQ mid 99" "NEXT" "PREV" "STATUS" "BOGUS 1"
```

实测（节选，✅）：

```
[欢迎] OK gesture_audio 0.1.0-m1 已连接，输入 HELP 查看指令
[OK  ] VOL 70        ->  OK VOL 70
[ERR ] SPEED 2.5     ->  ERR SPEED 超出 0.5..2.0
[ERR ] EQ mid 99     ->  ERR EQ 增益超出 -12..12 dB
[OK  ] STATUS        ->  STATUS vol=80 speed=2.00 state=PLAYING track=1/1 pos=… len=…
[ERR ] BOGUS 1       ->  ERR 未知指令 BOGUS
```

判定依据：合法指令回 `OK <原指令>`，越界回 `ERR <中文原因>` 且**不生效**（下一条 STATUS 可见参数未变）。

字段表（`server_format_status()`）：

| 字段 | 含义 |
|---|---|
| `vol` / `speed` | 当前音量 %、倍速 |
| `state` | `STOPPED`/`PLAYING`/`PAUSED`/`EOF`/`ERROR` |
| `track=x/y` | 当前曲目序号/总曲目数（1 基） |
| `pos` / `len` | 源时间毫秒（见 T3.5 说明） |
| `echo=on,delay,fb` / `eq=a,b,c` | 回声与三段 EQ |
| `dist` | 测距 mm（sim 后端 100–400） |
| `gesture` | 最近手势名 |
| `und` | ALSA 欠载次数（板端关注） |
| `clients` / `q` | 在线客户端数 / 指令队列长度 |

#### T4.2 连接上限 4（✅ 本机通过）

```bash
python tools/netctl.py <板子IP> --clients 5
```

预期：前 4 条 `接入成功`，第 5 条被拒；服务端日志 `拒绝连接：已达上限 4`。

实测量化：`实际保持连接 4 条`；第 5 条在 Windows 主机侧表现为"连接被关闭、未收到 ERR 文本"，
属正常（服务端 `send` 后立即 `close`，平台差异）。判定以**服务端日志**为准。

#### T4.3 状态周期推送（✅ 本机通过）

任何已连接客户端每 500ms 收到一行 `STATUS …`（无需请求）。
`tools/netctl.py` 默认只打印回执，加 `--show-status` 才显示推送——避免推送淹没回执。

#### T4.4 QUIT / 超长行

- `QUIT` → 回 `OK BYE` 并断开，`clients` 减 1。
- 单行超过 255 字节 → 回 `ERR 单行超过 255 字节` 并断开（防无换行数据塞满缓冲）。

### T5 手势映射（✅ 本机 sim 全 7 项通过）

```bash
./gesture_audio --run 19 --no-display --log info audio/test_44100.wav
```

sim 后端每 3 秒一个手势，脚本循环 `UP→RIGHT→DOWN→LEFT→PUSH→PULL→HOVER`：

| 时刻 | 手势 | 预期指令 | 实测日志（✅） |
|---|---|---|---|
| 0s | UP | 音量 +5 | `参数 vol=65`（默认 60） |
| 3s | RIGHT | 下一首 | `下一首 → 曲目 1` |
| 6s | DOWN | 音量 -5 | `参数 vol=60` |
| 9s | LEFT | 上一首 | `上一首 → 曲目 1` |
| 12s | PUSH | 播放/暂停切换 | `暂停` |
| 15s | PULL | 速度 +0.2（到 2.0 回到 0.5） | `参数 … spd=1.2` |
| 18s | HOVER | 回声开关 | `参数 … echo=1/250/40` |

测距：三角波 4 秒一个来回，`100 ↔ 400mm`（✅ 实测 102→393mm 平滑变化，无负值）。

真机 I2C（⏳）：`--sensor i2c`，未发现 `/dev/i2c-1` 或传感器时**自动降级 sim** 并 WARN：
`I2C 后端不可用（未发现 /dev/i2c-1 或未接传感器），降级为 sim`（✅ 本机触发过该降级）。
真机验证时要求：手势一次只投递一条指令（按住不连发）、`dist` 连续不跳变。

### T6 并发仲裁

#### T6.1 远程优先窗口（✅ 本机通过）

先发远程指令，3 秒内让 sim 手势到来：

```bash
./gesture_audio --run 8 --no-display --log debug --port 8888 audio/test_44100.wav
# 约 2.4 秒时
python tools/netctl.py <板子IP> "VOL 80"
```

实测（✅）：

```
[net] 参数 vol=80 …            ← 远程生效
DEBUG 手势 5 被抑制（远程优先窗口内，累计 1 次）
运行结束：抑制手势 1 次
```

判定：窗口内手势**不生效**（音量不被改回 75/85），`抑制手势 N 次` 与日志条数一致。

#### T6.2 同源去抖（✅ 本机通过）

```bash
python tools/netctl.py <板子IP> --burst "VOL 80" "VOL 80"
```

`--burst` 把多条指令写进同一个 TCP 段（间隔远小于 `ARB_DUP_GUARD_MS=150ms`）；
逐条发送的间隔通常 >150ms，打不中去抖窗口，必须用 `--burst`。

实测（✅）：

```
[突发] 一次写入 2 条：VOL 80 | VOL 80
[回执] OK VOL 80
[回执] OK VOL 80                 ← 回执都回（回执发生在仲裁之前）
```

服务端日志：

```
[net] 参数 vol=80 …              ← 只生效一次
DEBUG 指令 7 去抖丢弃（0 ms 内重复）
```

判定：参数只变一次，日志出现 `去抖丢弃`。

### T7 长跑（⏳ 板端）

```bash
./gesture_audio --run 300 --port 8888 --log info audio/test_100s.wav
```

30 分钟以上建议跑 300–600 秒，同时用 `netctl --watch 10` 每隔 10 秒观察一次。
关注：内存增长（`top -n 1` 的 RSS）、`und` 不持续增长、`q` 不堆积、无 `PLAY_ERROR`。

✅ 主机 19 秒连续跑（含 7 次手势、多次切歌）无崩溃、`队列丢弃 0`。
⚠️ 主机版时长不可当实时性判据：空 sink 用 `os_sleep_ms` 按墙钟推进，Windows 的 Sleep 粒度约 15ms，
512 帧（11.6ms）会被取整到 15ms，实际比实时慢约 20%。实时性只能在板上看 `und` 与听感。

## 4. 故障对照表

| 现象 | 最可能原因 | 先验证 | 处理 |
|---|---|---|---|
| 执行 `./gesture_audio` 报 `No such file or directory` 但文件存在 | 工具链与板端 loader 不匹配（本板软浮点 `ld-linux.so.3`） | `readelf -l \| grep interpreter` | 换 `arm-linux-`（`arm-none-linux-gnueabi`）重编（T0） |
| `ALSA 打开失败` | 设备被占用 / `/dev/snd` 缺失 | `aplay -l`、`ls /dev/snd` | 杀掉 madplay/Qt，再起；记录日志原文 |
| 有日志无声 | 素材格式不符（非 PCM/16bit/立体声） | `--selftest audio/xxx.wav` 看第 20 条 | 用 `tools/gen_wav` 重新生成 |
| `pos` 不动、state 却 PLAYING | EOF 后切歌（D1，已修） | 复现 T3.4 | 若仍复现，记录版本与日志，按 §5 D1 回归 |
| 屏幕上参数与客户端不一致 | 仲裁把某一条抑制了 | 服务端 `--log debug` 看 `被抑制` | 正常行为；调 `ARB_NET_HOLD_MS` 需走接口评审 |
| `PAUSE` 后 `state` 仍 `PLAYING` | 暂停分支不上报状态（D2，已修） | T3.6 | 见 §5 D2；仍复现则查是否新版本 |
| 坏文件/USB 拔掉后 `state` 一直是 STOPPED | ERROR 未上报（D3，已修） | 给不存在的 WAV 启动 | 见 §5 D3，`ERROR` 应粘滞到 `PLAY` |
| `bind 端口失败` | 上一次进程没退/端口被占 | `ps \| grep gesture` | `killall gesture_audio` 后重启 |
| 客户端收到 `ERR 连接数已满` | 已有 4 个连接 | `netctl --clients 5` 复现 | 正常；先 `QUIT` 掉旧连接 |
| NFS 挂载卡住 | 少了 `nolock,vers=3` | `mount \| grep nfs` | 按 §1.2 重挂 |

## 5. 缺陷与已知限制

### D1（已修 2026-11-11）：EOF 后切歌，播放线程停摆

- **现象**：曲目播完（`state=EOF`）后收到 `NEXT`/`PREV`/左右手势 → 日志显示 `曲目 x/y 打开`、
  `state` 变 `PLAYING`，但 `pos` 永远停在 0，不再产帧（无声、无波形更新）。
- **复现**：2 秒素材跑 9 秒，EOF（约 2s）后发 `NEXT`，随后连续查询 `STATUS`：
  `state=PLAYING pos=0` 保持 5 秒以上。
- **根因**（`src/player.c`）：`player_skip()` 只在 `s_req == PLAY_STOPPED` 时置 `PLAY_PLAYING`；
  EOF 时 `s_req` 仍是 `PLAY_EOF`。`player_thread` 的 `switch (req)` 走 `case PLAY_EOF: sleep; continue`，
  于是只有 `s_reopen` 那一轮切了文件，之后再不生产帧。
- **修复**（两处，缺一不可）：

  ```c
  /* 1) player_skip()：EOF/ERROR 之后切歌也要重新进入播放请求 */
  if (s_req == PLAY_STOPPED || s_req == PLAY_EOF || s_req == PLAY_ERROR)
      s_req = PLAY_PLAYING;

  /* 2) player_thread() 的 reopen 分支：打开成功后按请求态推进，
        不能无条件写 PLAYING，也不能让 req 停在旧值 */
  opened = (req == PLAY_PAUSED) ? PLAY_PAUSED : PLAY_PLAYING;
  player_enter(opened, 1);
  ```

  另外把散落的 `s_state = ...` 收敛到 `player_enter(st, set_req)`：
  写状态加锁、只在变化时发布、终态同时改写 `s_req`。
- **验证**（`tools/player_regress.py`）：

  ```
  reached EOF : EOF
  NEXT reply  : OK NEXT
  [PASS] D1 EOF 后 NEXT 恢复播放  state=PLAYING pos=650
  ```
- **影响面**：T3.4、T5（EOF 后的 LEFT/切线路径）、T7 长时间循环播放。

### D2（已修 2026-11-11）：暂停后 `state` 不上报 PAUSED

- **现象**：`PAUSE` 后位置确实冻结（暂停生效），但 `STATUS`/界面里的 `state` 一直是 `PLAYING`，
  客户端与 LCD 都无法显示"已暂停"。
- **根因**（`src/player.c`）：`player_thread` 的 `case PLAY_PAUSED:` 只置 `s_state = PLAY_PAUSED`
  就 `continue`，**没有调用 `publish_status()`**；状态快照只在 reopen/EOF/ERROR/播放循环每 8 个周期时发布，
  暂停分支被跳过了。
- **修复**：暂停分支改走 `player_enter(PLAY_PAUSED, 0)`——状态变化时发布一次快照，
  每 20ms 的循环不会重复刷共享内存。
- **验证**：

  ```
  [PASS] D2 PAUSE 上报 PAUSED  PAUSED/PAUSED
  [PASS] D2 暂停期间 pos 冻结  pos 2321 -> 2321
  [PASS] D2 RESUME 恢复 PLAYING  state=PLAYING pos=4364
  ```
- **影响面**：T3.6、T2（LCD 暂停态）、所有远程客户端的 `state` 字段。

### D3（已修 2026-11-11）：坏文件/缺素材时 `ERROR` 上报不出来

- **现象**：启动时给一个不存在的 WAV，`STATUS` 一直显示 `state=STOPPED`；
  即使 `PLAY` 也等不到 `ERROR`，现场无法区分"没加载"与"加载失败"。
- **根因**（`src/player.c`）：`!s_wav_open` 的打开失败分支把 `s_req` 改成 `PLAY_STOPPED` 且
  未发布快照；下一轮 `case PLAY_STOPPED` 立即把 `s_state` 覆盖成 `PLAY_STOPPED`，
  `ERROR` 只存在一瞬。
- **修复**：
  1. 所有打开失败路径统一 `player_enter(PLAY_ERROR, 1)`（改状态 + 改请求 + 立即发布）；
  2. 主循环对 `ERROR` 做粘滞：只有 `PLAY`（`req == PLAYING`）能复位，
     `PAUSE`/`NEXT` 等不会把错误态"洗掉"；
  3. `NEXT` 切到另一首可用曲目时经 reopen 分支恢复正常。
- **验证**：

  ```
  [PASS] D3 坏文件上报 ERROR  state=ERROR
  [PASS] D3 ERROR 粘滞（PLAY 仍 ERROR）  state=ERROR
  [PASS] D3 文件就位后 PLAY 复位  state=PLAYING pos=650
  ```
- **影响面**：T3.1 的"无 `PLAY_ERROR`"判据、客户端错误提示、现场"没声音"的归因速度。

### O1–O6（观察项，不是缺陷）

- **O1 变速后进度按源时间走**：见 T3.5，现场别误判。
- **O2 被拒连接可能收不到 ERR 文本**：服务端 `send` 后立即 `close`，Windows 客户端只见连接关闭；
  以服务端 `拒绝连接` 日志为准（Linux 板端通常能收到文本，实测后回填）。
- **O3 sim 后端每 3 秒一个手势**：会周期性切歌/暂停，**任何"跑多久才 EOF"的用例都要考虑它**（见 T3.3）。
- **O4 主机空 sink 比实时慢约 20%**：`os_sleep_ms` 取整所致，只影响主机计时，不影响板上。
- **O5 I2C 后端当前是占位**：只探测 `/dev/i2c-1`，M2 才填读寄存器逻辑；现在一律降级 sim。
- **O6 主机版暂停后追帧**：见 T3.6；暂停期间墙钟照走，`RESUME` 后空 sink 全速补差，
  主机日志里的 pos 会出现一次跳变，**板上不复现**（ALSA 设备节拍）。

## 6. 现场纪律（摘自 `devFlow.md §3`）

1. 上板前在工作群报备占用时段（默认 40 分钟），冲突找组长。
2. 串口同一时间只许一人连接；能用 telnet/ssh 就不用串口。
3. 集成机 tmux 会话名 `board`，不私开独占会话。
4. 离开必做：杀掉自己跑的程序、恢复板子网络配置、群里报"板子空闲"。
5. 板子上跑的程序必须能在 GitHub 找到对应提交（热修当天补提交）。

## 7. 一页速查

```bash
# 0) 上板前（集成机）
make BOARD=1 CROSS=arm-linux-
readelf -h build/gesture_audio | grep Machine          # 要 ARM

# 1) 自检（板子或主机都行）
./gesture_audio --selftest audio/test_44100.wav        # 要 20 通过 0 失败

# 2) 显示
./gesture_audio --dump-bmp ui.bmp                      # 800x480 24bit

# 3) 播放 + 状态
./gesture_audio --sensor sim --log info audio/test_44100.wav
python tools/netctl.py <板子IP> STATUS

# 4) 协议矩阵
python tools/netctl.py <板子IP> "VOL 70" "SPEED 1.5" "ECHO on 250 40" "EQ mid -3" STATUS
python tools/netctl.py <板子IP> --clients 5            # 第 5 条应被拒

# 5) 仲裁（远程后 3 秒内看手势是否被抑制）
./gesture_audio --run 8 --no-display --log debug --port 8888 audio/test_44100.wav
python tools/netctl.py <板子IP> "VOL 80"               # 2.4 秒时发

# 6) 长跑
./gesture_audio --run 300 --port 8888 --log info audio/test_30s.wav
```

## 附录 A：Windows 开发机（无 sh.exe / 无 gcc）编译命令

```powershell
# 主机版（BOARD=0）——有 MinGW gcc 时
gcc -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude -DBOARD=0 -D_GNU_SOURCE -pthread `
    src/*.c -o build/gesture_audio.exe -pthread -lm -lws2_32

# 素材生成器
gcc -O2 -Wall -Wextra -o build/gen_wav.exe tools/gen_wav.c -lm
```

2026-11-11 本机没有 gcc/MinGW、也不能装 WSL，改用**便携 Zig**（`py -3.11 -m pip install ziglang`，
不写注册表、不动系统 PATH）做主机回归，等价命令：

```powershell
$zig = (py -3.11 -c "import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),'zig.exe'))")
$src = (Get-ChildItem src\*.c).FullName
& $zig cc -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -DBOARD=0 -D_GNU_SOURCE -pthread `
    $src -o build\gesture_audio.exe -lws2_32 -lm -lpthread
```

（两条路都实测零警告；`BOARD=1` 在本机编不了，缺 ALSA 头与交叉编译器——那一步在 Ubuntu VM 做。）

## 附录 B：测试记录表（复制到现场记录）

| 用例 | 时间 | 结果(通过/失败/跳过) | 实测值或关键日志 | 备注 |
|---|---|---|---|---|
| T0 产物静态检查 | | | | |
| T1 自检 | | | __通过/失败/跳过__ | |
| T2.1 版式 BMP | | | 字节数 | |
| T2.3 真机 LCD | | | | |
| T3.1 全链路 | | | 丢弃数 | |
| T3.2 真机出声 | | | und | |
| T3.3 EOF | | | pos/len | |
| T3.4 EOF 后切歌 | | | | D1 已修，主机回归通过 |
| T3.5 变速 | | | speed | |
| T3.6 暂停/恢复 | | | pos 是否冻结 | D2 已修，主机回归通过 |
| T3.8 坏文件 ERROR（新增） | | | state 是否粘滞 | D3 已修，主机回归通过 |
| T3.7 回声/EQ | | | | |
| T4.1 协议矩阵 | | | ERR 条数 | |
| T4.2 连接上限 | | | 保持条数 | |
| T5 手势映射 | | | 7 项 | |
| T6.1 远程优先 | | | 抑制次数 | |
| T6.2 去抖 | | | | |
| T7 长跑 | | | RSS/und | |
