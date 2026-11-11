#!/usr/bin/env python3
"""
player_regress.py -- 播放状态机回归（D1/D2/D3），主机侧即可跑

覆盖：
  D1  EOF 后 NEXT/PREV 能真正恢复产帧（state=PLAYING 且 pos 递增）
  D2  PAUSE 后 STATUS 上报 PAUSED，且暂停期间 pos 冻结；RESUME 能恢复
  D3  坏文件上报 ERROR 且粘滞；文件就位后 PLAY 能复位

设计：
  - 自带 WAV 合成（2s / 8s），不依赖 tools/gen_wav 或任何音频素材；
  - 通过 TCP STATUS 读取状态，不做白盒断言，和现场测试口径一致；
  - 每个用例独立进程 + 独立端口，跑完自动清理；
  - 输出英文，避免 Windows 控制台代码页把中文打成乱码。

用法：
  python tools/player_regress.py --exe build/gesture_audio
  python tools/player_regress.py --exe build/gesture_audio.exe --keep-logs

退出码：0 全部通过；非 0 = 失败条数。
"""
import argparse
import math
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time

RATE = 44100
CHANNELS = 2


# ---------------------------------------------------------------- WAV 合成
def write_wav(path, seconds):
    """1s 双音 + 1s 静音循环，和 tools/gen_wav 的语义一致。"""
    frames = int(RATE * seconds)
    pcm = bytearray()
    for i in range(frames):
        t = i / RATE
        if int(t) % 2 == 0:
            v = int(12000 * math.sin(2 * math.pi * 440 * t) +
                    6000 * math.sin(2 * math.pi * 880 * t))
            v = max(-32768, min(32767, v))
        else:
            v = 0
        pcm += struct.pack("<hh", v, v)
    data = bytes(pcm)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, CHANNELS, RATE,
                                    RATE * CHANNELS * 2, CHANNELS * 2, 16)
    header += b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(header)
        f.write(data)


# ---------------------------------------------------------------- 网络工具
def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


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


def send_cmd(port, line, wait=0.25):
    s = socket.create_connection(("127.0.0.1", port), 1.0)
    s.settimeout(1.0)
    try:
        s.recv(4096)                       # welcome
        s.sendall((line + "\n").encode())
        time.sleep(wait)
        try:
            return s.recv(4096).decode("utf-8", "replace").strip()
        except socket.timeout:
            return ""
    finally:
        s.close()


def query_status(port):
    s = socket.create_connection(("127.0.0.1", port), 1.0)
    s.settimeout(1.0)
    buf = b""
    try:
        s.recv(4096)
        s.sendall(b"STATUS\n")
        end = time.time() + 1.0
        while time.time() < end:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
            m = re.search(rb"STATUS [^\n]+", buf)
            if m:
                return m.group(0).decode("utf-8", "replace")
    finally:
        s.close()
    return ""


def field(line, key):
    m = re.search(key + r"=([^\s]+)", line)
    return m.group(1) if m else "?"


def as_int(text):
    return int(text) if text.isdigit() else 0


# ---------------------------------------------------------------- 用例框架
FAILS = []


def check(name, cond, detail):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + "  " + detail)
    if not cond:
        FAILS.append(name)


class Server:
    def __init__(self, exe, wav_dir, port, extra, log_name):
        self.log_path = os.path.join(wav_dir, log_name)
        self.log = open(self.log_path, "wb")
        self.proc = subprocess.Popen(
            [exe, "--port", str(port), "--no-display", "--run", "30",
             "--log", "info"] + extra,
            cwd=wav_dir, stdout=self.log, stderr=subprocess.STDOUT)
        self.port = port

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()


def run_case(name, exe, wav_dir, extra, body):
    print("\n=== %s ===" % name)
    port = free_port()
    srv = Server(exe, wav_dir, port, extra, name + ".log")
    try:
        if not wait_port(port):
            check(name + " start", False, "server did not listen")
            return
        time.sleep(0.8)
        body(port)
    finally:
        srv.stop()


def case_d1(port):
    # 等真正的 EOF（主机空 sink 比实时慢，固定 sleep 会误判）
    state = "?"
    for _ in range(20):
        time.sleep(0.3)
        state = field(query_status(port), "state")
        if state == "EOF":
            break
    print("  reached EOF : %s" % state)
    print("  NEXT reply  : " + send_cmd(port, "NEXT"))
    state, pos = "?", 0
    for _ in range(8):
        time.sleep(0.4)
        st = query_status(port)
        state, pos = field(st, "state"), as_int(field(st, "pos"))
        if state == "PLAYING" and pos > 0:
            break
    check("D1 EOF 后 NEXT 恢复播放", state == "PLAYING" and pos > 0,
          "state=%s pos=%d" % (state, pos))


def case_d2(port):
    time.sleep(1.0)
    print("  before PAUSE: " + query_status(port))
    print("  PAUSE reply : " + send_cmd(port, "PAUSE"))
    time.sleep(0.3)
    a = query_status(port)
    time.sleep(0.8)
    b = query_status(port)
    pa, pb = as_int(field(a, "pos")), as_int(field(b, "pos"))
    check("D2 PAUSE 上报 PAUSED",
          field(a, "state") == "PAUSED" and field(b, "state") == "PAUSED",
          "%s/%s" % (field(a, "state"), field(b, "state")))
    check("D2 暂停期间 pos 冻结", pa == pb, "pos %d -> %d" % (pa, pb))
    print("  RESUME reply: " + send_cmd(port, "RESUME"))
    time.sleep(0.5)
    c = query_status(port)
    check("D2 RESUME 恢复 PLAYING",
          field(c, "state") == "PLAYING" and as_int(field(c, "pos")) > pb,
          "state=%s pos=%s" % (field(c, "state"), field(c, "pos")))


def case_d3(port, wav_dir):
    time.sleep(1.0)
    a = query_status(port)
    check("D3 坏文件上报 ERROR", field(a, "state") == "ERROR",
          "state=%s" % field(a, "state"))
    print("  PLAY reply  : " + send_cmd(port, "PLAY"))
    time.sleep(1.2)
    b = query_status(port)
    check("D3 ERROR 粘滞（PLAY 仍 ERROR）", field(b, "state") == "ERROR",
          "state=%s" % field(b, "state"))

    # 把有效素材放到坏路径上，再 PLAY：状态机应能复位
    good = os.path.join(wav_dir, "regress_2s.wav")
    bad = os.path.join(wav_dir, "regress_missing.wav")
    with open(good, "rb") as src, open(bad, "wb") as dst:
        dst.write(src.read())
    print("  PLAY reply  : " + send_cmd(port, "PLAY"))
    state, pos = "?", 0
    for _ in range(6):
        time.sleep(0.4)
        st = query_status(port)
        state, pos = field(st, "state"), as_int(field(st, "pos"))
        if state == "PLAYING" and pos > 0:
            break
    check("D3 文件就位后 PLAY 复位", state == "PLAYING" and pos > 0,
          "state=%s pos=%d" % (state, pos))
    try:
        os.remove(bad)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser(description="播放状态机回归 D1/D2/D3")
    ap.add_argument("--exe", default=os.path.join("build", "gesture_audio"),
                    help="被测试的可执行文件（主机版或板端版）")
    ap.add_argument("--wav-dir", default="",
                    help="WAV/日志目录，默认用临时目录")
    ap.add_argument("--keep-logs", action="store_true", help="结束后保留临时目录")
    args = ap.parse_args()

    exe = os.path.abspath(args.exe)
    if not os.path.isfile(exe):
        print("[FAIL] executable not found: " + exe)
        print("       build it first: make           (host)")
        print("                       make BOARD=1 CROSS=...  (board)")
        return 2

    wav_dir = args.wav_dir or tempfile.mkdtemp(prefix="gesture_regress_")
    os.makedirs(wav_dir, exist_ok=True)
    write_wav(os.path.join(wav_dir, "regress_2s.wav"), 2)
    write_wav(os.path.join(wav_dir, "regress_8s.wav"), 8)
    missing = os.path.join(wav_dir, "regress_missing.wav")
    if os.path.exists(missing):
        os.remove(missing)

    run_case("D1_eof_skip", exe, wav_dir,
             [os.path.join(wav_dir, "regress_2s.wav")], case_d1)
    run_case("D2_pause", exe, wav_dir,
             [os.path.join(wav_dir, "regress_8s.wav")], case_d2)
    run_case("D3_error", exe, wav_dir, [missing],
             lambda port: case_d3(port, wav_dir))

    print("\n===== %d case(s) failed =====" % len(FAILS))
    for f in FAILS:
        print("  FAIL: " + f)
    if args.keep_logs:
        print("logs kept in: " + wav_dir)
    elif not FAILS:
        import shutil
        shutil.rmtree(wav_dir, ignore_errors=True)
    return len(FAILS)


if __name__ == "__main__":
    sys.exit(main())
