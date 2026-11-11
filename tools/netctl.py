#!/usr/bin/env python3
"""
netctl.py —— 命令行 TCP 客户端（现场测试用，主机侧工具）

为什么需要它：
  板端服务器每 500ms 主动推一行 STATUS，用 telnet 手敲时这些推送会和
  OK / ERR 回执混在一起，人眼极难分辨「这条是回执还是周期状态」。
  这个客户端把两类行分开显示，并把每条指令的判定结果显式打印出来，
  让测试记录可以直接抄进测试文档。

协议（projectBrief.md §6 / server.c）：
  请求：PLAY PAUSE RESUME NEXT PREV VOL n SPEED f
        ECHO on|off delay fb EQ low|mid|high gain STATUS HELP QUIT
  回执：OK ... / ERR <原因>
  状态：STATUS vol=.. speed=.. state=.. ...

用法：
  python netctl.py 192.168.1.100 "VOL 70" "SPEED 1.5" STATUS
  python netctl.py 127.0.0.1 --port 8888 --watch 2 "ECHO on 250 40"
  python netctl.py 127.0.0.1 --clients 5        # 验证 4 客户端上限

退出码：0 全部回执为 OK；1 出现 ERR 或连接失败；2 用法错误。
"""
import argparse
import socket
import sys
import time


def fix_stdout():
    """Windows 控制台默认 GBK，中文回执可能乱码；能改就改成 UTF-8。"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


class Conn:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.closed = False

    def send(self, line):
        self.sock.sendall((line + "\n").encode("utf-8"))

    def read_lines(self, want, idle=0.15, deadline=3.0, want_status=False):
        """
        读若干行：want 为期望的回执条数；want_status=True 时把 STATUS 行也算回执
        （STATUS 指令的就地回答本身就是 STATUS 行，与周期推送同格式）。
        用分片超时轮询，而不是靠 recv 阻塞——
        服务器每 500ms 主动推一行 STATUS，若按「等 0.25s 无数据」判定，
        推送会把空闲窗口永远填满，导致每条指令都等到 deadline（实测 2s/条）。
        """
        got, buf = [], b""
        start = time.time()
        last_data = start
        while True:
            if want_status:
                replies = len(got)
            else:
                replies = sum(1 for ln in got if not ln.startswith("STATUS "))
            if replies >= want and time.time() - last_data > idle:
                break
            if time.time() - start > deadline:
                break
            remain = deadline - (time.time() - start)
            self.sock.settimeout(max(0.02, min(0.3, remain)))
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                self.closed = True
                break
            if not chunk:
                self.closed = True
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").strip()
                if text:
                    got.append(text)
            last_data = time.time()
        return got

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def split_reply(lines):
    """把回来的行分成 (回执行, 状态行)。"""
    replies, statuses = [], []
    for ln in lines:
        if ln.startswith("STATUS "):
            statuses.append(ln)
        else:
            replies.append(ln)
    return replies, statuses


def run_commands(host, port, cmds, timeout, watch, show_status):
    rc = 0
    try:
        c = Conn(host, port, timeout)
    except OSError as e:
        print(f"[FAIL] 连接 {host}:{port} 失败：{e}")
        return 1

    # 连接成功时服务器会先发一行欢迎语
    welcome = c.read_lines(1, idle=0.3)
    if welcome:
        print(f"[欢迎] {welcome[0]}")
    else:
        print("[WARN] 未收到欢迎语（可能是非本程序的端口）")

    if not cmds:
        print("[提示] 未给出指令，仅验证连接。示例：VOL 70 / STATUS")
        c.close()
        return 0

    for n_cmd, cmd in enumerate(cmds, 1):
        if c.closed:
            print(f"[FAIL] 服务器已关闭连接，第 {n_cmd} 条起的 {len(cmds) - n_cmd + 1} 条未发送：{cmd}")
            rc = 1
            break
        c.send(cmd)
        is_status = cmd.strip().upper().startswith("STATUS")
        lines = c.read_lines(1, idle=0.15, want_status=is_status)
        replies, statuses = split_reply(lines)
        if is_status and statuses:
            replies = statuses[:1]          # STATUS 指令的回执就是这一行
            statuses = statuses[1:]
        if replies:
            for r in replies:
                status_ok = is_status and r.startswith("STATUS ")
                mark = "OK  " if (r.startswith("OK") or status_ok) \
                       else ("ERR " if r.startswith("ERR") else "??? ")
                print(f"[{mark}] {cmd}  ->  {r}")
                if r.startswith("ERR"):
                    rc = 1
        elif c.closed:
            print(f"[FAIL] {cmd} 发送后连接被服务器关闭")
            rc = 1
            break
        else:
            print(f"[TIMEOUT] {cmd} 在 {timeout}s 内无回执")
            rc = 1
        if show_status:
            for s in statuses:
                print(f"[状态] {s}")

    if watch > 0:
        print(f"[观察] 继续接收 {watch}s 内的周期推送……")
        end = time.time() + watch
        while time.time() < end:
            lines = c.read_lines(1, idle=0.2, deadline=min(0.6, end - time.time()))
            _, statuses = split_reply(lines)
            for s in statuses:
                print(f"[状态] {s}")
    c.close()
    return rc


def run_clients(host, port, n, timeout):
    """并发 n 个连接：前 4 个应收到欢迎语，第 5 个应被拒（ERR 或直接关闭）。"""
    conns, rc = [], 0
    for i in range(n):
        try:
            c = Conn(host, port, timeout)
            line = c.read_lines(1, idle=0.3)
            if line:
                text = line[0]
            elif c.closed:
                # 服务器拒绝时先发 ERR 再 close；Windows 下 close 可能把
                # 未发出的数据丢掉，客户端只看到连接被关闭，这仍属"被拒"。
                text = "连接被服务器关闭（未收到 ERR 文本，判定为被拒）"
            else:
                text = "(无回执)"
            if text.startswith("ERR") or c.closed:
                print(f"[{i + 1}] 被拒：{text}")
            else:
                print(f"[{i + 1}] 接入成功：{text}")
                conns.append(c)
                continue
            c.close()
        except OSError as e:
            print(f"[{i + 1}] 连接失败：{e}")
            rc = 1
    print(f"----- 实际保持连接 {len(conns)} 条（服务器上限应为 {4}）-----")
    for c in conns:
        c.close()
    return rc


def run_burst(host, port, cmds, timeout):
    """
    把多条指令一次性写进同一个 TCP 段，中间不等待。
    用途：触发 ARB_DUP_GUARD_MS（150ms）的同源去抖——逐条发送间隔太大，永远打不中。
    """
    try:
        c = Conn(host, port, timeout)
    except OSError as e:
        print(f"[FAIL] 连接 {host}:{port} 失败：{e}")
        return 1

    c.read_lines(1, idle=0.2)
    c.sock.sendall(("\n".join(cmds) + "\n").encode("utf-8"))
    lines = c.read_lines(len(cmds), idle=0.2, deadline=max(1.0, timeout))
    replies, statuses = split_reply(lines)
    print(f"[突发] 一次写入 {len(cmds)} 条：{' | '.join(cmds)}")
    for r in replies:
        print(f"[回执] {r}")
    for s in statuses[:2]:
        print(f"[状态] {s}")
    if len(replies) < len(cmds):
        print(f"[WARN] 只收到 {len(replies)}/{len(cmds)} 条回执")
        return 1
    c.close()
    return 0


def main():
    fix_stdout()
    ap = argparse.ArgumentParser(description="gesture_audio 命令行 TCP 客户端")
    ap.add_argument("host", help="板子/本机 IP")
    ap.add_argument("cmds", nargs="*", help="按顺序发送的指令行，如 'VOL 70'")
    ap.add_argument("--port", type=int, default=8888, help="端口，默认 8888")
    ap.add_argument("--timeout", type=float, default=2.0, help="连接与单次读取超时秒")
    ap.add_argument("--watch", type=float, default=0.0, help="指令发完后继续观察 STATUS 的秒数")
    ap.add_argument("--show-status", action="store_true", help="打印每条指令期间的 STATUS 推送")
    ap.add_argument("--clients", type=int, default=0, help="并发连接数测试（忽略 cmds）")
    ap.add_argument("--burst", action="store_true", help="所有指令一次写入（测 150ms 去抖）")
    args = ap.parse_args()

    if args.clients > 0:
        return run_clients(args.host, args.port, args.clients, args.timeout)
    if args.burst:
        if not args.cmds:
            print("--burst 需要至少一条指令")
            return 2
        return run_burst(args.host, args.port, args.cmds, args.timeout)
    return run_commands(args.host, args.port, args.cmds,
                        args.timeout, args.watch, args.show_status)


if __name__ == "__main__":
    sys.exit(main())
