#!/usr/bin/env python3
"""
protocol_matrix.py —— M6-02 协议矩阵 / 4 连接 / 500ms 推送 / 重连回归

主机侧一键跑（不需要板子、不需要网络；本机 127.0.0.1 回环）：
  1. 启动 build/host/gesture_audio.exe（--sensor sim --no-display）；
  2. 覆盖 projectBrief.md §6 的全部指令与越界/非法输入；
  3. 验证 STATUS 字段格式、周期推送 ~500ms、4 客户端上限、断线重连；
  4. 验证半包/粘包/空行/超长行/QUIT 等边界。

用法：
  py -3.11 tools/protocol_matrix.py
  py -3.11 tools/protocol_matrix.py --exe build/host/gesture_audio.exe
  py -3.11 tools/protocol_matrix.py --exe build/host/gesture_audio.exe --keep-log

退出码：0 全部通过；非 0 = 失败条数。
"""
import argparse
import math
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave


# ------------------------------------------------------------------ 基础
def fix_stdout():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


class Conn:
    def __init__(self, host, port, timeout=2.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""
        self.closed = False

    def send(self, line):
        self.sock.sendall((line + "\n").encode("utf-8"))

    def send_raw(self, data):
        self.sock.sendall(data)

    def readline(self, timeout=2.0):
        end = time.time() + timeout
        while b"\n" not in self.buf:
            remain = end - time.time()
            if remain <= 0:
                return None
            self.sock.settimeout(max(0.02, remain))
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                return None
            except OSError:
                self.closed = True
                return None
            if not chunk:
                self.closed = True
                return None
            self.buf += chunk
        raw, self.buf = self.buf.split(b"\n", 1)
        return raw.decode("utf-8", "replace").strip()

    def read_non_status(self, timeout=2.0):
        end = time.time() + timeout
        while time.time() < end:
            ln = self.readline(max(0.02, end - time.time()))
            if ln is None:
                return None
            if ln.startswith("STATUS "):
                continue
            return ln
        return None

    def read_status(self, timeout=2.0):
        end = time.time() + timeout
        while time.time() < end:
            ln = self.readline(max(0.02, end - time.time()))
            if ln is None:
                return None
            if ln.startswith("STATUS "):
                return ln
        return None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class Checker:
    def __init__(self):
        self.fails = 0
        self.passes = 0

    def check(self, name, cond, detail=""):
        if cond:
            self.passes += 1
            print(f"  [PASS] {name:<26} {detail}")
        else:
            self.fails += 1
            print(f"  [FAIL] {name:<26} {detail}")


def parse_status(line):
    out = {}
    for tok in line.split()[1:]:
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def send_expect(ck, c, cmd, prefix, detail=""):
    """发一条指令，跳到下一条非 STATUS 回执；期间 180ms 内不重复同类指令。"""
    c.send(cmd)
    ln = c.read_non_status(2.0)
    ok = bool(ln) and ln.startswith(prefix)
    ck.check(f"cmd {cmd}", ok, f"→ {ln}" if ln else "no reply")
    time.sleep(0.18)                     # 避开 ARB_DUP_GUARD_MS 的同源去抖
    return ln


# ------------------------------------------------------------------ WAV / 服务器
def make_wav(path, seconds=4.0):
    rate, ch = 44100, 2
    frames = int(rate * seconds)
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        data = bytearray()
        for i in range(frames):
            t = i / rate
            v = int(8000 * math.sin(2 * 3.1415926 * 440 * t))
            data += struct.pack("<hh", v, v)
        w.writeframes(bytes(data))


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def wait_port(port, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = socket.create_connection(("127.0.0.1", port), 0.3)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


# ------------------------------------------------------------------ 用例
def test_commands(ck, host, port):
    print("\n== 1. 欢迎语 / 全部指令 / 越界与非法输入 ==")
    c = Conn(host, port)
    welcome = c.readline(2.0)
    ck.check("welcome", bool(welcome) and welcome.startswith("OK gesture_audio"),
             f"→ {welcome}")

    send_expect(ck, c, "HELP", "OK HELP", "help text")
    send_expect(ck, c, "vol 60", "OK ", "lowercase")
    for cmd in ("PLAY", "PAUSE", "RESUME", "NEXT", "PREV"):
        send_expect(ck, c, cmd, "OK ")

    for v in (0, 70, 100):
        send_expect(ck, c, f"VOL {v}", "OK ")
    for v in (101, -1, "abc"):
        send_expect(ck, c, f"VOL {v}", "ERR ")

    for v in ("0.5", "1.2", "2.0"):
        send_expect(ck, c, f"SPEED {v}", "OK ")
    for v in ("2.5", "0.4", "abc"):
        send_expect(ck, c, f"SPEED {v}", "ERR ")

    send_expect(ck, c, "ECHO on 250 40", "OK ")
    send_expect(ck, c, "ECHO off 50 0", "OK ")
    send_expect(ck, c, "ECHO on 800 90", "OK ")
    for cmd in ("ECHO on 40 40", "ECHO on 250 91", "ECHO maybe 250 40"):
        send_expect(ck, c, cmd, "ERR ")

    for cmd in ("EQ low -12", "EQ mid 0", "EQ high 12"):
        send_expect(ck, c, cmd, "OK ")
    for cmd in ("EQ mid 99", "EQ xxx 1", "EQ low"):
        send_expect(ck, c, cmd, "ERR ")

    send_expect(ck, c, "BOGUS 1", "ERR ")

    # 空行不应产生任何回执
    c.send("")
    time.sleep(0.25)
    c.send("STATUS")
    st = c.read_status(2.0)
    extra = c.read_non_status(0.25)
    ck.check("empty line ignored", st is not None and extra is None,
             f"status={'yes' if st else 'no'} extra={extra}")
    c.close()


def test_status(ck, host, port):
    print("\n== 2. STATUS 字段 / 500ms 周期推送 ==")
    c = Conn(host, port)
    c.readline(2.0)
    c.send("VOL 70")
    c.read_non_status(2.0)
    time.sleep(0.2)
    c.send("SPEED 1.0")
    c.read_non_status(2.0)
    time.sleep(0.2)
    c.send("STATUS")
    st = c.read_status(2.0)
    d = parse_status(st) if st else {}
    required = {"vol", "speed", "state", "track", "pos", "len", "echo", "eq",
                "dist", "gesture", "und", "clients", "q"}
    ck.check("status fields", required <= set(d.keys()),
             f"missing={sorted(required - set(d.keys()))}")
    ck.check("status vol=70", d.get("vol") == "70", f"vol={d.get('vol')}")
    ck.check("status speed=1.00", d.get("speed") == "1.00", f"speed={d.get('speed')}")
    ck.check("status clients=1", d.get("clients") == "1", f"clients={d.get('clients')}")
    ck.check("status echo triple", (d.get("echo") or "").count(",") == 2,
             f"echo={d.get('echo')}")
    ck.check("status eq triple", (d.get("eq") or "").count(",") == 2,
             f"eq={d.get('eq')}")
    ck.check("status state valid",
             d.get("state") in ("STOPPED", "PLAYING", "PAUSED", "EOF", "ERROR"),
             f"state={d.get('state')}")

    # 3s 内至少应有 5 条推送（500ms 一条）
    stamps = []
    for _ in range(6):
        line = c.read_status(2.0)
        if line is None:
            break
        stamps.append(time.time())
    ck.check("push count >=5/3s", len(stamps) >= 5, f"got {len(stamps)} in ~3s")
    if len(stamps) >= 3:
        gaps = [stamps[i + 1] - stamps[i] for i in range(len(stamps) - 1)]
        avg = sum(gaps) / len(gaps)
        ck.check("push interval ~500ms", 0.30 <= avg <= 0.80,
                 f"avg={avg * 1000:.0f}ms gaps={[round(g * 1000) for g in gaps]}")
    c.close()


def test_framing(ck, host, port):
    print("\n== 3. 半包 / 粘包 / 超长行 / QUIT ==")
    c = Conn(host, port)
    c.readline(2.0)

    # 粘包：两条指令一次写入
    time.sleep(0.3)                       # 避开上一条 VOL 的 150ms 去抖窗口
    c.send_raw(b"VOL 55\nSTATUS\n")
    got_ok = False
    got_st = None
    end = time.time() + 3.0
    while time.time() < end and (not got_ok or got_st is None):
        ln = c.readline(1.0)
        if ln is None:
            break
        if ln == "OK VOL 55":
            got_ok = True
        elif ln.startswith("STATUS ") and parse_status(ln).get("vol") == "55":
            got_st = ln
    ck.check("pipeline VOL+STATUS", got_ok and got_st is not None,
             f"ok={got_ok} status={'yes' if got_st else 'no'}")
    time.sleep(0.3)

    # 半包：一条指令拆两次写
    c.send_raw(b"VOL 6")
    time.sleep(0.1)
    c.send_raw(b"5\n")
    ln = c.read_non_status(2.0)
    ck.check("fragment VOL 65", ln == "OK VOL 65", f"→ {ln}")
    time.sleep(0.2)

    # 超长行：ERR 后断开
    c2 = Conn(host, port)
    c2.readline(2.0)
    c2.send_raw(b"A" * 300 + b"\n")
    ln = c2.read_non_status(2.0)
    ok = (ln is not None and ln.startswith("ERR 单行超过")) or c2.closed
    ck.check("overlong line ERR", ok, f"→ {ln} closed={c2.closed}")
    ck.check("overlong disconnect", c2.closed or c2.readline(0.5) is None,
             "connection closed")
    c2.close()

    # QUIT
    c3 = Conn(host, port)
    c3.readline(2.0)
    c3.send("QUIT")
    ln = c3.readline(2.0)
    ck.check("QUIT reply", ln == "OK BYE", f"→ {ln}")
    ck.check("QUIT closes", c3.closed or c3.readline(0.5) is None, "closed")
    c3.close()
    c.close()


def test_clients(ck, host, port):
    print("\n== 4. 4 连接上限 / 释放 / 周期广播 ==")
    conns = []
    for i in range(4):
        c = Conn(host, port)
        welcome = c.readline(2.0)
        ck.check(f"client {i + 1} welcome", bool(welcome) and welcome.startswith("OK"),
                 f"→ {welcome}")
        conns.append(c)

    c5 = Conn(host, port)
    ln = c5.readline(2.0)
    rejected = (ln or "").startswith("ERR") or c5.closed
    ck.check("5th rejected", rejected, f"→ {ln} closed={c5.closed}")
    c5.close()

    conns[0].send("STATUS")
    st = None
    end = time.time() + 2.0
    while time.time() < end:
        line = conns[0].read_status(0.5)
        if line:
            st = line
            break
    ck.check("STATUS clients=4", st is not None and parse_status(st).get("clients") == "4",
             f"clients={parse_status(st).get('clients') if st else '?'}")

    # 两个客户端都能收到周期推送
    time.sleep(0.2)
    ck.check("broadcast c1", conns[0].read_status(1.5) is not None, "got STATUS")
    ck.check("broadcast c2", conns[1].read_status(1.5) is not None, "got STATUS")

    # 释放一个槽位后应能再接入
    conns[0].close()
    time.sleep(0.5)
    c6 = Conn(host, port)
    welcome = c6.readline(2.0)
    ck.check("slot reuse after close", bool(welcome) and welcome.startswith("OK"),
             f"→ {welcome}")
    c6.close()
    for c in conns[1:]:
        c.close()


def test_reconnect(ck, host, port):
    print("\n== 5. 断线 / 重连 / 状态仍可用 ==")
    c = Conn(host, port)
    c.readline(2.0)
    c.send("STATUS")
    st1 = c.read_status(2.0)
    c.close()
    time.sleep(0.4)

    c2 = Conn(host, port)
    welcome = c2.readline(2.0)
    c2.send("STATUS")
    st2 = c2.read_status(2.0)
    ck.check("reconnect welcome", bool(welcome) and welcome.startswith("OK gesture_audio"),
             f"→ {welcome}")
    ck.check("reconnect STATUS", st1 is not None and st2 is not None,
             f"before={'yes' if st1 else 'no'} after={'yes' if st2 else 'no'}")
    c2.close()


# ------------------------------------------------------------------ main
def main():
    fix_stdout()
    ap = argparse.ArgumentParser(description="M6-02 协议矩阵回归")
    ap.add_argument("--exe", default=os.path.join("build", "host", "gesture_audio.exe"),
                    help="被测服务器可执行文件")
    ap.add_argument("--keep-log", action="store_true", help="保留服务器输出")
    args = ap.parse_args()

    if not os.path.isfile(args.exe):
        print(f"[FAIL] 找不到 {args.exe}，先编译主机版：make（或见 README）")
        return 1

    tmpdir = tempfile.mkdtemp(prefix="gesture_proto_")
    wav = os.path.join(tmpdir, "matrix.wav")
    make_wav(wav)
    port = free_port()
    server_log = open(os.path.join(tmpdir, "server.log"), "wb") if args.keep_log else None
    server = None
    ck = Checker()

    try:
        print(f"启动服务器：{args.exe} --port {port}（回环测试）")
        server = subprocess.Popen(
            [args.exe, "--port", str(port), "--sensor", "sim", "--no-display",
             "--run", "60", "--log", "error", wav],
            stdout=server_log or subprocess.DEVNULL,
            stderr=subprocess.STDOUT if server_log else subprocess.DEVNULL,
        )
        if not wait_port(port):
            print("[FAIL] 服务器 8s 内未监听端口")
            return 1

        test_commands(ck, "127.0.0.1", port)
        test_status(ck, "127.0.0.1", port)
        test_framing(ck, "127.0.0.1", port)
        test_clients(ck, "127.0.0.1", port)
        test_reconnect(ck, "127.0.0.1", port)
    finally:
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=3)
            except Exception:
                server.kill()
        if server_log:
            server_log.close()

    print("\n--------------------------------------------------------")
    print(f" 结果：{ck.passes} 通过 / {ck.fails} 失败"
          + (f"（服务器日志：{tmpdir}）" if args.keep_log else ""))
    return ck.fails


if __name__ == "__main__":
    sys.exit(main())
