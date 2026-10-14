#!/usr/bin/env python3
"""3322 (HiDiTing V100) SLE AT 交互工具.

硬件前提: CH347 USB-UART 接 UART2 (TX/RX 交叉 + 共地), 板子 Type-C 5V 供电.
业务串口 750000 baud. 3322 出厂 community 固件带低功耗睡眠, AT 无响应时先发
AT+PM=0 唤醒 (wake 子命令会自动带上).

用法:
  ./at3322.py wake  <port>                 # 唤醒: AT+PM=0 + AT 试探
  ./at3322.py send  <port> "AT+XXX"        # 发一条命令, 读响应直到 OK/ERROR 或超时
  ./at3322.py seq   <port> "cmd1" "cmd2"   # 顺序发多条, 每条等响应
  ./at3322.py mon   <port> [--dur 30]      # 纯监听 (验证 APP|run 心跳)
  ./at3322.py scan  <port> [--dur 20]      # 开 SLE 扫描并抓日志里的地址行

pyserial 用 fbb venv 里的: ~/.hs-fbb/venv/bin/python at3322.py ...
"""
import argparse
import re
import sys
import time

BAUD = 750000

# fbb venv 才有 pyserial
try:
    import serial
except ImportError:
    sys.exit("pyserial 缺失: 用 ~/.hs-fbb/venv/bin/python 运行本脚本")


def open_port(port: str) -> serial.Serial:
    return serial.Serial(port, BAUD, timeout=0.2, write_timeout=2)


def drain(s: serial.Serial) -> None:
    n = s.in_waiting
    if n:
        s.read(n)


def read_for(s: serial.Serial, seconds: float, echo: bool = True) -> str:
    """读满 seconds 秒, 透传返回全文."""
    buf = []
    end = time.time() + seconds
    while time.time() < end:
        n = s.in_waiting
        if n:
            data = s.read(n)
            text = data.decode("utf-8", "replace")
            buf.append(text)
            if echo:
                sys.stdout.write(text)
                sys.stdout.flush()
        else:
            time.sleep(0.02)
    return "".join(buf)


def send_cmd(s: serial.Serial, cmd: str, wait: float) -> str:
    drain(s)
    payload = (cmd + "\r\n").encode()
    s.write(payload)
    s.flush()
    print(f"\n>>> {cmd}")
    return read_for(s, wait)


def cmd_response(s: serial.Serial, cmd: str, wait: float = 3.0) -> str:
    """发一条并返回响应文本 (不带回显)."""
    out = send_cmd(s, cmd, wait)
    return out


def do_wake(port: str) -> None:
    s = open_port(port)
    print(f"[wake] {port} @ {BAUD} — 先 AT+PM=0 关低功耗, 再 AT 试探")
    # 睡眠中 RX 数据应能唤醒; 连发几轮提高命中
    for i in range(3):
        send_cmd(s, "AT+PM=0", wait=2)
        out = send_cmd(s, "AT", wait=1.5)
        if "OK" in out:
            print("\n[wake] 板子已应答 OK")
            s.close()
            return
        print(f"[wake] 第 {i + 1} 轮无 OK, 重试...")
    print("[wake] 仍无响应 — 检查 TX/RX 是否交叉、共地、供电, 或监听 30s 看心跳")
    s.close()


def do_scan(port: str, dur: float) -> None:
    """开扫描并高亮可能的地址行, 方便取 AT+SLECLIENTPAIR 的 6 字节地址."""
    s = open_port(port)
    s.reset_input_buffer()
    send_cmd(s, "AT+SLECLIENTENABLE", wait=2)
    send_cmd(s, "AT+SLECLIENTINIT", wait=2)
    print(f"\n[scan] 开扫描 {dur}s, 抓地址行...")
    s.write(b"AT+SLECLIENTSCAN=0\r\n")
    s.flush()
    text = read_for(s, dur)
    # 常见地址形态: 6 个逗号分隔的十六进制字节, 或 addr/ address 字样行
    addr_re = re.compile(r"^.*((?:[0-9a-fA-F]{1,2},){5}[0-9a-fA-F]{1,2}).*$",
                         re.MULTILINE)
    hits = [ln for ln in text.splitlines() if addr_re.match(ln)]
    print("\n[scan] ---- 疑似地址行 ----")
    for h in hits:
        print(h)
    s.write(b"AT+SLECLIENTSCAN=1\r\n")  # 停扫描
    s.flush()
    read_for(s, 1)
    s.close()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="op", required=True)

    p = sub.add_parser("wake"); p.add_argument("port")
    p = sub.add_parser("send"); p.add_argument("port"); p.add_argument("cmd")
    p.add_argument("--wait", type=float, default=3.0)
    p = sub.add_parser("seq"); p.add_argument("port"); p.add_argument("cmds", nargs="+")
    p.add_argument("--wait", type=float, default=3.0)
    p = sub.add_parser("mon"); p.add_argument("port")
    p.add_argument("--dur", type=float, default=30.0)
    p = sub.add_parser("scan"); p.add_argument("port")
    p.add_argument("--dur", type=float, default=20.0)

    a = ap.parse_args()
    if a.op == "wake":
        do_wake(a.port)
    elif a.op == "send":
        s = open_port(a.port)
        cmd_response(s, a.cmd, a.wait)
        s.close()
    elif a.op == "seq":
        s = open_port(a.port)
        for c in a.cmds:
            cmd_response(s, c, a.wait)
        s.close()
    elif a.op == "mon":
        s = open_port(a.port)
        print(f"[mon] {a.port} @ {BAUD} 监听 {a.dur}s (心跳 APP|run 约 20s 一条)")
        read_for(s, a.dur)
        s.close()
    elif a.op == "scan":
        do_scan(a.port, a.dur)


if __name__ == "__main__":
    main()
