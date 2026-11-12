#!/usr/bin/env python3
"""
gesture_client.py —— GEC6818 手势音频播放器 PC 客户端（M6-01）

两种界面：
  - 默认 Tkinter GUI：连接/断开、播放控制、音量/变速、回声、三段 EQ、
    500ms 状态刷新、断线自动重连。
  - `--cli`：命令行一次性下发或多条指令，适合无桌面环境与自动化测试。

协议与板端 server.c / projectBrief.md §6 完全一致：
  请求：PLAY PAUSE RESUME NEXT PREV VOL n SPEED f
        ECHO on|off delay fb  EQ low|mid|high gain  STATUS HELP QUIT
  应答：OK ... / ERR ...；STATUS 行由服务器每 500ms 主动推送。

心跳与重连：
  服务器每 500ms 推一行 STATUS；客户端超过 `--heartbeat` 秒没有任何数据
  就判定链路失效，关闭 socket 并按 0.5s→5s 指数退避重连。GUI 默认开启，
  `--cli --no-reconnect` 可关闭。

用法（Windows 开发机没有 `python` 别名时用 `py -3.11`）：
  py -3.11 client/gesture_client.py
  py -3.11 client/gesture_client.py --host 192.168.1.100 --port 8888
  py -3.11 client/gesture_client.py --cli --cmd "STATUS" --cmd "VOL 70"
  py -3.11 client/gesture_client.py --cli --watch 2
  py -3.11 client/gesture_client.py --gui-smoke --host 127.0.0.1

退出码：0 正常；1 有 ERR / 连接失败；2 用法错误；3 GUI 不可用。
"""
import argparse
import queue
import socket
import sys
import threading
import time

APP_TITLE = "GEC6818 手势音频客户端"


def fix_stdout():
    """Windows 控制台默认可能是 GBK；回执里有中文，能改就改 UTF-8。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def parse_status(line):
    """把 'STATUS vol=70 speed=1.00 ...' 解析成 dict（值保持字符串）。"""
    out = {}
    for tok in line.split()[1:]:
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def fmt_ms(ms):
    try:
        s = int(ms) // 1000
    except (TypeError, ValueError):
        return "--:--"
    return f"{s // 60:02d}:{s % 60:02d}"


def status_summary(st):
    return (f"{st.get('state', '?')}  {st.get('track', '?')}  "
            f"vol={st.get('vol', '?')}  speed={st.get('speed', '?')}  "
            f"echo={st.get('echo', '?')}  eq={st.get('eq', '?')}")


class ServerLink:
    """后台读线程 + 事件队列；GUI/CLI 都只消费事件，不直接碰 socket。"""

    def __init__(self, host, port, events, heartbeat=2.0, reconnect=True):
        self.host = host
        self.port = port
        self.events = events
        self.heartbeat = heartbeat
        self.reconnect = reconnect
        self._sock = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        with self._lock:
            if self._sock:
                try:
                    self._sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None
        if self._thread:
            self._thread.join(timeout=1.0)

    def send(self, line):
        with self._lock:
            if self._sock is None:
                raise ConnectionError("未连接")
            self._sock.sendall((line + "\n").encode("utf-8"))

    def _run(self):
        backoff = 0.5
        while not self._stop.is_set():
            sock = None
            try:
                sock = socket.create_connection((self.host, self.port), timeout=3.0)
                sock.settimeout(0.2)
                with self._lock:
                    self._sock = sock
                self.events.put(("connected", None))
                backoff = 0.5
                buf = b""
                last_rx = time.time()
                while not self._stop.is_set():
                    try:
                        chunk = sock.recv(4096)
                    except socket.timeout:
                        if time.time() - last_rx > self.heartbeat:
                            self.events.put(("stale", None))
                            break
                        continue
                    except OSError:
                        break
                    if not chunk:
                        break
                    last_rx = time.time()
                    buf += chunk
                    while b"\n" in buf:
                        raw, buf = buf.split(b"\n", 1)
                        text = raw.decode("utf-8", "replace").strip()
                        if text:
                            self.events.put(("line", text))
            except OSError as e:
                self.events.put(("error", str(e)))
            finally:
                with self._lock:
                    self._sock = None
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass
                self.events.put(("disconnected", None))

            if self._stop.is_set() or not self.reconnect:
                break
            self.events.put(("reconnecting", None))
            end = time.time() + backoff
            while time.time() < end and not self._stop.is_set():
                time.sleep(0.05)
            backoff = min(backoff * 2.0, 5.0)


# ------------------------------------------------------------------ GUI
def run_gui(args, smoke=False):
    try:
        import tkinter as tk
        from tkinter import ttk
    except Exception as e:                       # pragma: no cover
        print(f"GUI 不可用（缺少 tkinter）：{e}", file=sys.stderr)
        return 3

    try:
        root = tk.Tk()
    except Exception as e:                       # pragma: no cover
        print(f"无法创建窗口（无桌面？）：{e}", file=sys.stderr)
        return 3

    class App:
        def __init__(self):
            self.root = root
            self.events = queue.Queue()
            self.link = None
            self.status = {}
            self._pending = {}
            self._status_count = 0
            self._connected = False
            self._syncing = False
            self.exit_code = 0

            root.title(APP_TITLE)
            root.geometry("860x640")
            root.minsize(760, 560)
            self._build()
            self._poll()
            if smoke:
                root.after(2500, self._smoke_finish)
            else:
                root.protocol("WM_DELETE_WINDOW", self._on_close)
                self.connect()

        # ---------------- 界面 ----------------
        def _build(self):
            pad = {"padx": 6, "pady": 4}

            top = ttk.Frame(self.root)
            top.pack(fill="x", **pad)
            ttk.Label(top, text="主机").pack(side="left")
            self.host_var = tk.StringVar(value=args.host)
            ttk.Entry(top, textvariable=self.host_var, width=16).pack(side="left", padx=(4, 10))
            ttk.Label(top, text="端口").pack(side="left")
            self.port_var = tk.StringVar(value=str(args.port))
            ttk.Entry(top, textvariable=self.port_var, width=7).pack(side="left", padx=(4, 10))
            self.connect_btn = ttk.Button(top, text="连接", command=self.connect)
            self.connect_btn.pack(side="left")
            self.conn_var = tk.StringVar(value="未连接")
            self.conn_label = ttk.Label(top, textvariable=self.conn_var, foreground="#b00020")
            self.conn_label.pack(side="left", padx=12)
            ttk.Button(top, text="退出", command=self._on_close).pack(side="right")

            info = ttk.LabelFrame(self.root, text="状态（服务器每 500ms 推送）")
            info.pack(fill="x", **pad)
            self.vars = {}
            keys = [("state", "播放状态"), ("track", "曲目"), ("pos", "进度"),
                    ("vol", "音量"), ("speed", "速度"), ("echo", "回声"),
                    ("eq", "EQ"), ("dist", "测距"), ("gesture", "手势"),
                    ("und", "underrun"), ("clients", "在线"), ("q", "队列")]
            for i, (key, label) in enumerate(keys):
                r, c = divmod(i, 4)
                ttk.Label(info, text=label + "：").grid(row=r, column=c * 2, sticky="e", padx=4, pady=2)
                var = tk.StringVar(value="-")
                self.vars[key] = var
                ttk.Label(info, textvariable=var, width=20, anchor="w").grid(
                    row=r, column=c * 2 + 1, sticky="w", padx=4, pady=2)

            ctrl = ttk.LabelFrame(self.root, text="控制")
            ctrl.pack(fill="x", **pad)
            row = ttk.Frame(ctrl)
            row.pack(fill="x", padx=6, pady=4)
            for text, cmd in (("播放", "PLAY"), ("暂停", "PAUSE"), ("继续", "RESUME"),
                              ("上一首", "PREV"), ("下一首", "NEXT"),
                              ("查询", "STATUS"), ("帮助", "HELP")):
                ttk.Button(row, text=text, command=lambda c=cmd: self.send(c)).pack(side="left", padx=3)

            vol = ttk.Frame(ctrl)
            vol.pack(fill="x", padx=6, pady=2)
            ttk.Label(vol, text="音量", width=6).pack(side="left")
            self.vol_var = tk.IntVar(value=60)
            self.vol_scale = ttk.Scale(vol, from_=0, to=100, variable=self.vol_var,
                                       command=self._on_vol)
            self.vol_scale.pack(side="left", fill="x", expand=True, padx=6)
            self.vol_label = ttk.Label(vol, text="60", width=4)
            self.vol_label.pack(side="left")

            spd = ttk.Frame(ctrl)
            spd.pack(fill="x", padx=6, pady=2)
            ttk.Label(spd, text="速度", width=6).pack(side="left")
            self.spd_var = tk.IntVar(value=10)          # 0.5x..2.0x 用 x10 存
            self.spd_scale = ttk.Scale(spd, from_=5, to=20, variable=self.spd_var,
                                       command=self._on_speed)
            self.spd_scale.pack(side="left", fill="x", expand=True, padx=6)
            self.spd_label = ttk.Label(spd, text="1.0x", width=5)
            self.spd_label.pack(side="left")

            echo = ttk.Frame(ctrl)
            echo.pack(fill="x", padx=6, pady=2)
            self.echo_on = tk.BooleanVar(value=False)
            ttk.Checkbutton(echo, text="回声", variable=self.echo_on).pack(side="left")
            ttk.Label(echo, text="延迟(ms)").pack(side="left", padx=(12, 2))
            self.delay_var = tk.StringVar(value="250")
            ttk.Spinbox(echo, from_=50, to=800, increment=10, width=6,
                        textvariable=self.delay_var).pack(side="left")
            ttk.Label(echo, text="反馈(%)").pack(side="left", padx=(10, 2))
            self.fb_var = tk.StringVar(value="40")
            ttk.Spinbox(echo, from_=0, to=90, increment=5, width=6,
                        textvariable=self.fb_var).pack(side="left")
            ttk.Button(echo, text="应用回声", command=self._apply_echo).pack(side="left", padx=10)

            eq = ttk.Frame(ctrl)
            eq.pack(fill="x", padx=6, pady=2)
            ttk.Label(eq, text="EQ(dB)").pack(side="left")
            self.eq_vars = {}
            for band, cn in (("low", "低"), ("mid", "中"), ("high", "高")):
                ttk.Label(eq, text=cn).pack(side="left", padx=(8, 0))
                var = tk.IntVar(value=0)
                self.eq_vars[band] = var
                ttk.Spinbox(eq, from_=-12, to=12, increment=1, width=5,
                            textvariable=var).pack(side="left")
            ttk.Button(eq, text="应用 EQ", command=self._apply_eq).pack(side="left", padx=10)

            logf = ttk.LabelFrame(self.root, text="回执 / 事件")
            logf.pack(fill="both", expand=True, **pad)
            self.log = tk.Text(logf, height=8, wrap="none", state="disabled")
            self.log.pack(fill="both", expand=True, side="left")
            sb = ttk.Scrollbar(logf, command=self.log.yview)
            sb.pack(side="right", fill="y")
            self.log.configure(yscrollcommand=sb.set)

        # ---------------- 连接 ----------------
        def connect(self):
            if self.link is not None:
                self.disconnect()
                return
            host = self.host_var.get().strip() or "127.0.0.1"
            try:
                port = int(self.port_var.get())
            except ValueError:
                self._log("[错误] 端口必须是数字")
                return
            self.link = ServerLink(host, port, self.events,
                                   heartbeat=args.heartbeat, reconnect=True)
            self.link.start()
            self.conn_var.set(f"连接 {host}:{port} …")
            self.conn_label.configure(foreground="#b06a00")
            self.connect_btn.configure(text="断开")

        def disconnect(self):
            if self.link:
                self.link.stop()
                self.link = None
            self._connected = False
            self.conn_var.set("已断开")
            self.conn_label.configure(foreground="#b00020")
            self.connect_btn.configure(text="连接")

        def _on_close(self):
            if self.link:
                try:
                    self.link.send("QUIT")
                except Exception:
                    pass
                self.link.stop()
                self.link = None
            self.root.destroy()

        # ---------------- 事件循环 ----------------
        def _poll(self):
            try:
                while True:
                    kind, payload = self.events.get_nowait()
                    if kind == "line":
                        self._on_line(payload)
                    elif kind == "connected":
                        self._connected = True
                        self.conn_var.set("已连接（500ms 状态推送）")
                        self.conn_label.configure(foreground="#138a36")
                        self._log("[连接] 已连接")
                    elif kind == "stale":
                        self._log("[心跳] 超过 %.1fs 未收到 STATUS，重连中…" % args.heartbeat)
                        self.conn_var.set("心跳超时，重连中…")
                        self.conn_label.configure(foreground="#b06a00")
                    elif kind == "reconnecting":
                        self.conn_var.set("重连中…")
                        self.conn_label.configure(foreground="#b06a00")
                    elif kind == "disconnected":
                        self._connected = False
                        self.conn_var.set("已断开（自动重连）" if self.link else "已断开")
                        self.conn_label.configure(foreground="#b00020")
                        self._log("[连接] 断开")
                    elif kind == "error":
                        self._log("[错误] " + str(payload))
            except queue.Empty:
                pass
            self.root.after(100, self._poll)

        def _on_line(self, text):
            if text.startswith("STATUS "):
                st = parse_status(text)
                self.status = st
                self._status_count += 1
                self.vars["state"].set(st.get("state", "-"))
                self.vars["track"].set(st.get("track", "-"))
                self.vars["pos"].set("%s / %s" % (fmt_ms(st.get("pos")), fmt_ms(st.get("len"))))
                self.vars["vol"].set(st.get("vol", "-"))
                self.vars["speed"].set(st.get("speed", "-") + "x")
                self.vars["echo"].set(st.get("echo", "-"))
                self.vars["eq"].set(st.get("eq", "-"))
                self.vars["dist"].set(st.get("dist", "-") + " mm")
                self.vars["gesture"].set(st.get("gesture", "-"))
                self.vars["und"].set(st.get("und", "-"))
                self.vars["clients"].set(st.get("clients", "-"))
                self.vars["q"].set(st.get("q", "-"))
                try:
                    self._syncing = True
                    self.vol_scale.set(int(st.get("vol", 60)))
                    self.spd_scale.set(int(round(float(st.get("speed", 1.0)) * 10)))
                except (ValueError, TypeError):
                    pass
                finally:
                    self._syncing = False
                return
            self._log("<< " + text)
            if text.startswith("ERR"):
                self.exit_code = 1

        # ---------------- 发送 ----------------
        def send(self, line):
            if self.link is None:
                self._log("[提示] 未连接")
                return
            try:
                self.link.send(line)
                self._log(">> " + line)
            except Exception as e:
                self._log("[错误] 发送失败：" + str(e))

        def _debounce(self, key, fn):
            if key in self._pending:
                self.root.after_cancel(self._pending[key])
            self._pending[key] = self.root.after(180, fn)

        def _on_vol(self, value):
            v = int(float(value))
            self.vol_label.configure(text=str(v))
            if not self._syncing:
                self._debounce("vol", lambda: self.send(f"VOL {v}"))

        def _on_speed(self, value):
            v = int(float(value))
            self.spd_label.configure(text="%.1fx" % (v / 10.0))
            if not self._syncing:
                self._debounce("speed", lambda: self.send("SPEED %.1f" % (v / 10.0)))

        def _apply_echo(self):
            on = "on" if self.echo_on.get() else "off"
            try:
                delay = int(self.delay_var.get())
                fb = int(self.fb_var.get())
            except ValueError:
                self._log("[错误] 回声延迟/反馈必须是整数")
                return
            self.send(f"ECHO {on} {delay} {fb}")

        def _apply_eq(self):
            try:
                values = {b: int(self.eq_vars[b].get()) for b in ("low", "mid", "high")}
            except (ValueError, TypeError, tk.TclError):
                self._log("[错误] EQ 增益必须是 −12..12 的整数")
                return
            for band, v in values.items():
                self.send(f"EQ {band} {v}")

        def _log(self, text):
            self.log.configure(state="normal")
            self.log.insert("end", text + "\n")
            # 只保留最后 200 行
            if int(self.log.index("end-1c").split(".")[0]) > 200:
                self.log.delete("1.0", "2.0")
            self.log.configure(state="disabled")
            self.log.see("end")

        # ---------------- smoke ----------------
        def _smoke_finish(self):
            ok = self._connected and self._status_count > 0
            print("GUI_SMOKE %s（connected=%s status=%d）"
                  % ("PASS" if ok else "FAIL", self._connected, self._status_count))
            self.exit_code = 0 if ok else 1
            if self.link:
                self.link.stop()
                self.link = None
            self.root.destroy()

    app = App()
    if smoke:
        app.connect()
    root.mainloop()
    return app.exit_code


# ------------------------------------------------------------------ CLI
def run_cli(args):
    events = queue.Queue()
    link = ServerLink(args.host, args.port, events,
                      heartbeat=args.heartbeat, reconnect=not args.no_reconnect)
    link.start()

    deadline = time.time() + 3.0
    connected = False
    while time.time() < deadline:
        try:
            kind, payload = events.get(timeout=0.2)
        except queue.Empty:
            continue
        if kind == "connected":
            connected = True
            print(f"[连接] {args.host}:{args.port}")
            break
        if kind in ("error", "disconnected"):
            print(f"[FAIL] 连接失败：{payload}")
            link.stop()
            return 1

    if not connected:
        print("[FAIL] 3s 内未连接成功")
        link.stop()
        return 1

    # 连接成功后服务器先发一行欢迎语；必须从队列里取走，
    # 否则会被下一条指令当成回执（实测会整体错位一条）。
    welcome = None
    end = time.time() + 1.5
    while time.time() < end:
        try:
            kind, payload = events.get(timeout=0.2)
        except queue.Empty:
            continue
        if kind == "line":
            welcome = payload
            break
        if kind in ("error", "disconnected"):
            break
    if welcome:
        print("[欢迎] " + welcome)

    def drain_until_reply(timeout=3.0, want_status=False):
        end = time.time() + timeout
        while time.time() < end:
            try:
                kind, payload = events.get(timeout=0.2)
            except queue.Empty:
                continue
            if kind == "line":
                if payload.startswith("STATUS "):
                    if want_status:
                        return payload
                    if args.show_status:
                        print("[状态] " + payload)
                    continue
                return payload
            if kind in ("error", "disconnected"):
                return None
        return None

    rc = 0
    if args.cmd:
        for cmd in args.cmd:
            try:
                link.send(cmd)
            except Exception as e:
                print(f"[FAIL] 发送 {cmd} 失败：{e}")
                rc = 1
                break
            print(f">> {cmd}")
            reply = drain_until_reply(want_status=cmd.strip().upper().startswith("STATUS"))
            if reply is None:
                print("[FAIL] 3s 内无回执")
                rc = 1
                continue
            print("<< " + reply)
            if reply.startswith("ERR"):
                rc = 1
    else:
        # 交互模式：读线程把回执/状态交给打印线程，主线程只读 stdin
        running = threading.Event()
        running.set()

        def printer():
            while running.is_set():
                try:
                    kind, payload = events.get(timeout=0.2)
                except queue.Empty:
                    continue
                if kind == "line":
                    print(("[状态] " if payload.startswith("STATUS ") else "<< ") + payload)
                elif kind == "stale":
                    print("[心跳] 超时，重连中…")
                elif kind == "reconnecting":
                    print("[连接] 重连中…")
                elif kind == "disconnected":
                    print("[连接] 已断开")
                elif kind == "connected":
                    print("[连接] 已连接")

        th = threading.Thread(target=printer, daemon=True)
        th.start()
        print("输入协议指令（HELP 查看）；空行退出。")
        try:
            while True:
                line = input("gesture> ").strip()
                if not line:
                    break
                try:
                    link.send(line)
                except Exception as e:
                    print(f"[FAIL] 发送失败：{e}")
                    rc = 1
                    break
        except (EOFError, KeyboardInterrupt):
            pass
        running.clear()

    if args.watch > 0:
        print(f"[观察] 继续接收 {args.watch}s 的状态推送……")
        end = time.time() + args.watch
        while time.time() < end:
            try:
                kind, payload = events.get(timeout=0.2)
            except queue.Empty:
                continue
            if kind == "line" and payload.startswith("STATUS "):
                print("[状态] " + payload)

    try:
        link.send("QUIT")
        time.sleep(0.1)
    except Exception:
        pass
    link.stop()
    return rc


def main():
    fix_stdout()
    ap = argparse.ArgumentParser(description="GEC6818 手势音频播放器 PC 客户端")
    ap.add_argument("--host", default="127.0.0.1", help="板子/本机 IP，默认 127.0.0.1")
    ap.add_argument("--port", type=int, default=8888, help="TCP 端口，默认 8888")
    ap.add_argument("--heartbeat", type=float, default=2.0,
                    help="超过多少秒没收到任何数据判定断线（默认 2.0，服务器 500ms 推一次）")
    ap.add_argument("--cli", action="store_true", help="命令行模式（无 GUI）")
    ap.add_argument("--cmd", action="append", default=[], help="CLI 一次性指令，可重复")
    ap.add_argument("--watch", type=float, default=0.0, help="指令后继续观察状态秒数")
    ap.add_argument("--show-status", action="store_true", help="CLI 打印每条指令期间的状态推送")
    ap.add_argument("--no-reconnect", action="store_true", help="CLI 断线不重连")
    ap.add_argument("--gui-smoke", action="store_true",
                    help="启动 GUI、连接、等 2.5s 后自动退出（自动化验收用）")
    args = ap.parse_args()

    if args.gui_smoke:
        return run_gui(args, smoke=True)
    if args.cli:
        return run_cli(args)
    return run_gui(args, smoke=False)


if __name__ == "__main__":
    sys.exit(main())
