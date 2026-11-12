# 板端环境快照（boardSnapshot.md）

> 采集：2026-11-11 23:00 前后 · 通道：Windows SecureCRT → COM4（115200 8N1）· 身份：`root@GEC6818`
> 用途：给 `testGuide.md` / `boardGuide.md` 提供**本板实测值**，替代早期推断。
> 纪律：只记录实际看到的输出；未执行的项目标 ⏳，不填猜测值。

## 1. 系统与 ABI

| 项 | 实测命令 | 实测值 | 判定 |
|---|---|---|---|
| 动态 loader | `ls -l /lib/ld-linux*` | `/lib/ld-linux.so.3 -> ld-2.23.so` | **软浮点** |
| libc | `ls -l /lib/libc.so.6` | `libc-2.23.so` | glibc **2.23** |
| glibc 版本 | `/lib/libc.so.6 \| head -n1` | `GNU C Library ... release version 2.23` | ✅ |
| 内核 | `uname -a` | ⏳ 未采集 | `boardGuide.md` 记为 3.4.39-gec，待复核 |
| 用户态位数 | —（loader 为 32 位） | 32-bit ARM | ✅ |

**结论**：本板是 **ARM 32 位 / 软浮点 ABI / glibc 2.23**；VM 里的
`arm-linux-gcc`（`arm-none-linux-gnueabi`，GCC 5.4.0）与之匹配。
用 `gnueabihf` 编出的程序会要求 `/lib/ld-linux-armhf.so.3`，本板跑不了。

## 2. 显示 / 音频 / 输入

| 项 | 实测 | 说明 |
|---|---|---|
| framebuffer | `/dev/fb0` 存在（`crw-rw---- 29,0`） | LCD 可尝试输出 |
| 声卡设备节点 | `/dev/snd/{controlC0,pcmC0D0c,pcmC0D0p,timer}` | 内核 ALSA 正常 |
| 声卡列表 | `/proc/asound/cards`：card0 `I2S-s5p`，card1 `SPDIF` | ✅ |
| ALSA 用户库 | `find / -xdev -name 'libasound*'` 为空 | **板端没有 libasound**，需自带（`boardGuide.md §3.3.1`） |
| 触摸/输入 | ⏳ 未采集 | 待 `cat /proc/bus/input/devices` |

## 3. 网络

| 项 | 实测 | 判定 |
|---|---|---|
| 网口 | 仅 `eth0`（另有 `lo` / `ip6tnl0` / `sit0`） | — |
| 链路 | `operstate=down`、`carrier=0` | 未连通 |
| PHY | `dmesg`：`eth0: PHY ID 00000000 at 0..31` | **32 个地址全 0，PHY 未识别** |
| 打开接口 | `stmmac_open: Cannot attach to PHY (error: -19)` | 失败 |
| IP | `ifconfig eth0 10.20.90.100 ...` 能写进，但 `ping` 报 `Network is unreachable` | 无效配置 |
| NFS | `mount \| grep nfs` 为空；`/mnt/nfs` 未挂载 | 不可用 |

**结论**：本板以太网当前不可用（硬件/PHY 或镜像 PHY 配置问题），
**NFS/scp 部署路径暂时作废**；改用 TF 卡 / U 盘 / 串口 Zmodem（`ubuntuArmBuild.md §7`）。

## 4. 部署可用性

| 通道 | 状态 |
|---|---|
| NFS / scp | ❌（eth0 PHY 不识别） |
| TF 卡 | ✅ 待用（`/dev/mmcblk*`，需确认卡槽） |
| U 盘 | ✅ 待用（`/dev/sd*`） |
| 串口 Zmodem | 取决于板端是否有 `rz`：⏳ `which rz sz` 待执行 |
| rootfs `/root` | ✅ **可写**（2026-11-12 实测 `ROOT_RW=yes`）→ 持久版可放 `/root/gapp` |

## 6. 首版上板运行（2026-11-11 23:58）

| 项 | 实测 |
|---|---|
| 部署 | 串口 `rx/rb` 传 `gapp.tgz` 到 `/tmp/gapp`（无网络） |
| 运行 | `./bin/gesture_audio --sensor sim --log info audio/test_44100.wav` |
| 音频 | **耳机可闻**双音/静音段，效果器听感可辨；`ALSA 设备：plughw:0,0`，`rate=44100 period=512 buffer=2050` |
| 显示 | `framebuffer 就绪：800x480 line=3200 map=4608000`，显示线程 200ms 刷新 |
| 手势 | UP/DOWN 改音量（60→65→70）、PULL 变速（0.5/1.9）、NEXT/PAUSE/PLAY 生效 |
| 状态 | 30 秒稳定播放 **`underrun = 0`**、`队列丢弃 0 条` |
| 自检 | **板端 `--selftest` 20 通过 / 0 失败 / 0 跳过**（2026-11-11 23:4x） |
| 本地 TCP | `nc`、`telnet` 均在 `/bin`，可做 `127.0.0.1:8888` 回环测试（T4/T6 不再完全阻塞） |
| demo 播放 | `demo.wav` 48k/2ch/93.8s 解析正常；`default` 因配置树不在 `/usr/share/alsa` 失败，`plughw:0,0` 出声；sim 每 3s 切歌/变速/暂停，整首会被打断 |
| `/tmp` | tmpfs 404MB（已用 18.4MB，可用 385.5MB），放 17MB demo 没问题；**重启即清空，`/tmp/gapp` 会消失** |
| 串口传输 | XMODEM 实测约 **5 KB/s**；17.1MB `demo.wav` 实测 **53 分钟**；30s 单声道约 5–8 分钟 |
| 待补 | D1–D3 板端回归、T3.3/T3.4 的 EOF 实测 |

> 结论：M1 的“真机出声 + LCD + 自检”三项均已通过；`plughw:0,0` 是实际可用设备，
> 30 秒 0 underrun，暂不需要调 period。剩下的是 D1–D3 板端回归与 EOF 用例，
> 以及 T0 的 `readelf` 原始输出归档。

## 7. 待补采集（下一次上板）

```bash
uname -a
free
cat /proc/cmdline
cat /proc/bus/input/devices
ls -l /sys/class/leds/ /dev/i2c-* 2>/dev/null
which rx rb rz sz wget telnet base64
ls /dev/mmcblk* /dev/sd*
```

> 换板、换镜像、换网线后，§3 的网络结论要重测，不要照抄。

## 8. rootfs 持久化（2026-11-12 22:0x）

| 项 | 实测/命令 | 结果 | 判定 |
|---|---|---|---|
| `/root` 可写 | `touch /root/.wtest && echo ROOT_RW=yes` | `ROOT_RW=yes` | ✅ 有写权限（非只读 rootfs） |
| `/tmp/gapp` | 运行目录 | 存在 | ✅ tmpfs，重启丢失 |
| 持久版 `/root/gapp` | 本次固化 | ⏳ 待回填 `ls -l /root/gapp/bin` | 复制流程见 `ubuntuArmBuild.md §7.3.1` |
| `/root/gapp.tgz` 快照 | `sha256sum` / `ls -lh` | ⏳ 待回填 | 回滚与重刷用 |
| rootfs 剩余空间 | `df -h /root` | ⏳ 待回填 | 决定 17 MB 素材是否留 `/root` |

**结论**：本板 rootfs 可写，`/tmp/gapp` 可直接固化为 `/root/gapp`，重启后不必再占串口重传；
日常覆盖只需传 strip+gzip 的二进制（`ubuntuArmBuild.md §7.3.1`）。
⚠️ 重刷镜像会清空 `/root`，跨重刷要另留一份在 TF 卡；权威版本仍是 git 提交。
