#!/usr/bin/env python3
"""单进程一口气：PM=0 轰到生效 → ENABLE → INIT → SCAN → PAIR → FIND。
板子醒窗只有几秒，跨进程命令会撞睡眠，必须一个串口会话连发。"""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem11401'

def read_for(s, sec):
    out = ''
    end = time.time() + sec
    while time.time() < end:
        n = s.in_waiting
        if n:
            out += s.read(n).decode('utf-8', 'replace')
        else:
            time.sleep(0.01)
    return out

def send(s, cmd, wait):
    if s.in_waiting:
        s.read(s.in_waiting)
    s.write((cmd + '\r\n').encode()); s.flush()
    out = read_for(s, wait)
    print(f'>>> {cmd}\n{out}', flush=True)
    return out

s = serial.Serial(PORT, 750000, timeout=0.1, write_timeout=2)
s.reset_input_buffer()

# 阶段1：轰 PM=0 直到确认（PM 生效后板子不再睡，后续命令窗口稳定）
ok = False
for i in range(12):
    out = send(s, 'AT+PM=0', 0.8)
    if 'OK' in out:
        ok = True
        break
if not ok:
    print('[FAIL] PM=0 无法生效'); s.close(); sys.exit(1)
time.sleep(0.3)
send(s, 'AT', 1.0)   # 双确认

# 阶段2：SLE 序列
send(s, 'AT+SLECLIENTENABLE', 5)
send(s, 'AT+SLECLIENTINIT', 4)
send(s, 'AT+SLECLIENTSCAN=0', 10)
send(s, 'AT+SLECLIENTSCAN=1', 1)
send(s, 'AT+SLECLIENTPAIR=0,0,0,0,0,0', 12)
send(s, 'AT+SLECLIENTFIND', 10)

# 阶段3：观察流 25s
print('=== 流观察 25s ===', flush=True)
read_for(s, 25)
s.close()
