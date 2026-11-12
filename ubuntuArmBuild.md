# 导入 Ubuntu 做 ARM 交叉编译 —— 实施方案（V1.1）

> 适用：GEC6818 开发板（Cortex-A53 / ARMv7 32 位 / **软浮点**）+ Ubuntu 18.04 虚拟机
> 目标：把 Windows 侧工作区原样导入 VM，一条命令产出**能上板运行**的 `gesture_audio`
> 关联：`README.md`「编译」、`boardGuide.md §2.4/§2.5/§3.3`、`testGuide.md` T0/T1、`devFlow.md §4`
> 当前代码基线：`0.1.0-m1`（D1/D2/D3 已修，主机回归 20/20）
> 维护：环境 owner；改动构建参数时同步更新本文与 `Makefile`

> **实测修正（2026-11-11，COM4 板端）**：本板 `/lib/ld-linux.so.3 -> ld-2.23.so`、
> glibc **2.23**，是**软浮点 ABI**；VM 里的 `/usr/local/arm/5.4.0/usr/bin/arm-linux-gcc`
> （`-dumpmachine` = `arm-none-linux-gnueabi`）正好匹配。旧版“必须 gnueabihf”的结论作废。
> 另外板端**没有 libasound**，音频用户库要自己交叉编（§4.3.1）。

---

## 0. 结论先行（先看这 6 条）

1. **工具链以板端 loader 为准**：本板是 `/lib/ld-linux.so.3`（软浮点），用
   `arm-linux-gcc`（`arm-none-linux-gnueabi`，厂商 5.4.0）。若误用 `gnueabihf`，
   产物会要求 `/lib/ld-linux-armhf.so.3`，板上表现为“文件存在却报
   `No such file or directory`”。
2. **首选厂商 GCC 5.4.0**（`/usr/local/arm/5.4.0`）。Ubuntu 18.04 自带的
   `gcc-arm-linux-gnueabihf`（7.x）链接 glibc 2.27，而本板是 2.23，会报
   `GLIBC_2.27 not found`；它只能用来做语法检查。
3. **本板缺的不只是 ALSA 头，还缺 `libasound.so.2`**：内核 `/dev/snd` 正常，但用户库
   为空。需要按 §4.3.1 用同一套工具链交叉编 alsa-lib（或使用软浮点预编译库）。
4. **导入通道**：本机 VM 的共享文件夹当前是**关闭**的（`.vmx` 里
   `isolation.tools.hgfs.disable="TRUE"`、`sharedFolder0.present="FALSE"`）。
   最省事的是 §2 的 **HTTP 传输**或 `scp`；要用共享文件夹得先在 VMware 设置里打开。
5. **编译只跑一条**：`bash tools/armbuild.sh`（会自动挑 `arm-linux-` 前缀；内部执行
   `make clean && make BOARD=1 && make verify`），失败会直接告诉你缺哪一项。
6. **验收看三样**：`readelf` 显示 ARM + **`ld-linux.so.3`** + NEEDED 含
   `libasound.so.2`；板上 `--selftest` 20/20；真机出声且 `und=0`。

---

## 1. 一页流程

```
 Windows（本机）                         Ubuntu 18.04（VM）                     GEC6818
 ───────────────                         ──────────────────                     ───────
 tools\pack_for_ubuntu.ps1
   └─ dist\…src-<时间>.tar.gz
        │  §2 传输（HTTP / scp / 共享目录 / U 盘）
        ▼
                                    §3 解包 + 装依赖
                                    §4 选工具链 + ALSA 头
                                    §5 bash tools/armbuild.sh
                                          └─ build/gesture_audio (ARM)
                                    §6 make verify（T0）
                                          │  §7 NFS / scp / Zmodem
                                          ▼
                                                                        §8 T0–T3 首跑
                                                                        --selftest 20/20
                                                                        真机出声 und=0
```

---

## 2. 从 Windows 导入（打包 + 传输）

### 2.1 打包（Windows，仓库根目录）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1
```

产物：

```
dist\gec6818-gesture-audio-src-<yyyyMMdd-HHmmss>.tar.gz
dist\...tar.gz.sha256
```

包内只含源码/文档/脚本，**不含** `.git`、`build/`、`dist/`、`*.exe`、`*.o`、`*.wav`，
约 0.1–0.2 MB。要换输出目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1 -OutDir D:\share
```

### 2.2 传到 VM（四选一）

| 方式 | 前提 | 命令 |
|---|---|---|
| **A. HTTP（推荐，零配置）** | VM 能访问宿主机 VMnet8 地址 | 见下 |
| B. scp | VM 装了 `openssh-server`，网络互通 | `scp dist\*.tar.gz <用户>@<VM_IP>:~/` |
| C. VMware 共享文件夹 | 需先在 VM 设置里启用（当前是关的） | 见下 |
| D. U 盘 / ISO | 物理可达 | 拷到 U 盘再挂载 |

**A. HTTP**

Windows 侧（在 `dist` 目录起一个只读文件服务）：

```powershell
py -3.11 -m http.server 8000 --directory D:\gec6818-gesture-audio\dist
```

查宿主机地址：`ipconfig`，找 **VMnet8** 的 IPv4（NAT 模式下 VM 能访问它）。
VM 内：

```bash
cd ~
wget http://<宿主机VMnet8地址>:8000/gec6818-gesture-audio-src-<时间戳>.tar.gz
sha256sum -c <包名>.sha256    # 需要先把 .sha256 也 wget 下来
```

**C. 共享文件夹**（可选）

VMware Workstation → 虚拟机设置 → 选项 → 共享文件夹 → 选择“总是启用” → 添加
`D:\share`，名称 `share`。VM 内：

```bash
vmware-hgfsclient                  # 应看到 share
sudo mkdir -p /mnt/hgfs
sudo mount -t vmhgfs .host:/ /mnt/hgfs   # 或 vmhgfs-fuse
```

> 当前 `D:\虚拟机\环境\虚拟机\Ubuntu18.04.4\Ubuntu18.04.4-x64.vmx` 里
> `isolation.tools.hgfs.disable = "TRUE"`、`sharedFolder0.present = "FALSE"`、
> `sharedFolder.maxNum = "0"`，直接挂会失败。先改设置，不要改 `.vmx` 文本。

### 2.3 VM 内解包

```bash
mkdir -p ~/gec6818
tar -xzf ~/gec6818-gesture-audio-src-<时间戳>.tar.gz -C ~/gec6818
cd ~/gec6818
ls    # 应看到 Makefile src include tools README.md ubuntuArmBuild.md …
```

> 包内路径是 `./` 开头（`tar -C 目标 .` 打的），解包到 `~/gec6818` 即可。
> 所有文本文件是 LF；如果你的编辑器把 `tools/armbuild.sh` 存成了 CRLF，
> bash 会报 `$'\r': command not found`，用 `sed -i 's/\r$//' tools/armbuild.sh` 修。

---

## 3. VM 基础环境

```bash
sudo apt update
sudo apt install -y build-essential file binutils python3
```

- `build-essential`：`make` + 宿主 gcc，用于 `BOARD=0` 主机回归（可选但强烈建议）；
- `file` / `binutils`：`file`、`readelf`，T0 校验必需；
- `python3`：`tools/netctl.py`、`tools/player_regress.py`。

先跑一遍**主机版**确认源码没搬坏（不需要板子）：

```bash
cd ~/gec6818
make clean && make
./build/gesture_audio --selftest
# 期望：19 通过 / 0 失败 / 1 跳过（第 20 条需要 WAV 参数）
python3 tools/player_regress.py --exe build/gesture_audio
# 期望：0 case(s) failed
```

### 3.1 常见拦路：VM 没网 / `apt update` 报“无法解析域名”

`无法解析域名` 说明**链路可能通、只是 DNS 坏了**，也可能根本没拿到 IP。先分三层查，不要直接改源：

```bash
ip addr show            # 看 ens33（或 ens33/ens160）有没有 192.168.x.x
ip route                # 看有没有 default via 192.168.x.1
cat /etc/resolv.conf    # 看 nameserver 是什么
ping -c 2 223.5.5.5     # 直接 ping IP，绕开 DNS
```

| 现象 | 判断 | 处理 |
|---|---|---|
| 网卡没有 `inet` 地址 | DHCP 没拿到租约 | `sudo ip link set ens33 up` → `sudo dhclient -v ens33` |
| 有 IP、但没有 `default` 路由 | 网关缺失 | 同上；仍无则查 VMware NAT/DHCP 服务 |
| `ping 223.5.5.5` 通、`ping mirrors.aliyun.com` 不通 | 纯 DNS 问题 | 见下方“DNS 修法” |
| `ping 223.5.5.5` 也不通 | NAT/桥接链路断 | 查 VM 网络适配器是否为 NAT 且“启动时连接” |

**DNS 修法（临时，立即能让 apt 跑起来）**

```bash
sudo sh -c 'printf "nameserver 223.5.5.5\nnameserver 114.114.114.114\n" > /etc/resolv.conf'
getent hosts mirrors.aliyun.com     # 有输出即解析恢复
sudo apt update
```

**DNS 修法（永久，netplan；Ubuntu 18.04 默认）**

```bash
ls /etc/netplan/                    # 找到实际的 yaml，如 01-netcfg.yaml
sudo nano /etc/netplan/01-netcfg.yaml
```

```yaml
network:
  version: 2
  ethernets:
    ens33:
      dhcp4: true
      nameservers:
        addresses: [223.5.5.5, 114.114.114.114]
```

```bash
sudo netplan apply
```

若系统用 `systemd-resolved`，也可以先试：

```bash
sudo systemctl restart systemd-resolved
resolvectl status | head -20
```

**VMware 侧检查（Windows 宿主机）**

- 虚拟机设置 → 网络适配器 → **NAT**，勾选“启动时连接”；
- 服务 `VMware NAT Service`、`VMware DHCP Service` 必须是 Running；
- 还不行：虚拟网络编辑器 → VMnet8 → “还原默认设置”（会重置 192.168.x 网段，VM 里要重新 `dhclient`）。

> 本机 2026-11-11 实测：VMnet8 = `192.168.235.1`，两个 VMware 服务均 Running，VM 配置为 `ethernet0.connectionType = "nat"`。
> 正常时 VM 应拿到 `192.168.235.x`；若拿不到，问题在 VM 内部（网卡未启用/未 DHCP）或虚拟网络编辑器被改过。

> ⚠️ **NAT 只适合 apt / 临时上网**。后面板子要通过 NFS 访问 VM，VM 必须改成**桥接**并与板子同网段；
> 否则现象是“板子 ping 不通 VM、NFS 挂载超时”。

#### 没网也要继续：先查工具链是否已预装

﻿很多课程镜像已经带好厂商工具链，**apt 失败不代表不能编译**：

```bash
which arm-linux-gcc make gcc
arm-linux-gcc -dumpmachine
arm-linux-gcc -print-sysroot
```

只要 `${CROSS}gcc` 存在且 `-dumpmachine` 与板端 loader 匹配，就可以直接 `bash tools/armbuild.sh`。
缺的 `.deb` 可从宿主机下载后经共享目录/U 盘拷入，用 `sudo dpkg -i *.deb` 离线安装。

---

## 4. 交叉工具链与 ALSA

### 4.1 先查：VM 里到底有什么

```bash
which arm-linux-gcc arm-linux-gnueabihf-gcc 2>/dev/null
arm-linux-gcc -dumpmachine 2>/dev/null
arm-linux-gcc -print-sysroot 2>/dev/null
```

本机 VM 实测：

```
/usr/local/arm/5.4.0/usr/bin/arm-linux-gcc
arm-none-linux-gnueabi
/usr/local/arm/5.4.0/usr/arm-none-linux-gnueabi/sysroot
```

判定（**以板端 loader 为准**）：

| 板端 `/lib/ld-linux*` | 工具链要求 |
|---|---|
| `ld-linux.so.3`（本板实测） | `arm-none-linux-gnueabi` / `arm-linux-gcc`（软浮点） |
| `ld-linux-armhf.so.3` | `arm-linux-gnueabihf-gcc`（硬浮点） |
| 命令不存在 | 按 §4.2 装/加 PATH |

> `arm-none-linux-gnueabi` 不是“错的”：本板实测就是软浮点。不要被旧文档的
> “必须 gnueabihf”误导——两者 ABI 不兼容，混用会报 `No such file or directory`。

### 4.2 工具链选择（按优先级）

**A. 厂商 GCC 5.4.0（首选，本机 VM 已装）**

```bash
export PATH=$PATH:/usr/local/arm/5.4.0/usr/bin
arm-linux-gcc -v
```

本板实测 `-dumpmachine` = `arm-none-linux-gnueabi`，与 `/lib/ld-linux.so.3` 匹配。
编译前缀用 `arm-linux-`：

```bash
bash tools/armbuild.sh arm-linux-
```

**B. Ubuntu 18.04 自带（只做语法/构建检查）**

```bash
sudo apt install -y gcc-arm-linux-gnueabihf
```

7.x、链接 glibc 2.27；本板是 2.23，产物会报 `GLIBC_2.27 not found`，
**只能做编译检查，不要上板**。

**C. 其它旧工具链（≤ 6.x）**

只要 `-dumpmachine` 与板端 loader 匹配、sysroot glibc ≤ 2.23，都可以；
用 `bash tools/armbuild.sh <前缀>` 指定即可。

> 不要用 gcc 9/10/11+ 直接产板端包：新 glibc 符号版本会高于板端 rootfs。

### 4.3 音频依赖：本板缺的是 libasound，不只是头

#### 4.3.1 预检

```bash
CROSS=arm-linux-
SYS=$(${CROSS}gcc -print-sysroot)
ls "$SYS/usr/include/alsa/asoundlib.h" 2>/dev/null
find /usr/local/arm/5.4.0 -name 'libasound*' 2>/dev/null
```

本机实测：**头文件和库都没有**。板端再确认一次：

```bash
find / -xdev -name 'libasound*' 2>/dev/null   # 本板为空
ls /dev/snd/                                  # 本板有 controlC0/pcmC0D0p
```

#### 4.3.2 交叉编 alsa-lib（推荐，ABI 最匹配）

```bash
tar xf alsa-lib-1.1.3.tar.bz2 -C ~ && cd ~/alsa-lib-1.1.3
CC=arm-linux-gcc AR=arm-linux-ar RANLIB=arm-linux-ranlib \
  ./configure --host=arm-none-linux-gnueabi --prefix=/usr \
              --disable-static --disable-python
make -j2
make install DESTDIR=$PWD/install
find install -name 'libasound.so*'
```

产出：`install/usr/lib/libasound.so.2.0.0`、`install/usr/include/alsa/`。
如果工具链没有 `arm-linux-ar`，先 `ls /usr/local/arm/5.4.0/usr/bin/arm-linux-*`
看实际工具名，再把 `AR/RANLIB` 换成对应的。

#### 4.3.3 备选：软浮点预编译库（快）

Debian **armel**（不是 armhf）的 `libasound2` 是软浮点，glibc 2.19 构建，
可在本板 2.23 上运行：

```bash
dpkg -x libasound2_1.0.28-1_armel.deb /tmp/alsa-arm
find /tmp/alsa-arm -name 'libasound.so*'
```

#### 4.3.4 编译时指库

```bash
make BOARD=1 CROSS=arm-linux- READELF=readelf \
  ALSA_CFLAGS="-I$HOME/alsa-lib-1.1.3/install/usr/include" \
  ALSA_LIBS="-L$HOME/alsa-lib-1.1.3/install/usr/lib -l:libasound.so.2"
```

#### 4.3.5 部署：库 + 配置树都要带

`libasound` 打开 `default` 设备时依赖 `/usr/share/alsa/alsa.conf` 及其 include 文件；
板端没有这个目录，**只拷 .so 会在 `snd_pcm_open` 处失败**。建议整包 staging：

```bash
mkdir -p ~/stage/{bin,usr/lib,usr/share,audio}
cp build/gesture_audio ~/stage/bin/
cp ~/alsa-lib-1.1.3/install/usr/lib/libasound.so.2.0.0 ~/stage/usr/lib/
ln -sf libasound.so.2.0.0 ~/stage/usr/lib/libasound.so.2
cp -a ~/alsa-lib-1.1.3/install/usr/share/alsa ~/stage/usr/share/
make tools && ./build/gen_wav ~/stage/audio/test_44100.wav 8
cd ~/stage && tar czf gapp.tgz .
```

板端（根文件系统只读时也可用）：

```bash
mkdir -p /tmp/gapp && cd /tmp/gapp
wget http://<VM_IP>:8000/gapp.tgz && tar xzf gapp.tgz
export LD_LIBRARY_PATH=/tmp/gapp/usr/lib
export ALSA_CONFIG_PATH=/tmp/gapp/usr/share/alsa/alsa.conf
/tmp/gapp/bin/gesture_audio --selftest /tmp/gapp/audio/test_44100.wav
/tmp/gapp/bin/gesture_audio --sensor sim --log info /tmp/gapp/audio/test_44100.wav
```

如果能写根文件系统，也可以把 `usr/lib`、`usr/share/alsa` 直接铺到 `/`，就不需要两个环境变量。

> **不要**用 Ubuntu 的 armhf `libasound`：那是硬浮点 ABI，本板软浮点加载不了。
> 如果 `default` 仍打不开，先看 `ALSA_CONFIG_PATH` 是否指对，再试 `hw:0,0`。

**D. 多架构 apt（不推荐）**

`dpkg --add-architecture armhf` 会引入硬浮点库，与本板 ABI 不符；
且容易和宿主 libc 打架。除非你明确知道在做什么，否则用 4.3.2。

### 4.4 板上确认三件事（已实测，可复核）

```bash
ls -l /lib/ld-linux* /lib/libc.so.6
/lib/libc.so.6 | head -n1
find / -xdev -name 'libasound*' 2>/dev/null
cat /proc/asound/cards
```

本板 2026-11-11 实测：

| 项 | 值 |
|---|---|
| loader | `/lib/ld-linux.so.3 -> ld-2.23.so`（**软浮点**） |
| libc | `libc-2.23.so`（glibc 2.23） |
| libasound | **无**（`/dev/snd` 节点正常） |
| 声卡 | card0 `I2S-s5p` / card1 `SPDIF` |
| eth0 | UP，但无 IP；`/mnt/nfs` 未挂载 |

把这几行记进 `testGuide.md` 的环境表；换板/换镜像时先重测，不要照抄。

---

## 5. 编译

### 5.1 推荐：一条命令

```bash
cd ~/gec6818
bash tools/armbuild.sh
# 或显式指定厂商前缀
bash tools/armbuild.sh /usr/local/arm/5.4.0/usr/bin/arm-linux-
```

脚本会依次做：

1. 检查 `${CROSS}gcc`、`-dumpmachine`、sysroot；
2. 预检 `alsa/asoundlib.h`；
3. `make clean` + `make BOARD=1 CROSS=…`（日志同时写 `/tmp/gesture_audio_build.log`）；
4. `make verify`：打印架构、解释器、NEEDED、GLIBC 需求；
5. 最后再硬检查一次 `Machine: ARM`，并给出上板命令。

### 5.2 手工等价命令

```bash
make clean
make BOARD=1 CROSS=arm-linux- READELF=readelf
make BOARD=1 CROSS=arm-linux- READELF=readelf verify
```

产物：`build/gesture_audio`（ELF 32-bit ARM，软浮点）。

### 5.3 构建参数说明

| 参数 | 作用 |
|---|---|
| `BOARD=1` | 编板端代码：ALSA 输出、framebuffer 显示；链接 `-lasound` |
| `BOARD=0`（默认） | 主机模拟：空 sink + 终端仪表行；无硬件依赖，用于回归 |
| `CROSS=` | 工具链前缀；本板用 `arm-linux-`，空 = 宿主 `cc` |
| `ALSA_CFLAGS` | 追加 ALSA 头路径（§4.3.4） |
| `ALSA_LIBS` | 覆盖 `-lasound`（§4.3.4） |
| `READELF=` | 工具链没有 `arm-linux-readelf` 时用 `READELF=readelf` |

> 优化级别固定 `-O2`。厂商 5.4.0 的默认目标是 ARMv7 软浮点；
> 要压 EQ 的 CPU 再加 `-march=armv7-a -mfpu=neon` 前，先确认整条链接链的
> ABI 仍是软浮点，且必须 `make clean` 全量重编。

---

## 6. 产物校验（T0，必须全过再上板）

```bash
readelf -h build/gesture_audio | grep -E "Class:|Machine:"
readelf -l build/gesture_audio | grep interpreter
readelf -d build/gesture_audio | grep NEEDED
readelf --version-info build/gesture_audio | grep -o 'GLIBC_[0-9.]*' | sort -u
```

通过标准：

| 检查 | 期望 | 不符时的含义 |
|---|---|---|
| `Class` | `ELF32` | 编成了 64 位，CROSS 指错 |
| `Machine` | `ARM` | 用了宿主 gcc |
| `interpreter` | **`/lib/ld-linux.so.3`** | 出现 `ld-linux-armhf.so.3` = 硬浮点工具链，本板跑不了 |
| `NEEDED` | 含 `libasound.so.2`、`libm.so.6`、`libpthread.so.0`、`libc.so.6` | 缺 ALSA 链接（本板需自带 libasound） |
| `GLIBC_x.y` | 最大值 ≤ **2.23**（本板 `libc-2.23.so`） | 工具链太新，见 §9 |

> 软浮点对象**不需要** `Tag_ABI_VFP_args: VFP registers`；缺失是正常的。
> 真正的判据是解释器路径与板端 `/lib/ld-linux*` 一致。

`make verify` 已把上面大部分打出来，日常用它即可。

---

﻿## 7. 部署到板子

> **本板实测（2026-11-11）：eth0 不可用。** `dmesg` 显示
> `eth0: PHY ID 00000000 at 0..31`（32 个地址全是 0），`stmmac_open: Cannot attach to PHY (error: -19)`，
> `carrier=0`、`operstate=down`。写进 IP 也没用，`ping` 直接 `Network is unreachable`。
> 所以本板**不要走 NFS/scp**，用下面的 TF 卡 / U 盘 / 串口 Zmodem；NFS 留给换板或修好 PHY 之后。

### 7.1 staging（VM 侧）

```bash
cd ~/gec6818
mkdir -p ~/stage/{bin,usr/lib,usr/share,audio}
cp build/gesture_audio ~/stage/bin/
cp ~/alsa-lib-1.1.3/install/usr/lib/libasound.so.2.0.0 ~/stage/usr/lib/
ln -sf libasound.so.2.0.0 ~/stage/usr/lib/libasound.so.2
cp -a ~/alsa-lib-1.1.3/install/usr/share/alsa ~/stage/usr/share/
make tools
./build/gen_wav ~/stage/audio/test_44100.wav 8
cd ~/stage && tar czf gapp.tgz .
ls -lh gapp.tgz
```

> **打包前先 `rm -f ~/stage/gapp.tgz`**：如果旧包还在目录里，`tar czf gapp.tgz .` 会把旧包也打进去
> （本机 3MB 包里就有一个嵌套 `./gapp.tgz`），白占体积。
>
> **本板 `/tmp` 是 tmpfs，重启后 `/tmp/gapp` 会被清空。** 首次部署后把 `gapp.tgz` 另存到
> 持久目录（`/root` 可写时，或 TF 卡），重启后只需：
> `mkdir -p /tmp/gapp && tar xzf /root/gapp.tgz -C /tmp/gapp`，不用再占串口 10 分钟。
> 更好的做法是直接在 rootfs 上跑持久版、以后只覆盖变化的二进制，见 **§7.3.1**。

把 `gapp.tgz` 弄到 Windows（VM 共享目录没开时，用宿主机的上传口）：

```bash
python3 - <<'EOF'
import urllib.request
data = open('/home/china/stage/gapp.tgz', 'rb').read()
req = urllib.request.Request('http://192.168.235.1:8001/gapp.tgz', data=data, method='PUT')
print('upload', urllib.request.urlopen(req).status, len(data))
EOF
```

Windows 侧文件在 `D:\vmShares\upload\gapp.tgz`。

### 7.2 传到板子（三选一）

**A. TF 卡（本板首选）**

1. Windows 上把 `gapp.tgz` 拷到 FAT32 TF 卡根目录；
2. 卡插板子，板端：

```bash
ls /dev/mmcblk*                       # eMMC 是 mmcblk0，TF 通常是 mmcblk1
mkdir -p /mnt/sd
mount /dev/mmcblk1p1 /mnt/sd
mkdir -p /tmp/gapp && cd /tmp/gapp
cp /mnt/sd/gapp.tgz .
gzip -d gapp.tgz && tar xf gapp.tar
```

**B. U 盘**

```bash
ls /dev/sd*
mkdir -p /mnt/usb && mount /dev/sda1 /mnt/usb
mkdir -p /tmp/gapp && cd /tmp/gapp
cp /mnt/usb/gapp.tgz .
gzip -d gapp.tgz && tar xf gapp.tar
```

**C. 串口 Xmodem / Zmodem（无网络时的主力）**

```bash
which rx rb rz sz
ls -l $(which rx) 2>/dev/null
```

- 有 `rz`（ZMODEM，优先）：
  ```bash
  cd /tmp && rz                          # SecureCRT: Transfer -> Send Zmodem，选 gapp.tgz
  ```
- 有 `rb`（YMODEM，带文件名与长度，最稳）：
  ```bash
  cd /tmp && rb                          # SecureCRT: Transfer -> Send Ymodem，选 gapp.tgz
  ```
- 只有 `rx`（XMODEM）：
  ```bash
  cd /tmp && rx gapp.tgz                 # SecureCRT: Transfer -> Send Xmodem，选 gapp.tgz
  ```
  XMODEM 不携带文件长度，收到的包尾部可能有填充；用 `gzip -dc` 解压即可：
  ```bash
  gzip -dc /tmp/gapp.tgz > /tmp/gapp.tar
  ```
- 两个都没有：`tools\make_serial_upload.ps1` 生成 base64 脚本，再用
  SecureCRT `Transfer -> Send ASCII`。

```bash
# 统一解包（XMODEM 尾部若有多余填充，gzip 会忽略）
gzip -dc /tmp/gapp.tgz > /tmp/gapp.tar
mkdir -p /tmp/gapp && cd /tmp/gapp
tar xf /tmp/gapp.tar
ls -l
```

### 7.3 板端首跑

```bash
cd /tmp/gapp
export LD_LIBRARY_PATH=/tmp/gapp/usr/lib
export ALSA_CONFIG_PATH=/tmp/gapp/usr/share/alsa/alsa.conf

./bin/gesture_audio --selftest audio/test_44100.wav
./bin/gesture_audio --dump-bmp /tmp/ui.bmp
./bin/gesture_audio --sensor sim --log info audio/test_44100.wav
```

`player.c` 会按 `default → plughw:0,0 → hw:0,0` 回退，日志里的 `ALSA 设备：xxx` 会告诉你实际用的是哪个。
如果根文件系统可写，也可以把 `usr/lib`、`usr/share/alsa`、`bin` 直接铺到 `/`，就不用两个环境变量。

### 7.3.1 持久化到 rootfs（当前版本 → 持久版的覆盖更新）

`/tmp` 是 tmpfs，重启清空；**持久版放 rootfs**（推荐 `/root/gapp`），重启后仍在。
下文的「当前版本」= 本次编出的 `build/gesture_audio`，「持久版」= `/root/gapp`
（含 `bin/`、`usr/lib`、`usr/share`、`audio/`，即 `gapp.tgz` 解开后的样子）。

**先判持久目录能不能写**（✅ 本板 2026-11-12 实测 `/root` 可写，`ROOT_RW=yes`；换板/换镜像必须重测）：

```bash
mount | head -20                                  # / 是 ext4 还是 squashfs/cramfs
df -h / /root 2>/dev/null
touch /root/.wtest && echo ROOT_RW=yes && rm -f /root/.wtest
```

- 出现 `ROOT_RW=yes`：用 `/root/gapp`（root 家目录，最不容易被系统升级覆盖）；
- 报 `Read-only file system`：先 `mount -o remount,rw /` 再试；仍失败就退回 TF 卡
  （`mount /dev/mmcblk1p1 /mnt/sd`，持久目录改 `/mnt/sd/gapp`，下文路径整体替换）。

**① 首次固化（只需一次：把当前 `/tmp/gapp` 变成持久版）**

```bash
killall gesture_audio 2>/dev/null; sleep 1
mkdir -p /root/gapp
cp -a /tmp/gapp/. /root/gapp/          # 含 bin/ usr/lib usr/share audio/
cd /root/gapp && tar czf /root/gapp.tgz . && sha256sum /root/gapp.tgz
```

**② 日常覆盖更新（推荐：只换二进制）**

libasound / `alsa.conf` / 素材没变时，串口只传「strip + gzip 过的新程序」：
`-g` 产物约 1.0 MB，`strip` 后约 0.2 MB，gz 后 < 0.1 MB，ZMODEM 5 KB/s 下约 20 s
（比整包 1.4 MB 快一个数量级）。

```bash
# VM / 集成机
cd ~/gec6818 && bash tools/armbuild.sh
cp build/gesture_audio /tmp/gesture_audio.new
arm-linux-strip /tmp/gesture_audio.new        # 无此命令就用 arm-none-linux-gnueabi-strip
gzip -9 -c /tmp/gesture_audio.new > /tmp/gesture_audio.gz
sha256sum /tmp/gesture_audio.gz               # 记下，板端解压后比对

# 板端（rx / rb / rz 把 gesture_audio.gz 传到 /tmp）
killall gesture_audio 2>/dev/null; sleep 1    # 进程在跑会报 Text file busy
rm -rf /root/gapp.bak && cp -a /root/gapp /root/gapp.bak
gzip -dc /tmp/gesture_audio.gz > /tmp/gesture_audio.new
chmod +x /tmp/gesture_audio.new
cp /tmp/gesture_audio.new /root/gapp/bin/.gesture_audio.new
mv /root/gapp/bin/.gesture_audio.new /root/gapp/bin/gesture_audio
```

> 为什么多一步 `cp` 再 `mv`：`/tmp`（tmpfs）→ rootfs 的 `mv` 实际是「复制 + 删源」，
> 中途断电会留下半个可执行文件；先 `cp` 落到同分区，再 `mv` 就是 `rename`，原子切换。
> 镜像没有 `killall` 时用 `kill $(pidof gesture_audio)`。

**③ 整包覆盖（`usr/lib`、`alsa.conf`、素材也变了）**

```bash
# VM 侧：旧包先从打包目录删掉，否则会被打进新包（§7.1）
cd ~/stage && rm -f gapp.tgz && tar czf gapp.tgz .
# 板端：新包先落盘（串口传的用 /tmp/gapp.tgz），解到新目录，最后一刻再切换
cp /mnt/sd/gapp.tgz /root/gapp.tgz.new && mv /root/gapp.tgz.new /root/gapp.tgz
rm -rf /root/gapp.new && mkdir -p /root/gapp.new && tar xzf /root/gapp.tgz -C /root/gapp.new
mv /root/gapp /root/gapp.prev && mv /root/gapp.new /root/gapp
```

**④ 持久版怎么启动（环境变量别每次手敲）**

```bash
cat > /root/gapp/run.sh <<'EOF'
#!/bin/sh
export LD_LIBRARY_PATH=/root/gapp/usr/lib
export ALSA_CONFIG_PATH=/root/gapp/usr/share/alsa/alsa.conf
cd /root/gapp
exec ./bin/gesture_audio "$@"
EOF
chmod +x /root/gapp/run.sh

/root/gapp/run.sh --selftest audio/test_44100.wav                # 期望 20 通过 / 0 失败
/root/gapp/run.sh --sensor sim --log info audio/test_44100.wav   # 出声 + LCD
```

> 写进 `/etc/profile.d/` 对板端 busybox shell 不一定生效（⚙️ 未实测），wrapper 最稳；
> 要开机自启再另做 init 脚本，且必须先报备占用时段（`devFlow.md §3`）。

**⑤ 对账与回滚**

- 版本可追溯：`/root/gapp/VERSION` 记 `commit` + 构建时间 + 二进制 sha256，
  与 GitHub 提交一一对应（`devFlow.md §7` 红线 4）；本次覆盖后在 `testGuide.md` T0 记一笔；
- 回滚二进制：`cp -a /root/gapp.bak/bin/gesture_audio /root/gapp/bin/`；
- 回滚整包：`rm -rf /root/gapp && mv /root/gapp.prev /root/gapp`；
- **镜像重刷会清空 `/root`**：想跨重刷保留，把 `gapp.tgz` 同时留一份在 TF 卡。

### 7.4 NFS（换板或修好 PHY 后）

若之后 `ls /sys/bus/mdio_bus/devices` 出现非零 PHY ID、`carrier=1`，再按下面启用 NFS：

```bash
# VM
sudo apt install -y nfs-kernel-server
echo "/home/gec6818/nfs 192.168.1.0/24(rw,sync,no_root_squash,no_subtree_check)" \
  | sudo tee -a /etc/exports
sudo exportfs -arv && sudo systemctl restart nfs-kernel-server

# 板端（IP 按实际网段改）
ifconfig eth0 192.168.1.100 netmask 255.255.255.0 up
route add default gw 192.168.1.1
mount -t nfs -o nolock,vers=3 192.168.1.10:/home/gec6818/nfs /mnt/nfs
```

> `nolock,vers=3` 必带；VM 必须是**桥接**。

## 8. 板端首跑（T0–T3）

```bash
cd /tmp/gapp
export LD_LIBRARY_PATH=/tmp/gapp/usr/lib
export ALSA_CONFIG_PATH=/tmp/gapp/usr/share/alsa/alsa.conf

# 1) 逻辑自检：不碰声卡/LCD/传感器，必须先 20/20
./bin/gesture_audio --selftest audio/test_44100.wav

# 2) 界面版式：导出一帧 BMP（可拷回电脑看）
./bin/gesture_audio --dump-bmp /tmp/ui.bmp

# 3) 正式运行（sim 后端先跑，不依赖传感器）
./bin/gesture_audio --sensor sim --log info audio/test_44100.wav
```

> 本板无网络，`tools/netctl.py` 不能从 VM 远程连；要么在板端用 `telnet 127.0.0.1 8888`
> （镜像有 telnet 客户端时），要么直接看串口日志里的 `STATUS` 周期输出。
> 根文件系统可写时把 `usr/lib`、`usr/share/alsa`、`bin` 铺到 `/`，可省掉两个环境变量。

预期：

- 自检 `20 通过 / 0 失败`；
- 日志出现 `framebuffer 就绪：800x480`（若失败会有一行降级 WARN，业务不受影响）；
- 播放时 `STATUS` 的 `pos` 递增、`und` 不持续增长；
- `PAUSE` 后 `state=PAUSED` 且 `pos` 冻结（D2 已修）；
- 2s 短素材 EOF 后 `NEXT` 能继续出声、`pos` 递增（D1 已修）；
- 坏文件/删素材后 `state=ERROR` 保持，换回好文件再 `PLAY` 能复位（D3 已修）。

D1/D2/D3 也可在 VM 上对**主机版**自动回归：

```bash
python3 tools/player_regress.py --exe build/gesture_audio
```

真机回归则用 `tools/netctl.py` 手工发 `PAUSE` / `NEXT` / `PLAY` 观察 `STATUS`。

---

## 9. 故障对照表

| 现象 | 最可能原因 | 判据 | 处理 |
|---|---|---|---|
| `./gesture_audio: No such file or directory`（文件明明在） | 解释器 ABI 不匹配（软/硬浮点混用） | `readelf -l \| grep interpreter` | 换 `arm-linux-`（`arm-none-linux-gnueabi`）全量重编 |
| `version 'GLIBC_2.27' not found` | 编译器太新 | `readelf --version-info` vs 板上 `/lib/libc.so.6` | 换厂商 5.4.0 工具链（§4.2 A） |
| `error while loading shared libraries: libasound.so.2` | 板上缺 ALSA 运行库（本板实测就没有） | 板上 `find / -name 'libasound*'` | 按 §4.3.2 交叉编，或 §4.3.3 放软浮点库 |
| `alsa/asoundlib.h: No such file or directory` | 工具链 sysroot 缺开发头 | 预检命令 | §4.3.2 编 alsa-lib 时一并装头 |
| `../../libasound.so: file not recognized` / 链接报浮点 ABI | 用了 armhf（硬浮点）的 libasound | `readelf -A libasound.so.2 \| grep VFP` | 换软浮点（armel 或自己编）的库 |
| `ALSA 打开失败` / `Device or resource busy` | 声卡被 Qt/madplay 占用或 `/dev/snd` 缺失 | `aplay -l`、`fuser -v /dev/snd/*` | 杀掉占用进程；确认单一 owner |
| 有日志无声 | 素材格式不符（非 PCM/16bit/1–2 声道） | `--selftest audio/x.wav` 第 20 条 | 用 `gen_wav` 重新生成 |
| `Illegal instruction` | `-march` 超过板端（如 armv8） | `readelf -A` 的 CPU_arch | 用默认 armv7 重编 |
| `make: arm-linux-gcc: command not found` | PATH 没加 | `which` | §4.2 export PATH |
| NFS `mount` 卡住/超时 | 缺 `nolock,vers=3` 或 VM 非桥接 | `mount \| grep nfs` | 按 §7.4 重挂；VM 改桥接 |
| 板端 `eth0` 报 `Cannot attach to PHY` | 本板 PHY 不可用（实测） | `dmesg \| grep -i phy`、`carrier` | 改 TF 卡/U 盘/Zmodem（§7.2），不要继续折腾 IP |
| `Permission denied` 运行 NFS 上的程序 | 挂载带 `noexec` 或没执行位 | `mount \| grep /mnt/nfs` | `chmod +x`；必要时拷到 `/tmp` 再跑 |
| 板端 `readelf: not found` | rootfs 精简 | — | 把产物拷回 VM 用 `readelf` 校验 |

---

## 10. 回滚与产物管理

```bash
# 上板前留一份上一版
cp build/gesture_audio build/gesture_audio.prev 2>/dev/null || true

# 记录本次产物的指纹（写进当日 agentLogs / testGuide）
sha256sum build/gesture_audio
```

- 板端回滚 = 把 `gesture_audio.prev` 拷回 `/mnt/nfs/gesture_audio`；
- **板子上跑的程序必须能在 GitHub 找到对应提交**（`devFlow.md §7` 红线 4），
  每次部署记录 commit SHA；
- `build/` 与 `dist/` 不入库（`.gitignore` 已覆盖）；
- 素材（`*.wav`）走 NFS，不进 git。

---

## 11. 检查清单（可打印）

```
[ ] 包 SHA256 与 Windows 侧一致
[ ] ${CROSS}gcc -dumpmachine = arm-none-linux-gnueabi（本板软浮点）
[ ] alsa/asoundlib.h 与 libasound.so.2 都已就位（§4.3）
[ ] make 零警告、零错误
[ ] readelf: Class=ELF32, Machine=ARM
[ ] readelf -l: /lib/ld-linux.so.3（不是 armhf）

[ ] readelf -d: NEEDED 含 libasound.so.2
[ ] GLIBC 需求 ≤ 2.23
[ ] 板端 --selftest 20 通过 / 0 失败
[ ] 板端真机出声，und 不持续增长
[ ] PAUSE→state=PAUSED、pos 冻结；EOF→NEXT 恢复；ERROR 粘滞
[ ] testGuide 的 ⏳ 项已回填实测值
[ ] agentLogs 当日条目 + commit SHA 已记录
```

---

## 附：最短路径（工具链已就绪时）

```bash
# Windows
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pack_for_ubuntu.ps1
py -3.11 -m http.server 8000 --directory dist

# Ubuntu
wget http://<宿主机IP>:8000/gec6818-gesture-audio-src-*.tar.gz
mkdir -p ~/gec6818 && tar -xzf gec6818-gesture-audio-src-*.tar.gz -C ~/gec6818
cd ~/gec6818
bash tools/armbuild.sh
cp build/gesture_audio ~/nfs/ && chmod +x ~/nfs/gesture_audio

# 板端
cd /mnt/nfs && ./gesture_audio --selftest audio/test_44100.wav
```
