# PC 客户端（M6-01）

`gesture_client.py` 是 GEC6818 手势音频播放器的 PC 端控制界面，协议与板端
`src/server.c` / `projectBrief.md §6` 完全一致。两种形态：

| 形态 | 启动 | 用途 |
|---|---|---|
| Tkinter GUI（默认） | `py -3.11 client/gesture_client.py` | 现场演示：按钮、滑杆、状态面板 |
| CLI | `py -3.11 client/gesture_client.py --cli ...` | 无桌面环境 / 自动化脚本 |

> 开发机没有 `python` 别名时用 `py -3.11`；Ubuntu 用 `python3`。

## 界面功能

- 连接/断开、主机与端口输入
- PLAY / PAUSE / RESUME / PREV / NEXT / STATUS / HELP
- 音量滑杆 0–100、速度滑杆 0.5–2.0x（拖动后 180ms 防抖再发）
- 回声：开关、延迟 50–800ms、反馈 0–90%
- 三段 EQ：low / mid / high，−12…+12dB
- 状态面板：播放状态、曲目、进度、音量、速度、回声、EQ、测距、手势、
  underrun、在线客户端数、队列长度；服务器每 500ms 推送一次
- 回执/事件日志（最多保留 200 行）

## 心跳与自动重连

服务器每 500ms 主动推一行 `STATUS`，客户端把它当心跳：

- 超过 `--heartbeat` 秒（默认 2.0）没有任何数据 → 判定链路失效，关 socket；
- 按 0.5s → 1s → 2s → 4s → 5s 指数退避重连；
- GUI 默认开启；`--cli --no-reconnect` 可关闭。

服务器最多 4 个客户端（`SERVER_MAX_CLIENTS`），第 5 个会收到
`ERR 连接数已满` 后断开。

## CLI 用法

```bash
# 一次性下发（默认 127.0.0.1:8888）
py -3.11 client/gesture_client.py --cli --cmd "VOL 70" --cmd "SPEED 1.2" --cmd "STATUS"

# 下发后继续看 2s 的周期状态
py -3.11 client/gesture_client.py --cli --cmd "ECHO on 250 40" --watch 2 --show-status

# 连板子
py -3.11 client/gesture_client.py --cli --host 192.168.1.100 --port 8888 --cmd STATUS

# 交互模式（空行退出）
py -3.11 client/gesture_client.py --cli
```

CLI 的 `STATUS` 回执本身也是 `STATUS ...` 行，客户端会把它当作该指令的回复，
其余周期推送只在 `--show-status` 或 `--watch` 时打印。

## 自动化验收

```bash
# 1) GUI 冒烟：连接真实服务器，收到 ≥1 条 STATUS 后自动退出
py -3.11 client/gesture_client.py --gui-smoke --host 127.0.0.1 --port 8888

# 2) 心跳/重连：不依赖板子，用 Python 假服务器
py -3.11 tools/client_link_test.py

# 3) 服务器协议矩阵（含 4 连接、500ms 推送、重连）
py -3.11 tools/protocol_matrix.py
```

`tools/netctl.py` 仍是**最轻量**的命令行工具；本客户端多了 GUI 与自动重连，
正式演示用 GUI，排障/脚本用 `netctl.py` 或本客户端的 `--cli`。
