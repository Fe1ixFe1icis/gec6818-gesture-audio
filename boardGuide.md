# GEC6818 开发板参考文档（Board Guide）

> 适用项目：智能手势控制音频播放器（`projectBrief.md`）
> 版本：**V1.1** · 2026-11-11 · 面向板端开发（C / Linux 系统编程）
> 定位：**动手前的唯一事实来源**。写代码、接线、上板前先查这里，避免靠"网上抄一段"试错。

**修订记录**

| 版本 | 日期 | 改动 |
|---|---|---|
| V1.0 | 2026-11-11 | 初版：硬件 / 系统环境 / 外设编程 / 性能边界 / 自检清单 |
| V1.1 | 2026-11-11 | ① 调试串口落到本机实测：CH340 USB 转串口、PC 端 **COM6**、SecureCRT 会话 `Serial-COM6`，并补「串口独占」判据与「连上≠通了」排障（§1.2 / §2.2 / §8.B）；② 修 §3.3 延迟预算表把 `period_size` 误标成 `buffer_size` 的错误；③ 修 §4.2 音频环形缓冲容量算错（2 秒 · 立体声 · 16bit = 344KB，原写 700KB）；④ §2.4 标出 `devFlow.md` / `README.md` 里 `-o nlock` 的笔误应为 `nolock`；⑤ §8.D 填入已确认项 |

## 0. 可信度标记与使用方式

本文每条结论都标了可信度，**不要跳过标记**：

| 标记 | 含义 | 你该怎么做 |
|---|---|---|
| ✅ | 已由厂商资料或多份独立实测记录交叉验证 | 可直接用 |
| ⚠️ | 资料冲突 / 因批次或镜像而异 | 先按 §6 自检清单实测，再改本文 |
| ❓ | 公开资料缺失，需自行确认 | 不要基于它做设计决策 |

三条使用原则：

1. **硬件以丝印和原理图为准**，本文只描述"典型配置"。同型号底板不同批次可能裁剪外设。
2. **软件以板上 `uname -a`、`ls /dev`、`cat /proc/...` 为准**，本文给的是出厂 Linux 镜像的典型值。
3. 任何 ⚠️/❓ 项一旦实测确认，**回来把本文改成 ✅ 并写清实测命令与输出**（文档随板子一起沉淀，见 §8.D）。

---

## 1. 硬件

### 1.1 核心板（GEC-S5P6818）

| 项目 | 参数 | 说明 |
|---|---|---|
| SoC | Samsung **S5P6818** | 8× Cortex-A53，最高 1.4GHz，ARMv8-A 架构 |
| 运行模式 | **AArch32 / ARMv7 用户态** ✅ | 出厂 BSP 为 32 位内核，`cat /proc/cpuinfo` 显示 `ARMv7 Processor`。**不要**按 64 位 ARM 编译 |
| 内存 | 1GB DDR3（单通道），可定制 2GB | 单通道意味着内存带宽是瓶颈，大块 memcpy 会明显抢占 |
| 存储 | 8GB eMMC（标配） | 4/8/16/32GB 可选，19nm MLC |
| 电源管理 | **AXP228** PMU | 支持动态调频、库仑计、休眠唤醒 |
| 以太网 PHY | **RTL8211E** 千兆 | 实际吞吐受 CPU 与单通道 DDR3 限制 |
| 核心板尺寸 | 75mm × 55mm，10 层板 | 引脚间距 2.0mm |

> 常见网络错误：把 S5P6818 写成"四核/六核 Cortex-A7"、"主频 1.5GHz"、"AArch64 用户态"。以上都错，别抄进报告。

### 1.2 底板接口（典型配置）

| 接口 | 规格 | 本项目是否用 |
|---|---|---|
| 电源 | DC 5V 圆孔（CN1），**建议 ≥2A 独立适配器** | 是 |
| 调试串口 | **UART0** → **USB 转串口（CH340）** → PC 侧显示为 **COM6**，**115200 8N1 无流控** ✅（本机实测，见 §2.2） | 是（救命通道） |
| USB OTG | Micro-USB，用于 **fastboot 烧写**，也可作 Host | 烧写时 |
| USB Host ×3 | 经 HUB 扩展，USB 2.0 | 可选（U 盘/键鼠/WiFi） |
| 以太网 | RJ45 千兆，RTL8211E | 是（NFS/TCP） |
| 音频 | 3.5mm 输出（绿）/ 输入（红）+ 板载喇叭接口，**WM8960** codec（常见）⚠️，I2S | 是（核心） |
| LCD | 40Pin RGB 排线 + **背光 PWM（第 1 脚）** | 是（核心） |
| 显示扩展 | LVDS、MIPI DSI、HDMI | 否 |
| SD | SD0（TF 卡，可做启动/升级卡）、SD1 | 升级时 |
| 按键 | **K1 返回 / K2 音量- / K3 音量+ / K4 菜单**，硬复位键 | 可选（物理备用手感） |
| 蜂鸣器 | **有源蜂鸣器，GPIO 直驱（非 PWM）** ⚠️ | 可选（提示音） |
| LED | 电源指示灯 + **4 路可控 LED** | 调试指示 |
| 传感器 | 重力传感器 MMA8653、红外接收头 HS0038 | 否 |
| 扩展 | SPI、DVP/CSI 摄像头、SIM/3G、GPS（可选） | 传感器接这里 |

### 1.3 显示与触摸

| 项目 | 值 | 备注 |
|---|---|---|
| 屏型号 | **vs070cxn**（7 寸，800×480）✅ | 由内核启动参数 `lcd=vs070cxn` 指定；换屏必须同步改 bootargs |
| 分辨率 / 色深 | **800 × 480，32bpp（ARGB8888）** ✅ | 一帧 = 800×480×4 = **1,536,000 字节** |
| 像素字节序 | 小端，内存中为 B,G,R,A；C 里写 `0x00RRGGBB` 即可 ✅ | `0x00FF0000` = 纯红 |
| 设备节点 | `/dev/fb0` ✅ | |
| 触摸 | 电容，**gslx680** 控制器，I2C 接口 ✅ | 启动参数 `tp=gslx680-linux` |
| 触摸节点 | `/dev/input/event0`（**编号可能变**）⚠️ | 必须按名字匹配，见 §3.2 |

### 1.4 音频链路

```
应用 (PCM) → ALSA → I2S → WM8960 codec → 3.5mm 耳机 / 板载喇叭
                                  ↑
                            I2C 控制（音量/通路）
```

> codec 型号以板上丝印与 `aplay -l` / `amixer scontrols` 输出为准；不同批次可能换料，但**应用层只依赖 ALSA，不受影响**。

- 声卡：`card 0`，`device 0`，播放节点 `/dev/snd/pcmC0D0p` ✅
- 控制节点 `/dev/snd/controlC0`，用 `amixer` 调音量与通路
- **同时只有一个进程能占用 PCM 设备**（除非配 dmix），这是本项目"本地手势 + PC 远程"必须处理的前提（见 §3.3）

### 1.5 启动链路与存储布局 ✅

```
上电
 └─ BootROM（片内，不可改）
     ├─ SD0（TF 卡）── 插了启动卡就用它
     ├─ SD2（eMMC）
     └─ USB OTG ───── fastboot 烧写通道
          ↓
     2ndboot（初始化 DDR）→ u-boot（打包为 ubootpak.bin）
          ↓ 读环境变量 bootcmd / bootargs
     ext4load mmc 2:1 0x48000000 uImage → bootm 0x48000000
          ↓
     Linux 3.4.39-gec（32 位）→ 挂载 /dev/mmcblk0p2 (ext4) → /sbin/init
```

出厂环境变量（**必须记住这两行，改屏参/改根分区就在这里**）：

```
bootcmd=ext4load mmc 2:1 0x48000000 uImage;bootm 0x48000000
bootargs=root=/dev/mmcblk0p2 rw rootfstype=ext4 lcd=vs070cxn tp=gslx680-linux
```

- u-boot 启动时有 **数秒倒计时**，此时按任意键进命令行 ✅
- 内核加载地址 `0x48000000`，校验方式 legacy uImage
- 升级方式二选一：
  - **SD 启动卡自动升级**：卡内建目录（常见 `GEC6818-qt` ⚠️），放入 `GECuboot.bin`、`boot.img`、`qt-rootfs.img`、`env.txt`，插卡上电自动比对并烧写；开机时**按住 K1（返回键）可强制升级** ⚠️
  - **fastboot（USB OTG）**：`fastboot flash ubootpak ubootpak.bin` → `flash boot boot.img` → `flash system qt-rootfs.img` → `reboot`

> ⚠️ 烧写是**唯一能把板子变砖的操作**。规则：升级前先把当前可启动镜像备份到集成机；两人在场，一人念命令一人敲；不在临近答辩时升级。

### 1.6 供电与上电顺序

1. 先接串口线，再插电源（保证能看到完整启动日志）
2. 5V 独立适配器，**不要用电脑 USB 口供电**（电流不足会随机重启/花屏）
3. 关机用 `poweroff` 或 `sync` 后断电；直接拔电可能损坏 eMMC 文件系统
4. 屏幕排线很脆，插拔前断电，卡扣要压平

---

## 2. 系统环境

### 2.1 镜像与版本

| 项目 | 值 |
|---|---|
| 内核 | **Linux 3.4.39-gec**（三星 BSP，非主线） ✅ |
| 用户态 | ARMv7 32 位，glibc |
| 根文件系统 | 出厂常见 **Qt 4.8.7 版**（`qt-rootfs.img`），也有 Ubuntu 版 |
| 已装库 | Qt4、ALSA、madplay（部分镜像）、busybox |
| 内存占用 | Qt 镜像常驻较高，纯命令行程序可忽略 |

### 2.2 串口登录（第一优先级技能）

Windows：**SecureCRT**（本机在用，会话名 `Serial-COM6`）/ MobaXterm / PuTTY

#### 2.2.1 本机实测参数（2026-11-11 ✅）

| 项 | 实测值 | 怎么查到的 |
|---|---|---|
| 转接芯片 | **USB-SERIAL CH340**（`VID_1A86&PID_7523`，驱动服务 `CH341SER_A64`，设备状态 OK，`ConfigManagerErrorCode=0`） | 设备管理器 / `Get-CimInstance Win32_PnPEntity` |
| PC 端口号 | **COM4**（2026-11-11 当前） | `[System.IO.Ports.SerialPort]::GetPortNames()` |
| 终端软件 | **SecureCRT**，会话 `Serial-COM4`，状态栏显示 `Serial, COM4, 115200`，终端类型 VT100 | 窗口标题与状态栏 |
| 端口参数 | **115200 · 8 数据位 · 1 停止位 · 无校验 · 无流控** | 同上 |
| 板端 | UART0 = `/dev/ttySAC0`。底板自带 USB 转串口时一根 USB 线即可；用外置 USB-TTL 模块时接 **GND / TX / RX 三根，TX 与 RX 交叉** | 底板丝印 + 内核 console |

**端口号是机器侧属性，不是板子的属性**：换 USB 口、换电脑、换插拔顺序都会重新分配。别照抄 COM6，先查再连。

SecureCRT 配置位置：`Options → Session Options → Connection → Serial`，Port = `COM4`，Baud rate = `115200`，Data bits = `8`，Parity = `None`，Stop bits = `1`；`Flow Control` 里 **RTS/CTS 与 XON/XOFF 全部取消勾选**（开了就一片乱码，这是最高频的错）。

> **中文日志必须用 UTF-8 显示**。程序的日志是 UTF-8；SecureCRT 默认常是 GBK，
> 会把 `结果：20 通过` 显示成 `缁撴灉锛0 ...`（`2` 被 GBK 双字节吞掉）。
> 设置：`Options → Session Options → Terminal → Appearance → Character encoding` 选 **UTF-8**，
> 字体选支持中文的等宽字体。改完重跑 `--selftest` 即可看到正常中文。

#### 2.2.2 连之前先确认"是哪一个口"

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' } |
    Select-Object Name, Status, ConfigManagerErrorCode
```

要能看见 `USB-SERIAL CH340 (COM6)` 且 `Status=OK`。只看到 `蓝牙链接上的标准串行 (COM3/COM4)` 而没有 CH340，就是线没插好或驱动没装（沁恒官网 `CH341SER` 驱动）。

#### 2.2.3 "连上了"和"通了"是两件事

串口是**独占资源**，同一时刻只有一个程序能打开它。这条特性正好可以当判据，几乎不花钱：

```powershell
# SecureCRT 已连接时，另开一个 PowerShell 试开同一个口
$p = New-Object System.IO.Ports.SerialPort("COM6",115200,'None',8,'One')
$p.Open()   # 报"对端口 COM6 的访问被拒绝" = SecureCRT 确实占着它
```

| 现象 | 含义 | 下一步 |
|---|---|---|
| 另开程序报 **访问被拒绝** | SecureCRT 持有该口，连接状态正常 | 继续看板端有没有输出 |
| 另开程序 **打开成功** | 没有程序占用 → SecureCRT 其实没连上 | 在 SecureCRT 里重新 Connect，核对会话端口 |
| SecureCRT 里按键有本地回显、但**板端零回包**（回车不出新行、不见提示符） | 线路侧没有数据：板子没上电 / 只插了 USB 线没接板子 / TX-RX 接反 / 板端 console 没跑 | 先查供电与 3 根线，再查波特率 |

**本机 2026-11-11 15:5x 实测状态**：适配器在位、端口被 SecureCRT 独占（第一行判据成立）、**但板端无任何回显**（敲回车与字符都只有本地显示）。→ 落在上表第三行，需人工确认板子是否已上电、USB 转串口是否真的接到了 UART0 的 3 根线上。

#### 2.2.4 看到什么算登录成功

看到 `[root@GEC6818 /]#` 即登录成功，root 默认无密码 ⚠️（本机尚未取得回显，未确认）。看不到就按**供电 → 线序 → 波特率 → 流控**的顺序查，不要一上来怀疑板子坏了。

Linux 主机：

```bash
sudo minicom -D /dev/ttyUSB0 -b 115200
# 或
picocom -b 115200 /dev/ttyUSB0     # CH340 在 Linux 下是 /dev/ttyUSB0（内核自带 ch341 驱动）
```

### 2.3 网络配置（每次重启会丢，必须固化）

> **⚠️ 本板实测（2026-11-11）：eth0 不可用，NFS/scp 暂时走不通。**
> `dmesg` 显示 `eth0: PHY ID 00000000 at 0..31`（32 个地址全是 0），
> 打开时 `stmmac_open: Cannot attach to PHY (error: -19)`，`carrier=0`、`operstate=down`。
> 即使 `ifconfig` 能写进 IP，链路也没起来，`ping` 报 Network is unreachable。
> 这属于 PHY/MDIO 硬件或镜像 PHY 配置问题，不是 IP/路由能修的。
> **当前部署请走 `§2.4` 的 TF 卡 / U 盘 / 串口 Zmodem**；网络方案保留给换板或修好 PHY 之后。

```bash
# 板端临时配置（仅当 eth0 能 attach PHY 时有效）
ifconfig eth0 192.168.1.100 netmask 255.255.255.0 up
route add default gw 192.168.1.1
ping -c 3 192.168.1.10        # 集成机 IP

# 固化：追加到 /etc/init.d/rcS 或 /etc/profile（按镜像实际 init 体系）
```

前置条件：**虚拟机网络必须桥接**，否则板子永远 ping 不通集成机。三件套自查：

```bash
ip addr        # 集成机：有 192.168.1.x
ping <板子IP>  # 集成机 → 板子
ping <集成机IP># 板子 → 集成机
```

诊断本板 PHY 的命令（换线/换板后复测）：

```bash
dmesg | grep -i -E 'stmmac|phy|mdio|eth0'
ls /sys/bus/mdio_bus/devices
cat /sys/class/net/eth0/operstate /sys/class/net/eth0/carrier
```

### 2.4 传文件（按推荐顺序）

> **本板（2026-11-11 实测）eth0 PHY 不可用，以下 ①/② 暂时无效**；
> 当前请直接用 ③ TF 卡 / ④ U 盘 / ⑤ 串口 Zmodem。

**① NFS（换板或修好 PHY 后首选，见 `devFlow.md` §4）**

```bash
# 集成机（Ubuntu）
echo "/home/gec6818/nfs 192.168.1.0/24(rw,sync,no_root_squash,no_subtree_check)" | sudo tee -a /etc/exports
sudo exportfs -arv && sudo systemctl restart nfs-kernel-server

# 板端
mkdir -p /mnt/nfs
mount -t nfs -o nolock,vers=3 192.168.1.10:/home/gec6818/nfs /mnt/nfs
```

`vers=3` 和 `nolock` 两个参数必须带：3.4 内核对 NFSv4 与锁协议支持不完整，缺了会卡住不返回。

**校注（V1.1）**：`devFlow.md` §4 与 `README.md` 里写成 `mount -t nfs -o nlock ...`，`nlock` 是 `nolock` 的笔误，照抄会挂载失败。挂载命令以本节为准。

**② scp**：镜像若带 sshd 才能用，**先确认 `ps | grep sshd`** ⚠️（依赖上面 eth0）
**③ TF 卡（本板首选）**：FAT32 卡拷入后在板端挂载：

```bash
ls /dev/mmcblk*                 # eMMC 是 mmcblk0，TF 卡通常是 mmcblk1
mkdir -p /mnt/sd
mount /dev/mmcblk1p1 /mnt/sd
cp /mnt/sd/gapp.tgz /tmp/
```

**④ U 盘**：`ls /dev/sd*` 确认设备名，`mount /dev/sda1 /mnt/usb` 后拷贝。
**⑤ 串口 Xmodem/Zmodem（本机首选）**：先确认板端有哪个接收端：

```bash
which rx rb rz sz
ls -l $(which rx) 2>/dev/null
```

- 有 `rb`（YMODEM，最稳）：板端 `cd /tmp && rb`，SecureCRT 菜单
  `Transfer → Send Ymodem` 选 `gapp.tgz`；YMODEM 带文件名和长度。
- 有 `rx`（XMODEM）：板端 `cd /tmp && rx gapp.tgz`，SecureCRT 菜单
  `Transfer → Send Xmodem` 选 `gapp.tgz`；XMODEM 不带长度，收到后文件尾部可能有
  填充，`gzip -t` 可能提示 trailing garbage，`gzip -dc > gapp.tar` 仍可解出。
- 有 `rz`（ZMODEM）：板端 `cd /tmp && rz`，SecureCRT 菜单 `Transfer → Send Zmodem`；
  ZMODEM 带长度与 CRC，优先用它。
- 两个都没有：用 `tools\make_serial_upload.ps1` 生成 base64 脚本，再
  `Transfer → Send ASCII`（见该脚本头部说明）。

115200 下 1–2 MB 大约 2–5 分钟，适合没有网络的应急部署。

### 2.5 交叉编译工具链

出厂配套为 **GCC 5.4.0**（本机 VM 实测路径 `/usr/local/arm/5.4.0/usr/bin/arm-linux-gcc`，
`-dumpmachine` 输出 `arm-none-linux-gnueabi`，sysroot 在
`/usr/local/arm/5.4.0/usr/arm-none-linux-gnueabi/sysroot`）。

```bash
# 安装后验证（本机 VM 就是这套）
arm-linux-gcc --version | head -n1
arm-linux-gcc -dumpmachine                       # arm-none-linux-gnueabi
arm-linux-gcc -print-sysroot
arm-linux-gcc hello.c -o hello
file hello                                       # ARM, EABI5
```

**✅ 本板 ABI 实测（2026-11-11，串口 COM4，root 登录）**

| 项 | 实测值 | 判定 |
|---|---|---|
| 动态 loader | `/lib/ld-linux.so.3 -> ld-2.23.so` | **软浮点**（硬浮点为 `ld-linux-armhf.so.3`） |
| libc | `/lib/libc.so.6 -> libc-2.23.so` | glibc **2.23** |
| `-dumpmachine` | `arm-none-linux-gnueabi` | 与 loader 一致，**厂商这套 5.4.0 就是对的** |
| ALSA 用户库 | `find / -xdev -name 'libasound*'` 为空 | **板上没有 libasound**，需自带（见 §3.3.1） |

> ⚠️ 旧版本文档按“rootfs 多为硬浮点”推断要求 `gnueabihf`，**与本板实测不符**。
> `gnueabi`/`gnueabihf` 不是看名字，而是看板端 loader：
> `ld-linux.so.3` → 软浮点；`ld-linux-armhf.so.3` → 硬浮点。两者 ABI 不兼容，
> 混用的典型症状就是“文件明明在，执行却报 `No such file or directory`”。

**其他约束**：

- 本板 glibc 2.23：不要用 Ubuntu 18.04 自带的 `gcc-arm-linux-gnueabihf`（7.x，链接 glibc 2.27），
  产物会报 `GLIBC_2.27 not found`；
- 板上**没有 libasound**，但内核 `/dev/snd` 与 `/proc/asound/cards` 正常。
  要么用 5.4.0 工具链自己交叉编 alsa-lib，要么用软浮点的预编译库（见 §3.3.1）；
- 救急可加 `-static` 绕开板端库版本问题，但本项目的 ALSA/`default` 插件不建议全静态。

### 2.6 设备节点地图

| 节点 | 用途 | 可信度 |
|---|---|---|
| `/dev/fb0` | LCD 帧缓冲，800×480×32bpp | ✅ |
| `/dev/input/event0` | 触摸屏（gslx680） | ⚠️ 编号需实测 |
| `/dev/input/event1+` | 可能为按键/其它输入设备 | ⚠️ |
| `/dev/snd/pcmC0D0p` | 音频播放 PCM | ✅ |
| `/dev/snd/controlC0` | 音频控制（音量/通路） | ✅ |
| `/dev/i2c-0 … /dev/i2c-N` | I2C 总线，编号需 `i2cdetect -l` 实测 | ⚠️ |
| `/dev/ttySAC0` | **调试串口，禁止占用** | ✅ |
| `/dev/ttySAC1` / `ttySAC2` | 引出串口（COM1/COM2） | ⚠️ 对应关系实测 |
| `/dev/mmcblk0p1/p2` | eMMC 分区（p2 为根） | ✅ |
| `/sys/devices/platform/leds/leds/beep/brightness` | 蜂鸣器（已注册为 gpio-leds） | ✅ |
| `/sys/devices/platform/leds/leds/ledN/brightness` | 4 路 LED | ⚠️ N 的取值实测 |
| `/sys/class/gpio/` | GPIO sysfs 导出接口 | ⚠️ 见 §3.4 |

一次性摸清全板外设（**上板第一条命令**）：

```bash
uname -a; cat /proc/cpuinfo | head -20; free
ls /dev/fb* /dev/input/event* /dev/snd/ /dev/i2c-* /dev/ttySAC* 2>/dev/null
cat /proc/bus/input/devices
cat /proc/asound/cards; aplay -l
i2cdetect -l 2>/dev/null
ls /sys/class/leds/ /sys/devices/platform/leds/leds/ 2>/dev/null
cat /proc/cmdline
```

### 2.7 开机自启

3.4 内核镜像通常用 sysvinit/busybox init，**不要指望 systemd** ⚠️：

```bash
# 常见位置（按实际存在选）
/etc/init.d/rcS
/etc/profile
```

自启只放"环境准备"（网络、NFS 挂载），**不要放业务程序**——程序崩溃会拖住登录，调试期一律手工启动。

---

## 3. 外设编程

### 3.1 LCD 帧缓冲（display 模块）

**关键参数**：800×480、32bpp、一行 `line_length` 字节（**不要硬编码 800×4**，用 `finfo.line_length`）。

```c
/* include/lcd.h 参考实现，接口由 display 模块 owner 冻结后调整 */
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

typedef struct {
    int            fd;
    unsigned char *mem;    /* 显存映射 */
    unsigned int  *back;   /* 用户态后备缓冲 */
    int            w, h, bpp;
    size_t         line_len, map_size;
} fb_t;

int fb_open(fb_t *fb)
{
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;

    if ((fb->fd = open("/dev/fb0", O_RDWR)) < 0) return -1;
    ioctl(fb->fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb->fd, FBIOGET_FSCREENINFO, &fi);

    fb->w        = vi.xres;
    fb->h        = vi.yres;
    fb->bpp      = vi.bits_per_pixel;
    fb->line_len = fi.line_length;
    fb->map_size = fi.line_length * vi.yres_virtual;   /* 注意是 virtual */

    fb->mem = mmap(NULL, fb->map_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) { close(fb->fd); return -1; }

    fb->back = calloc((size_t)fb->w * fb->h, 4);        /* 1.5MB */
    return fb->back ? 0 : -1;
}

static inline void put_px(fb_t *fb, int x, int y, unsigned int c)
{
    if ((unsigned)x < (unsigned)fb->w && (unsigned)y < (unsigned)fb->h)
        fb->back[y * fb->w + x] = c;                    /* 0x00RRGGBB */
}

/* 整屏提交；优化方向见下文 */
void fb_flush(fb_t *fb)
{
    for (int y = 0; y < fb->h; y++)
        memcpy(fb->mem + (size_t)y * fb->line_len,
               fb->back + (size_t)y * fb->w, (size_t)fb->w * 4);
}
```

**为什么要有 `back` 后备缓冲**：直接往显存画，屏幕会实时显示"画到一半"的中间态，出现闪烁/撕裂。双缓冲 + 一次性提交是 LCD 界面的最低要求。

**性能取舍**（1.5MB 整屏拷贝，按单通道 DDR3 估算 1~3ms）：

| 刷新策略 | 适用 | 说明 |
|---|---|---|
| 整屏 `memcpy`，5fps | ✅ 本项目首选 | 每帧 200ms 预算，开销可忽略 |
| 脏矩形局部提交 | 参数条/波形局部变化 | 省带宽，复杂度上升 |
| `FBIOPAN_DISPLAY` 翻页 | 若有 yres_virtual=960 ⚠️ | 零拷贝，但驱动未必支持，先 `fbset` 验证 |

**坑**：

- `mmap` 长度必须用 `line_length × yres_virtual`，用 `w*h*4` 在虚拟高度更大时会缺页崩溃
- 帧缓冲**没有并发保护**：多进程同时画一定花屏。显示只能有一个 owner（本项目由 `display.c` 独占）
- 中文字库不要临时移植 freetype。预先用工具生成点阵字模数组（`const unsigned char font[]`），零依赖、零解码开销
- BMP 解析注意行对齐：`row_size = ((width*3 + 3) & ~3)`，24 位 BMP 每行补到 4 字节

### 3.2 触摸输入（evdev）

**不要假设 event0**，按设备名匹配：

```bash
cat /proc/bus/input/devices    # 找 Name="gslX680" 或含 touch 的那一组，看 Handlers=eventN
evtest /dev/input/event0       # 有 evtest 的话，直接看原始事件
```

```c
#include <linux/input.h>

static int touch_open(int *max_x, int *max_y)
{
    /* 生产代码应遍历 /dev/input/event* 用 EVIOCGNAME 匹配，此处简化 */
    int fd = open("/dev/input/event0", O_RDONLY);
    struct input_absinfo ai;

    if (fd < 0) return -1;
    ioctl(fd, EVIOCGABS(ABS_X), &ai); *max_x = ai.maximum;
    ioctl(fd, EVIOCGABS(ABS_Y), &ai); *max_y = ai.maximum;
    return fd;
}

/* 主循环：一次 SYN_REPORT 代表一帧完整坐标 */
struct input_event ev;
while (read(fd, &ev, sizeof ev) == sizeof ev) {
    switch (ev.type) {
    case EV_ABS:
        if (ev.code == ABS_X) x = ev.value * 800 / max_x;   /* 必须缩放！ */
        if (ev.code == ABS_Y) y = ev.value * 480 / max_y;
        break;
    case EV_KEY:
        if (ev.code == BTN_TOUCH) down = ev.value;
        break;
    case EV_SYN:
        if (ev.code == SYN_REPORT && down) { /* 处理 (x,y) */ }
        break;
    }
}
```

**坑**：

- **原始坐标范围通常不是 0~800**（常见 0~1023 或 0~4095），不缩放会导致只能点到屏幕一角
- 坐标可能 X/Y 互换或反向（面板贴装方向差异）。先用 `evtest` 点四角，确认 (0,0) 在哪，再决定是否交换/取反
- 手势判定必须有**距离阈值 + 时间窗**：`|Δx| > 50px 且 Δt < 300ms` 才算滑动，否则抖动会误触发切歌
- 电容屏上报是"按下-移动-抬起"序列，**不要按像素点触发业务**，要按手势语义在上抬时提交
- `/dev/input/event*` 是独占读的：两个进程同时读会互相抢事件。触摸只有一个 owner

### 3.3 音频 ALSA（player / effect 模块的核心）

**先确认链路通不通**：

```bash
cat /proc/asound/cards          # 应有 card 0
aplay -l                        # 列出 playback device
amixer sset 'Playback' 80%      # 音量（控件名以 amixer scontrols 为准）
speaker-test -D hw:0,0 -c2 -t sine -l 1   # 出声即通路正常
aplay -D hw:0,0 test.wav        # WAV 必须是 16bit/立体声/板子支持的采样率
```

**参数选择（决定延迟，本项目的关键）**：

| 参数 | 推荐值 | 说明 |
|---|---|---|
| 格式 | `S16_LE` | 板端 codec 原生，避免软件转换 |
| 声道 | 2 | |
| 采样率 | 44100（或 48000） | 先 `hw_params_set_rate_near` 接受驱动就近值 |
| period_size | 256 ~ 1024 帧 | 256 ≈ 5.8ms，1024 ≈ 23ms @44.1k |
| buffer_size | 4 × period | 4 个周期，兼顾延迟与抗抖动 |

**延迟预算**（@44.1kHz；注意表里是 **period_size**，`buffer_size = 4 × period_size`，别把两者混为一谈）：

| period_size | buffer_size | 单周期时长 | 端到端缓冲 |
|---|---|---|---|
| 1024 | 4096 | 23ms | ≈ 93ms |
| 512 | 2048 | 12ms | ≈ 46ms |
| 256 | 1024 | 6ms | ≈ 23ms（CPU 抖动大时易 underrun） |

本项目目标"延迟 ≤ 300ms"，**ALSA 侧留 60~90ms 就够**，剩下的预算给传感器与显示。不要把 buffer 调到 200ms 以上，手感会明显发钝。

```c
#include <alsa/asoundlib.h>

snd_pcm_t *pcm;
snd_pcm_hw_params_t *hw;
unsigned int rate = 44100;
snd_pcm_uframes_t period = 512, buffer = 2048;

snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
snd_pcm_hw_params_malloc(&hw);
snd_pcm_hw_params_any(pcm, hw);
snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
snd_pcm_hw_params_set_channels(pcm, hw, 2);
snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
snd_pcm_hw_params(pcm, hw);

/* 写循环：必须处理 xrun（欠载），否则一次卡顿后永久失声 */
for (;;) {
    snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, frames);
    if (n == -EPIPE)       { snd_pcm_prepare(pcm); continue; }  /* underrun */
    if (n == -ESTRPIPE)    { while (snd_pcm_resume(pcm) == -EAGAIN) usleep(1000);
                             snd_pcm_prepare(pcm); continue; }
    if (n < 0)             { snd_pcm_recover(pcm, n, 1); continue; }
    buf += n * 4; frames -= n;                     /* 立体声 16bit = 4 字节/帧 */
}
```

**MP3 播放的正确分层**（本项目最容易做错的地方）：

```
❌ 错误：madplay 直接播 MP3 → 声卡    （声音出来了，但你插不进效果链，也没法变速）
✅ 正确：libmad 解码 → PCM 环形缓冲 → 效果链(回声/EQ/变速) → ALSA
```

- `madplay` 只适合**验证声卡通路**，不要用它做项目播放器
- 若要快速出效果：`madplay -o raw:- song.mp3 | ./my_player` 走管道，把 madplay 当纯解码器；缺点是进程间延迟与不可控
- 正式方案：交叉编译 libmad（或直接用 WAV，把解码问题从关键路径上移除）+ 自研 PCM 管线

**变速 / 变调的工程取舍**：

| 需求 | 实现 | 代价 |
|---|---|---|
| 变速（音调一起变） | 改重采样步长，线性插值 | 极低，A53 余量充足 |
| 变速不变调 | 需要时间伸缩（WSOLA/相位声码器） | 中高，实现复杂、易有金属音 |
| 变调不变速 | 重采样 + 时间伸缩组合 | 高 |

**建议 v1 只做"变速（音调随动）"**，答辩时说明取舍；把省下的算力投入回声 + 三段 EQ 的稳定性。这比"三个都做但都卡"强得多。

**坑**：

- 设备被占用报 `Device or resource busy`：别的进程（Qt 播放器、残留的 madplay）还占着。`ps` 找出来杀掉，或设计上保证单一 owner
- 爆音/断续：先加 `buffer_size`，再查 CPU 占用；`period` 太小是主因
- 采样率不支持：用 `plughw:0,0` 走软件转换，延迟略增但兼容
- 需要在多进程间共享声卡时，写 `/etc/asound.conf` 配 `dmix`（本项目不推荐，实时性变差）

#### 3.3.1 本板没有 libasound：音频用户库必须自带

2026-11-11 实测（COM4）：

```
/lib/ld-linux.so.3 -> ld-2.23.so        # 软浮点
find / -xdev -name 'libasound*'         # 空
ls /dev/snd/                            # controlC0 pcmC0D0c pcmC0D0p timer
cat /proc/asound/cards                  # card 0: I2S-s5p / card 1: SPDIF
```

即：**内核声卡驱动在，用户态 `libasound.so.2` 缺**。`player.c` 的 `BOARD=1` 链接
`-lasound`，所以必须先补库，否则链接或运行都会失败。

**A. 用 5.4.0 工具链交叉编 alsa-lib（推荐，ABI 最匹配）**

```bash
tar xf alsa-lib-1.1.3.tar.bz2 -C ~ && cd ~/alsa-lib-1.1.3
CC=arm-linux-gcc AR=arm-linux-ar RANLIB=arm-linux-ranlib \
  ./configure --host=arm-none-linux-gnueabi --prefix=/usr \
              --disable-static --disable-python
make -j2
make install DESTDIR=$PWD/install
find install -name 'libasound.so*'      # install/usr/lib/libasound.so.2.0.0
```

**B. 预编译软浮点库（备选，快）**

Debian **armel**（不是 armhf）的 `libasound2` 是软浮点，glibc 2.19 构建，可在 2.23 上运行：

```bash
dpkg -x libasound2_1.0.28-1_armel.deb /tmp/alsa-arm
find /tmp/alsa-arm -name 'libasound.so*'
```

**部署到板子（库 + 配置树）**

`libasound` 打开 `default` 还依赖 `/usr/share/alsa/alsa.conf` 及其 include 文件；
板端默认没有这个目录，**只拷 .so 不够**。

VM 侧 staging（把库、配置树、程序打成一个包）：

```bash
cd ~/alsa-lib-1.1.3
mkdir -p ~/stage/usr/{lib,share} ~/stage/bin
cp install/usr/lib/libasound.so.2.0.0 ~/stage/usr/lib/
ln -sf libasound.so.2.0.0 ~/stage/usr/lib/libasound.so.2
cp -a install/usr/share/alsa ~/stage/usr/share/
cp ~/gec6818/build/gesture_audio ~/stage/bin/
cd ~/stage && tar czf gapp.tgz .
```

板端两种铺法：

```bash
# A. 根文件系统可写：直接铺进去
cd /tmp && wget http://<VM_IP>:8000/gapp.tgz && tar xzf gapp.tgz -C /
./bin/gesture_audio --selftest

# B. 根文件系统只读：放 /tmp，用环境变量
mkdir -p /tmp/gapp && tar xzf gapp.tgz -C /tmp/gapp
export LD_LIBRARY_PATH=/tmp/gapp/usr/lib
export ALSA_CONFIG_PATH=/tmp/gapp/usr/share/alsa/alsa.conf
/tmp/gapp/bin/gesture_audio --selftest
```

> 板端跑 `ldd ./gesture_audio` 或 `LD_DEBUG=libs` 可确认加载的是哪一份 libasound；
> `default` 打不开时先查 `ALSA_CONFIG_PATH`，再试 `hw:0,0`。

### 3.4 GPIO

**四种方式，按优先级选**：

| 方式 | 适用 | 权限 | 稳定性 |
|---|---|---|---|
| ① gpio-leds sysfs | 板载 LED / 蜂鸣器（已被内核注册） | root | 最稳，推荐 |
| ② gpio sysfs (`/sys/class/gpio`) | 扩展口自由引脚、中断输入 | root | 稳，慢（每次读写都走文件系统） |
| ③ 直接写寄存器 (mmap `/dev/mem`) | 需要微秒级翻转 | root | 快，但可能与驱动打架 |
| ④ 写内核驱动 | 需要严格资源管理 | - | 本项目不必 |

**① 板载 LED / 蜂鸣器（首选，零配置）** ✅

```bash
ls /sys/devices/platform/leds/leds/          # 列出 led1..led4、beep
echo 1 > /sys/devices/platform/leds/leds/beep/brightness   # 响
echo 0 > /sys/devices/platform/leds/leds/beep/brightness   # 停
```

C 里就是 `open/write`，无需 mmap。注意亮度值语义受 `GPIO_ACTIVE_LOW` 影响，**先手动试 0/1 确认极性**再写死代码。

**② gpio sysfs**

```bash
# 编号 = bank*32 + pin（A=0,B=1,C=2,D=3,E=4），但内核 gpio base 可能非 0 ⚠️
cat /sys/kernel/debug/gpio        # 用这个表核对编号，不要凭公式硬算
echo 74 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio74/direction
echo 1   > /sys/class/gpio/gpio74/value

# 中断输入
echo in   > /sys/class/gpio/gpio74/direction
echo both > /sys/class/gpio/gpio74/edge
```

中断必须用 `poll()` 监听 `POLLPRI`（不是 `POLLIN`），且每次返回后 `lseek(fd,0,SEEK_SET)` 再读，否则读不到新值。

**③ 寄存器级（S5P6818 GPIO 控制器）** ✅

每个 bank 占 0x1000，基地址 `0xC001A000 + bank*0x1000`（A=0 … E=4，共 5×32 = 160 个 GPIO）：

| 偏移 | 寄存器 | 作用 |
|---|---|---|
| 0x00 | GPIOxOUT | 输出电平（1=高） |
| 0x04 | GPIOxOUTENB | 输出使能（1=输出，0=输入） |
| 0x08 | GPIOxDETMODE | 边沿/电平检测模式 |
| 0x10 | GPIOxINTENB | 中断使能 |
| 0x14 | GPIOxDET | 事件检测状态 |
| 0x18 | GPIOxPAD | **引脚电平（读这里）** |
| 0x1C | GPIOxPUENB | 上拉使能 |
| 0x20 | GPIOxALTFN0 | 引脚 0~15 功能选择（每引脚 2 bit） |
| 0x24 | GPIOxALTFN1 | 引脚 16~31 功能选择 |
| 0x28 | GPIOxDETMODEEX | 扩展检测模式 |
| 0x3C | GPIOxDETENB | 检测使能 |
| 0x40 | GPIOxSLEW | 压摆率 |
| 0x48 / 0x50 | GPIOxDRV1 / DRV0 | 驱动能力 |
| 0x58 | GPIOxPULLSEL | 上/下拉选择 |
| 0x60 | GPIOxPULLENB | 上/下拉使能 |

```c
#define GPIO_BASE(bank) (0xC001A000u + 0x1000u * (bank))   /* A=0 … E=4 */
#define R_OUT 0x00
#define R_OUTENB 0x04
#define R_PAD 0x18
#define R_ALTFN0 0x20
#define R_ALTFN1 0x24

/* 把引脚配置为 GPIO 功能（注意：多数引脚 GPIO 是 Function1，不是 0！） */
static void altfn_to_gpio(volatile unsigned int *g, int pin)
{
    volatile unsigned int *reg = g + (pin < 16 ? R_ALTFN0 : R_ALTFN1) / 4;
    int shift = (pin & 15) * 2;
    unsigned int v = *reg;
    v &= ~(3u << shift);
    v |=  (1u << shift);        /* 01 = Function1；具体值必须查手册 Function Table */
    *reg = v;
}
```

**⚠️ 最大的坑：GPIO 不一定是 Function0。**
例如 GPIOC12 的 `GPIOCALTFN0[25:24] = 01` 才是 I/O 功能。**每个引脚都要查 S5P6818 用户手册的 I/O Function Table**，凭"0 就是 GPIO"的直觉写必然失灵。

**⚠️ 寄存器操作的纪律**：

- `/dev/mem` mmap 需要 root；先 `mmap` 整个 0x5000 覆盖 A~E 五个 bank，别每个引脚映射一次
- **只碰原理图上确认"空闲"的引脚**。LCD 背光 PWM、触摸中断、I2S、以太网 PHY 的引脚都被内核驱动占着，抢过来会让屏幕/声音直接失效
- 写寄存器一律"读-改-写"，不要整字覆盖（同一寄存器管着 16 个引脚）
- 调试期用 `devmem` 单步验证，确认地址无误再写进代码

### 3.5 I2C（手势/测距传感器）

```bash
i2cdetect -l          # 列出总线 → /dev/i2c-N
i2cdetect -y 1        # 扫描总线 1 上的从机地址（0x08~0x77）
i2cget -y 1 0x39 0x00           # 读寄存器
i2cset -y 1 0x39 0x00 0x01      # 写寄存器
```

若镜像没有 i2c-tools：交叉编译 i2c-tools 静态版拷到 `/usr/sbin`，或自己用下面的 ioctl 写个小工具。

```c
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

/* 组合读：写寄存器地址 + 读数据，中间不发 STOP（传感器必备） */
int i2c_read_reg(int fd, unsigned char addr, unsigned char reg,
                 unsigned char *buf, int len)
{
    struct i2c_msg msg[2] = {
        { .addr = addr, .flags = 0,        .len = 1,   .buf = &reg },
        { .addr = addr, .flags = I2C_M_RD, .len = len, .buf = buf  },
    };
    struct i2c_rdwr_ioctl_data d = { .msgs = msg, .nmsgs = 2 };
    return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}
```

**选型与接线纪律**：

- 传感器必须 **3.3V 供电 + 3.3V 逻辑**，5V 器件要电平转换，直连可能烧 IO
- 地址是 **7 位**（`0x39` 而非 `0x72`），7 位地址左移一位才是写地址
- SDA/SCL 需要上拉（模块板通常自带 4.7k，裸芯片要自己加）
- **不要盲扫整条总线**：触摸屏 gslx680 挂在 I2C 上，某些器件被误探测会进入异常状态
- 推荐器件（都有成熟 Linux 用户态驱动可抄）：APDS-9960 / PAJ7620（手势）、VL53L0X（激光测距）、MPU6050（姿态）

### 3.6 UART（串口类传感器）

- 可用：`/dev/ttySAC1`、`/dev/ttySAC2` ⚠️ 与底板 COM1/COM2 的对应关系实测确认
- **绝对不要打开 `/dev/ttySAC0`**，那是调试 console，占用后你的终端会乱、甚至登录不上

```c
#include <termios.h>
#include <fcntl.h>

int uart_open(const char *dev, int baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    struct termios tio;
    if (fd < 0) return -1;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);                       /* 原始模式：不做任何字符处理 */
    cfsetispeed(&tio, B9600);              /* 按传感器规格 */
    cfsetospeed(&tio, B9600);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;               /* 关硬件流控 */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 10;                  /* 1s 超时，避免永久阻塞 */
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return fd;
}
```

**坑**：TTL 3.3V 不能直连 RS232（±12V 会烧）；必须共地；收包要写状态机（帧头/长度/校验），不要假设 `read` 一次拿到完整帧。

### 3.7 PWM 与 ADC

| 资源 | 现状 | 结论 |
|---|---|---|
| PWM | LCD 背光用 PWM（40Pin 第 1 脚），由内核占用 ⚠️ | 不要抢；需要 PWM 输出得另找引脚并自己配 PWM 寄存器 |
| 蜂鸣器 | **GPIO 直驱，不是 PWM** ⚠️（§1.2 同一项也未定；另有资料称是 PWM 引脚复用为 GPIO，待 §8.D 实测） | 只能确认开关，别指望调频发声 |
| ADC | S5P6818 内置 ADC，但 GEC6818 是否引出通用节点未确认 ❓ | **不要把方案建立在 ADC 上** |

> 对本项目的直接影响：`projectBrief.md` 里"超声波测距（ADC）"这条路径**不可靠**。改用
> ① I2C 手势传感器（首选，数字量、协议成熟）
> ② GPIO 触发 + 回响计时的超声波（HC-SR04 类，纯 GPIO，无需 ADC）
> ③ UART 传感器（GY-39 等，模块自带 MCU，输出数字帧）
> 三条都不依赖 ADC，风险可控。

---

## 4. 性能与资源边界

### 4.1 算力

| 项目 | 值 | 工程含义 |
|---|---|---|
| CPU | 8 × Cortex-A53 @1.4GHz | 8 核但共享单通道 DDR3，**内存带宽才是真瓶颈** |
| 指令集 | AArch32 + NEON | 可写 NEON intrinsics，但先证明标量版不够快 |
| 浮点 | 硬件 FPU（VFPv4） | `float` 可用；但音频 DSP 建议定点，省电且确定性好 |

**音频 DSP 预算（估算，需实测）**：44.1kHz 立体声 = 88,200 采样/秒。单核 1.4GHz 全部给它也只有 ~15,800 cycles/采样。保守按单核 20% 占用算 ≈ 3,000 cycles/采样，足够做：

- 回声（环形缓冲 + 一次乘加）：~10 cycles/采样
- 三段 EQ（3 个双二阶 biquad，直接 I 型）：~30 cycles/采样
- 线性插值重采样：~15 cycles/采样
- 合计 < 100 cycles/采样，**余量两个数量级**

结论：**回声 + EQ + 变速毫无压力**。真正的风险在相位声码器级变调（FFT + 重叠相加），那是另一个量级，见 §3.3。

### 4.2 内存

```bash
free                  # 看可用内存
df -h                 # 看 eMMC / tmpfs 余量
```

- 一帧后备缓冲 1.5MB；音频环形缓冲按 2 秒算 44,100 × 2ch × 2B × 2s ≈ **345KB**（V1.0 写的 700KB 是按 4 秒算的量，已更正），都很小
- 大音频素材放 `/mnt/nfs` 或 `/userdata`，**别塞根分区**（写满根分区会启动失败）
- 内存不是约束，**别为省内存牺牲代码可读性**

### 4.3 端到端延迟预算（手势 → 出声）

| 环节 | 预算 | 手段 |
|---|---|---|
| 传感器采样 | 10~20ms | I2C 100kHz 读一次寄存器 <1ms，主要开销在轮询周期 |
| 滤波 + 手势判定 | 0~20ms | 滑动平均窗口 ≤ 5 帧 |
| 参数下发（消息队列/共享内存） | <1ms | 不要用 socket 在本机传参数 |
| 效果链处理 | <5ms | 见 §4.1 |
| ALSA 缓冲 | 46~93ms | period 512 / buffer 2048 |
| **合计** | **~70~140ms** | 满足 ≤300ms，手感"跟手" |

调优顺序：先把 ALSA 缓冲压到 93ms 以内验证不爆音，再优化传感器轮询周期。**不要一上来就调 period=128**，先把功能跑通。

### 4.4 实测方法

板上没有 perf，用 `clock_gettime(CLOCK_MONOTONIC)` 打点：

```c
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
```

记录每个环节耗时，打印到串口/日志，**用数据说话而不是"感觉有点卡"**。

---

## 5. 与本项目模块的映射

| 模块 | 板端接口 | 主要风险 | 备选方案 |
|---|---|---|---|
| `sensor.c` | I2C（手势）/ GPIO（超声波）/ UART（GY-39） | 引脚与总线编号未确认；ADC 不可用 | 先按 §3.5 扫描总线，选 I2C 数字传感器 |
| `player.c` | ALSA + libmad | 解码与效果抢 CPU；设备独占 | 先只用 WAV 跑通管线，MP3 后加 |
| `effect.c` | 纯 CPU DSP | 变调算法复杂度 | v1 只做变速（音调随动） |
| `display.c` | `/dev/fb0` + evdev | 撕裂、字库、事件号不固定 | 双缓冲 + 脏矩形；点阵字模 |
| `server.c` | TCP socket | 端口占用、断线重连 | 端口 8888，加心跳与重连 |
| `main.c` | pthread + 共享内存 + 消息队列 | 并发仲裁（手势 vs 远程） | 单写者原则：所有播放参数只由主控线程改 |

**并发仲裁的设计要点**（`projectBrief.md` §2 要求"双通道并存 + 并发仲裁"）：

- 手势与 TCP 都是**参数生产者**，播放引擎是**唯一消费者**
- 生产者只投递"参数变更消息"（消息队列），不直接调 ALSA
- 冲突策略要明确写进代码注释：例如"远程控制优先，手势在其后 3s 内降权"，避免两边打架导致音量抖动

---

## 6. 上板 15 分钟自检清单

新板/重装系统/换人接手，按顺序执行，**每条都记录输出**（贴进 §8.D）。

| # | 命令 | 期望结果 | 不通过怎么办 |
|---|---|---|---|
| 0 | SecureCRT 连 COM6，敲回车 | 出现 `[root@GEC6818 /]#` | 按 §2.2.3 三段判据查：独占 → 供电 → 线序 → 波特率/流控 |
| 1 | `uname -a` | 含 `3.4.39`，armv7l | 镜像不对，重烧 |
| 2 | `cat /proc/cpuinfo \| grep -c processor` | `8` | 内核配置异常 |
| 3 | `free` | 总内存 ~1GB | 内存参数错误 |
| 4 | `cat /proc/cmdline` | 含 `lcd=vs070cxn tp=gslx680-linux` | 改 u-boot bootargs |
| 5 | `ls /dev/fb0` | 存在 | 显示驱动未加载 |
| 6 | `fbset` | `800x480`，32bpp | 屏参错误 |
| 7 | `cat /proc/bus/input/devices` | 有 gslX680 / touch | 触摸驱动问题 |
| 8 | `evtest /dev/input/eventN` | 点屏幕有 ABS 事件 | 换 event 号再看 |
| 9 | `aplay -l` | 列出 card 0 播放设备 | 声卡驱动未加载 |
| 10 | `speaker-test -D hw:0,0 -c2 -t sine -l 1` | 喇叭/耳机出声 | 查 amixer 静音与通路 |
| 11 | `ifconfig eth0` + `ping 集成机` | 通 | 桥接/网段/网线 |
| 12 | `mount -t nfs -o nolock,vers=3 ...` | 挂载成功 | 检查 exports 与防火墙 |
| 13 | `ls /sys/devices/platform/leds/leds/` | 有 beep、ledN | 内核 LED 配置被裁 |
| 14 | `i2cdetect -l` | 列出总线 | 无 i2c-tools，自行编译 |
| 15 | 运行交叉编译的 `hello` | 打印正常 | **浮点 ABI 不匹配**，见 §2.5 |

第 15 条最容易翻车。`hello` 报 `No such file or directory` 但文件确实存在 = 动态链接器路径或 ABI 问题，先 `readelf -A` 对比，不要怀疑代码。

---

## 7. 故障排查

| 现象 | 最可能原因 | 验证 | 处理 |
|---|---|---|---|
| 串口全是乱码 | 波特率/流控 | 换 115200 8N1，关流控 | 重设终端 |
| 上电无任何输出 | 供电不足 / 串口线序 | 换 5V/2A 电源，TX-RX 交叉 | 换线换电源 |
| 终端全空白，回车无反应 | SecureCRT 没真正 Connect / 板子没上电 / TX-RX 没接 | 另开 PowerShell 试开 COM6，看是否「拒绝访问」 | 重新 Connect，查供电与线序（§2.2.3） |
| `对端口 COM6 的访问被拒绝` | 串口被另一个终端占着（多开的 SecureCRT / 串口助手） | `Get-Process SecureCRT`、关掉多余会话 | 同一时刻只留一个连接者 |
| 屏幕白屏/花屏 | bootargs 屏参错 / 排线松 | `cat /proc/cmdline`，`fbset` | 改 `lcd=`，重插排线 |
| 程序跑不起来 `No such file` | 架构/ABI/解释器不符 | `readelf -l \| grep interpreter`、`ls /lib/ld-linux*` | 换与板端一致的软浮点工具链（`arm-linux-gcc`），或 `-static` |
| `GLIBC_2.xx not found` | 编译器版本过高 | `readelf --version-info` | 降到厂商 5.4.0 重编 |
| 打开 `/dev/fb0` 失败 | 权限 / 驱动 | `ls -l /dev/fb0`，`dmesg` | 用 root，查 fb 驱动 |
| 屏幕撕裂/闪烁 | 直接写显存 | 目视 | 加后备缓冲 + 整屏提交 |
| 触摸点不准 | 坐标未缩放 / 轴反向 | `evtest` 点四角 | 按 max 缩放，交换/取反 |
| 触摸无反应 | event 号变了 | `cat /proc/bus/input/devices` | 按名字匹配设备 |
| 无声 | 静音 / 通路 / 设备占用 | `amixer`、`aplay -l`、`ps` | 调 amixer，杀占用进程 |
| 播放断续/爆音 | buffer 太小 / CPU 抢占 | 看 underrun 日志 | 加大 buffer，降 period 频率 |
| `Device or resource busy` | 声卡被别的进程占 | `ps \| grep -E "madplay\|qt"` | 保证单一 owner |
| 网口 ping 不通 | 虚拟机 NAT / 网段不同 | `ip addr` | 改桥接，同网段静态 IP |
| NFS 挂载卡死 | 缺 `nolock,vers=3` | - | 补参数重挂 |
| 重启后网络配置丢失 | 未固化 | - | 写 rcS/profile |
| GPIO 写了没反应 | 引脚功能未切 / 被驱动占用 | 读 `PAD` 与 `/sys/kernel/debug/gpio` | 改 ALTFN，换引脚 |
| 写 GPIO 后屏幕异常 | 抢了 LCD/背光引脚 | 对照原理图 | 立即换引脚，别硬扛 |
| 时间不准 | RTC 无后备电池 | `date` | 上电后 NTP 或手工设 |

---

## 8. 附录

### 8.A 命令速查

```bash
# 系统
uname -a; cat /proc/cmdline; free; df -h; dmesg | tail -30

# 显示
fbset; cat /sys/class/graphics/fb0/virtual_size
cat /dev/urandom > /dev/fb0          # 花屏自测（会刷满屏，慎用）

# 触摸/输入
cat /proc/bus/input/devices; evtest /dev/input/event0

# 音频
cat /proc/asound/cards; aplay -l; amixer scontrols; amixer sset 'Playback' 80%
speaker-test -D hw:0,0 -c2 -t sine -l 1

# GPIO
ls /sys/class/leds/; ls /sys/devices/platform/leds/leds/
cat /sys/kernel/debug/gpio
echo 74 > /sys/class/gpio/export; cat /sys/class/gpio/gpio74/value

# I2C
i2cdetect -l; i2cdetect -y 1; i2cget -y 1 0x39 0x00

# 网络
ifconfig eth0 192.168.1.100 netmask 255.255.255.0 up
mount -t nfs -o nolock,vers=3 192.168.1.10:/home/gec6818/nfs /mnt/nfs

# 进程与资源
ps; top -n 1; killall <名字>
```

### 8.B 关键地址与参数

| 名称 | 值 |
|---|---|
| GPIO bank 基址 | `0xC001A000 + bank*0x1000`（A=0…E=4） |
| 内核加载地址 | `0x48000000` |
| 根分区 | `/dev/mmcblk0p2`（ext4） |
| 屏参 bootargs | `lcd=vs070cxn tp=gslx680-linux` |
| 帧缓冲大小 | 800×480×4 = 1,536,000 B |
| 调试串口 | UART0（板端 `/dev/ttySAC0`）/ 115200 8N1 / 无流控 / PC 侧 **COM6（CH340，2026-11-11 实测）** |

### 8.C 参考资料

| 资料 | 用途 |
|---|---|
| 《S5P6818 用户手册》(SEC_S5P6818X_Users_Manual) | GPIO/I2C/PWM/ADC 寄存器与 I/O Function Table，**唯一权威** |
| 《GEC6818 开发板硬件手册》+ 原理图 | 引脚连接、外设接线、按键/LED 编号 |
| 粤嵌配套《Linux 开发指南》 | 镜像烧写、环境搭建 |
| 内核源码 `arch/arm/mach-s5p6818/` | GPIO 寄存器结构体、板级引脚配置 |
| `projectBrief.md` / `devFlow.md` | 项目需求与协作纪律（本文不重复） |

### 8.D 待实测确认清单（团队填表）

> 规则：谁实测谁填，填完把正文对应位置的 ⚠️ 改成 ✅。这张表是本文档的"未完成部分"。

| 项 | 待确认内容 | 实测命令 | 结论 | 确认人/日期 |
|---|---|---|---|---|
| 串口端口与适配器 | PC 侧口号、转接芯片型号、驱动是否正常 | `[System.IO.Ports.SerialPort]::GetPortNames()`；`Get-CimInstance Win32_PnPEntity` | **COM6 / USB-SERIAL CH340（VID_1A86&PID_7523，驱动 CH341SER_A64，Status=OK）** ✅ | 成员机 2026-11-11 |
| 串口能否登录 | 115200 8N1 无流控下是否出现 `[root@GEC6818 /]#` | SecureCRT 敲回车 | 端口被 SecureCRT 独占（判据成立）；**板端当前零回包，待确认上电与线序** ⚠️ | 成员机 2026-11-11 |
| 工具链 ABI | 板上是硬浮点还是软浮点 | `readelf -A /bin/busybox` | | |
| 触摸 event 号 | gslx680 对应 `/dev/input/event?` | `cat /proc/bus/input/devices` | | |
| 触摸坐标范围 | ABS_X/ABS_Y 的 maximum，是否需反向 | `evtest` | | |
| LED 名称 | `led1..led4` 实际名称与极性 | `ls /sys/devices/platform/leds/leds/` | | |
| 蜂鸣器极性 | 写 1 响还是写 0 响 | 手工 echo 试 | | |
| I2C 总线 | 扩展口对应 `/dev/i2c-N`，已占用地址 | `i2cdetect -l/-y N` | | |
| 串口映射 | ttySAC1/2 与 COM1/COM2 对应 | 短接 TX-RX 回环 | | |
| 按键输入 | K1~K4 是否注册为 input 设备 | `evtest` | | |
| 声卡名 | `aplay -l` 显示 wm8960 还是 S5P6818 | `aplay -l` | | |
| 支持采样率 | 44.1k / 48k 是否原生 | `aplay` 试播 | | |
| 实时性能 | 效果链 CPU 占用与最小可用 period | 自研打点程序 | | |
| 内存余量 | 空闲内存、根分区余量 | `free`、`df -h` | | |

---

## 9. 一页速记（贴在调试位墙上）

1. 串口 **115200 8N1 无流控**，PC 侧当前是 **COM6 / CH340 / SecureCRT 会话 `Serial-COM6`**；先接串口再上电，而且先查口号、别照抄。
2. 屏 **800×480×32bpp**，`0x00RRGGBB`，双缓冲再提交。
3. 触摸**按名字找 event**，坐标**必须缩放**。
4. 声卡 `card 0`，**单一 owner**，`buffer 2048 / period 512` 起步。
5. GPIO 优先用 `/sys/devices/platform/leds/leds/`，动寄存器前先查 Function Table。
6. **ADC 不可依赖**，传感器走 I2C / UART / GPIO。
7. 编译用厂商 **5.4.0 `arm-linux-gcc`（软浮点，`ld-linux.so.3`）**；不确定就 `ls /lib/ld-linux*` 对一下。
8. 板子是独占资源，上板先报备（`devFlow.md` §3）。
