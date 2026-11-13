#!/usr/bin/env python3
"""
client_link_test.py —— M6-01 客户端链路/心跳/重连回归（不依赖板子）

用一个 Python 假服务器验证 client/gesture_client.py 的 ServerLink：
  1. 连接、欢迎语、周期 STATUS；
  2. 服务器“静默但不断开” → 客户端按 heartbeat 判定 stale；
  3. 服务器关闭 → disconnected；
  4. 同端口重启 → 客户端自动重连并恢复 STATUS；
  5. 重连后指令仍有 OK 回执。

用法：
  py -3.11 tools/client_link_test.py
退出码：0 通过；非 0 = 失败条数。
"""
import os
import queue
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "client"))

from gesture_client import ServerLink  # noqa: E402


class DummyServer(threading.Thread):
    """最小 TCP 服务器：欢迎语 + 200ms STATUS + 回显 OK。"""

    def __init__(self, port=0):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", port))
        self.sock.listen(2)
        self.sock.settimeout(0.1)
        self.port = self.sock.getsockname()[1]
        self.silent = False
        self._stop_evt = threading.Event()
        self._clients = []

    def stop(self):
        self._stop_evt.set()
        self.join(timeout=1.0)
        try:
            self.sock.close()
        except OSError:
            pass
        for c in self._clients:
            try:
                c.close()
            except OSError:
                pass

    def run(self):
        while not self._stop_evt.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self._clients.append(conn)
            conn.settimeout(0.05)
            conn.sendall("OK dummy 已连接\n".encode("utf-8"))
            buf = b""
            last = 0.0
            while not self._stop_evt.is_set():
                now = time.time()
                if not self.silent and now - last > 0.2:
                    try:
                        conn.sendall(b"STATUS vol=60 speed=1.00 state=PLAYING "
                                     b"track=1/1 pos=0 len=8000 echo=off,250,40 "
                                     b"eq=0,0,0 dist=100 gesture=NONE und=0 clients=1 q=0\n")
                    except OSError:
                        break
                    last = now
                try:
                    chunk = conn.recv(1024)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if line:
                        try:
                            conn.sendall(("OK " + line + "\n").encode("utf-8"))
                        except OSError:
                            break
            try:
                conn.close()
            except OSError:
                pass


def wait_for(events, pred, timeout=5.0):
    end = time.time() + timeout
    seen = []
    while time.time() < end:
        try:
            kind, payload = events.get(timeout=0.2)
        except queue.Empty:
            continue
        seen.append((kind, payload))
        if pred(kind, payload):
            return kind, payload, seen
    return None, None, seen


def main():
    fails = 0
    events = queue.Queue()
    srv = DummyServer()
    port = srv.port
    srv.start()
    link = ServerLink("127.0.0.1", srv.port, events, heartbeat=0.7, reconnect=True)
    link.start()

    try:
        kind, payload, _ = wait_for(events, lambda k, p: k == "connected", 3.0)
        ok = kind == "connected"
        print(f"  [{'PASS' if ok else 'FAIL'}] connect")
        fails += 0 if ok else 1

        kind, payload, _ = wait_for(
            events, lambda k, p: k == "line" and p.startswith("STATUS "), 2.0)
        ok = kind == "line"
        print(f"  [{'PASS' if ok else 'FAIL'}] first STATUS")
        fails += 0 if ok else 1

        # 静默但不断开 → 心跳超时
        srv.silent = True
        kind, payload, _ = wait_for(events, lambda k, p: k == "stale", 2.5)
        ok = kind == "stale"
        print(f"  [{'PASS' if ok else 'FAIL'}] heartbeat stale")
        fails += 0 if ok else 1

        # 关闭服务器 → 客户端进入重连
        srv.stop()
        srv = None
        kind, payload, _ = wait_for(
            events, lambda k, p: k in ("disconnected", "reconnecting"), 3.0)
        ok = kind in ("disconnected", "reconnecting")
        print(f"  [{'PASS' if ok else 'FAIL'}] disconnected")
        fails += 0 if ok else 1

        # 同端口重启 → 自动重连 + 恢复 STATUS
        time.sleep(0.8)                  # 让旧连接上的重连尝试先失败
        while True:                      # 清掉重连过程中的旧事件
            try:
                events.get_nowait()
            except queue.Empty:
                break
        srv = DummyServer(port)
        srv.start()
        kind, payload, _ = wait_for(events, lambda k, p: k == "connected", 6.0)
        ok = kind == "connected"
        print(f"  [{'PASS' if ok else 'FAIL'}] auto reconnect")
        fails += 0 if ok else 1

        kind, payload, _ = wait_for(
            events, lambda k, p: k == "line" and p.startswith("STATUS "), 2.0)
        ok = kind == "line"
        print(f"  [{'PASS' if ok else 'FAIL'}] STATUS after reconnect")
        fails += 0 if ok else 1

        link.send("PING")
        kind, payload, _ = wait_for(
            events, lambda k, p: k == "line" and p.startswith("OK PING"), 2.0)
        ok = kind == "line"
        print(f"  [{'PASS' if ok else 'FAIL'}] command after reconnect")
        fails += 0 if ok else 1
    finally:
        link.stop()
        if srv is not None:
            srv.stop()

    print(f"\nRESULT: {'ALL PASS' if fails == 0 else str(fails) + ' FAILED'}")
    return fails


if __name__ == "__main__":
    sys.exit(main())
