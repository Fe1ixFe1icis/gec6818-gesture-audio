# v0.2.0「初鸣」· First Voice

| 项 | 值 |
|---|---|
| 版本号 | `0.2.0` |
| 版本名称 | 初鸣（First Voice） |
| 发布标签 | `v0.2.0`（annotated tag） |
| 发布日期 | 2026-11-13 |
| 发布分支 | `dev` → `main`（merge commit，见 `gitGuide.md §9.2`） |
| 代码基线 | `include/config.h` 的 `APP_VERSION = "0.2.0"` |
| 定位 | **首个进入 main 的可演示版本** |

> **一句话**：把「能出声」变成「能听」——M1 框架收口，音质链 19 项指标全过，PC 客户端与协议矩阵主机侧闭环，板端真机出声且 30 秒 0 underrun。

---

## 1. 这一版包含什么

### 1.1 M1 基础框架（11-11）
- 12 个公共头文件（接口冻结候选）+ 9 个板端源文件：`log` / `ctrl` / `effect` / `player` / `sensor` / `display` / `server` / `main` / `selftest`。
- 构建：`Makefile` 双配置（`BOARD=0` 主机模拟 / `BOARD=1` 板端 ALSA），`make tools` / `make selftest` / `make audio_quality` / `make arm` / `make verify`。
- 并发仲裁：远程命令后 3000ms 内手势降权 + 同源同类 150ms 去抖，集中在 `main.c` 一处。
- 工程规范：`AGENT.md`（AI 不代执行 git）、`agentLogs/`（L0–L4 留痕）、`gitGuide.md`、`testGuide.md`（T0–T7）。
- 缺陷修复：D1（EOF 后切歌停摆）/ D2（PAUSE 不上报）/ D3（坏文件状态不报错）——`player.c` 状态机收敛。

### 1.2 M5Q 音质链（11-12 ~ 11-13）
| 任务 | 做法 | 结果 |
|---|---|---|
| M5Q-01 抗混叠 | 线性插值 → 窗口 sinc（Blackman，64 抽头 × 256 相位，`step>1` 先低通再抽取） | 混叠抑制达标 |
| M5Q-02 EQ 平滑 | 30ms 增益斜坡 + 切系数保留 `z1/z2` | 切换无 click |
| M5Q-03 回声 | 800ms 预分配、20ms 抽头交叉淡化、4kHz 反馈阻尼、wet/dry、反馈软限幅 | 回声逐次变暗、不自激 |
| M5Q-04 软限幅 | `abs(x) ≤ 0.9FS` 线性直通，之上 `g=t/(1+t)` 渐近饱和 | 满幅硬削波归零 |
| M5Q-05 高通 | 21Hz 二阶 Butterworth（RBJ） | 亚声/直流阻断 |
| M5Q-06 EQ 频点/预设 | 120Hz 低架 / 1kHz 峰 Q0.7 / 6kHz 高架；standard·vocal·demo 三预设，30ms 交叉淡化 | 三段可听辨、无染色 |
| M5Q-07 离线尺子 | `tools/audio_quality.c`：正弦/扫频/脉冲 + Goertzel + FFT，一键出报告、退出码=失败项数 | 改前/改后可量化 |
| M5Q-10 变调 | `src/pitch.c`：时域颗粒移调（双读头 + 互补三角交叉淡化），±12 半音，默认旁通 | 偏差 < 20 音分 |

### 1.3 M6 主机侧（11-12）
- `client/gesture_client.py`：Tkinter GUI + CLI，2s 心跳、0.5→5s 指数退避重连、500ms 状态面板、音量/速度滑杆防抖。
- `tools/protocol_matrix.py`：全指令与越界、STATUS 字段、500ms 推送、4 连接上限与槽位释放、半包/粘包/超长行/QUIT、断线重连。
- 顺带修真实缺陷：`server.c` 的 STATUS 在 EOF/STOPPED/ERROR 下返回旧参数（矩阵测试抓到）。

### 1.4 板端适配与部署
- ABI 实测：**软浮点**（`/lib/ld-linux.so.3`）+ glibc **2.23**；`armbuild.sh` 按 loader 自动判定工具链前缀。
- 板端**无 libasound**：自带交叉编译的 `libasound.so.2.0.0` + `/usr/share/alsa` 配置树，ALSA 设备回退 `default` → `plughw:0,0` → `hw:0,0`。
- 部署：`/root/gapp` 持久化、只换二进制（strip+gzip→原子 `mv`）、`VERSION` 对账与回滚；无网络，走 TF 卡 / U 盘 / `rx·rb·rz` / base64 兜底。

---

## 2. 量化指标（主机侧，`make audio_quality`）

| 指标 | 结果 | 判据 |
|---|---|---|
| 混叠抑制 1k @2.0x | −82.5 dBc | < −40 |
| 混叠抑制 8k @2.0x | −64.5 dBc | < −40 |
| 上奈奎斯特抑制 15k | −97.9 dBFS | < −40 |
| THD+N @1kHz | 0.0083 % | < 1 % |
| 满幅 + EQ +12dB 硬削波 | 0 / 88200 样本 | = 0 |
| EQ 切换 click | −66.2 dBFS | < −60 |
| 回声阻尼 | −6.3 dB/遍 | < −3 |
| 90% 反馈自激 | 不自激，末段峰值 0 | 无硬削 |
| 10Hz 衰减 | −13.1 dB | < −12 |
| 直流残留 | 0 LSB | ≤ 1 |
| 变调 ±12 半音偏差 | 最大 16.1 音分 | < 20 |
| 离线用例总数 | **19 / 19 PASS** | 全过 |

> 注：11-13 用 MinGW gcc 13.2 复跑时，`alias_1k_2x` / `alias_8k_2x` 为 −76.3 / −58.3 dBc、`thd_1k` 0.0149%，与上表（11-12 记录）略有差异但同向达标，疑为编译器/浮点实现差异；换测量机时请固定同一编译器再比对。

**其他证据**：主机自检 19 通过 / 0 失败 / 1 跳过；协议矩阵 60 通过 / 0 失败（推送实测 537–563ms）；客户端链路 7/7；GUI smoke PASS；D1–D3 回归 0 失败。

**板端实测**：自检 20/20；`framebuffer 就绪 800x480 line=3200`；`ALSA 设备 plughw:0,0`、`rate=44100 period=512 buffer=2050`；**30 秒 underrun 0**；耳机出声，手势音量/变速/切歌/暂停生效。

---

## 3. 已验证 / 未验证（如实）

**已验证**
- 主机：编译零警告、自检 19 项、离线音质 19 项、协议矩阵 60 项、客户端链路 7 项。
- 板端：T0 部署、T1 自检、T2.3 framebuffer、T3.2 出声 + 30s und=0。
- 工具链：VM 5.4.0 交叉编译 + alsa-lib 1.1.3 自编，产物走 `plughw`。

**未验证（本版不承诺）**
- **M5Q-08 真机 A/B 听感**（耳机盲听、三套 EQ 预设、变调可选开）——需上板。
- **M6-03 板端回环**、**M6-04 远程 TCP**——受 eth0 阻塞。
- **D1–D3 板端复测**、T3.3/T3.4 EOF 实测、T0 `readelf` 原文归档。
- **M5Q-09 CPU/延迟回归**（300s 长跑）。
- 变调在板端的实际听感与 CPU 占用。

---

## 4. 已知限制

1. **eth0 PHY 全 0**（`Cannot attach to PHY -19`）：无网络，NFS/scp/远程 TCP 全部不可用，只能串口或 TF 卡部署。
2. **板端无 libasound**：必须随包带 `libasound.so.2.0.0` 与 `/usr/share/alsa`，否则 `snd_pcm_open` 失败。
3. **变调为 granular 时域算法**：有轻微 warble（时域侧带），默认关闭（`--pitch 0`），仅演示/彩排可选。
4. **LCD 仅 ASCII 点阵**：中文字库未做（M4）；触摸与映射表未接线。
5. **传感器未上板**：当前 `--sensor sim` 模拟后端；M2 选型建议 PAJ7620U2，待到货（`sensorSelection.md`）。
6. **无 MP3 解码**：仅 WAV（PCM 16bit）。
7. **串口 5KB/s**：17MB 素材实测 53 分钟，演示素材请用裁剪版（`tools/prep_demo_wav.py`）。

---

## 5. 兼容性

- **板端协议未变**：`HELP / PLAY / PAUSE / RESUME / STOP / NEXT / PREV / VOL / SPEED / ECHO / EQ / STATUS / QUIT` 语义与格式不变，旧客户端可继续使用。
- **新增仅命令行选项**（不进协议、不动冻结接口）：`--eq-preset standard|vocal|demo`、`--pitch <半音>`。
- 配置/参数范围未变（音量 0–100、速度 0.5–2.0、回声延迟/反馈、EQ ±12dB）。
- 客户端需要 Python 3.8+ 与 tkinter（`py -3.11` 自带）。

---

## 6. 部署、升级与回滚

```bash
# 1) 打包（VM/Ubuntu 侧）：二进制 + alsa 运行库 + 音频
mkdir -p ~/stage/{bin,usr/lib,usr/share,audio}
cp build/gesture_audio ~/stage/bin/
cp ~/alsa-lib-1.1.3/install/usr/lib/libasound.so.2.0.0 ~/stage/usr/lib/
ln -sf libasound.so.2.0.0 ~/stage/usr/lib/libasound.so.2
cp -a ~/alsa-lib-1.1.3/install/usr/share/alsa ~/stage/usr/share/
cd ~/stage && rm -f gapp.tgz && tar czf gapp.tgz .

# 2) 上板（无网络：TF 卡/U 盘/rz）
mkdir -p /root/gapp && cd /root/gapp && tar xzf gapp.tgz
LD_LIBRARY_PATH=/root/gapp/usr/lib ALSA_CONFIG_PATH=/root/gapp/usr/share/alsa/alsa.conf \
  ./bin/gesture_audio --sensor sim /root/gapp/audio/test_44100.wav

# 3) 回滚：保留上一版目录，切换即可
mv /root/gapp /root/gapp.bak-$(date +%m%d) && mv /root/gapp.prev /root/gapp
```

- **代码回滚**：在 `main` 上 `git revert -m 1 <merge-commit>`（禁止 `reset` + 强推，见 `gitGuide.md §9.5`）。
- **版本对账**：板端 `/root/gapp/VERSION` 记录 commit + 构建时间 + 二进制 sha256，出问题先比对。

---

## 7. 标签与校验命令（供操作人执行）

```bash
# 打 annotated tag（GitHub Release 也基于它）
git checkout main && git pull
git tag -a v0.2.0 -m "v0.2.0 初鸣 (First Voice)" \
  -m "首个进 main 的可演示版本：M1 框架 + M5Q 音质链（19/19 指标）+ M6 主机侧（60/60 协议矩阵）" \
  -m "板端：真机出声、30s und=0、fb 800x480、自检 20/20；软浮点 ABI / glibc 2.23" \
  -m "未完成：M5Q-08 听感、M6-03/04、D1-D3 板端复测、M2 传感器、M4 中文字库"
git push origin v0.2.0

# 校验
git describe --tags        # 预期：v0.2.0
git show v0.2.0 --stat | head -20
```

```bash
# 主机侧复跑（任意机器）
make audio_quality && ./build/audio_quality.exe --outdir build/aq_report   # 19/19
make && ./build/gesture_audio --selftest                                  # 19 通过 / 0 失败 / 1 跳过
```

---

## 8. 版本命名规则（约定）

- 形式：`MAJOR.MINOR.PATCH`，`0.x` 期间 **MINOR = 能力面增加**，**PATCH = 缺陷修复/文档**。
- `-mN` 后缀**只用于「里程碑 Gate 全过」的收口标签**（如 M1 的 G1–G6 全绿）；本版 Gate 未全过（G2 接口冻结、G4/G5 板端部分），因此**不带后缀**。
- 版本名（codename）一版一名，便于答辩叙述与口头对账：`v0.1.0-m1` 内部基线（未进 main）→ **`v0.2.0` 初鸣** → 后续 `v0.3.0`「掌音」（M2 手势接入）。
