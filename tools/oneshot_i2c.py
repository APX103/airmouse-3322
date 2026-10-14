#!/usr/bin/env python3
"""单进程 I2C 总线验证：PM=0 轰到生效 → AT+STARTI2CMASTER（写模式验 ACK）。"""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem11401'
ADDR = sys.argv[2] if len(sys.argv) > 2 else '104'   # 0x68 十进制
MODE = sys.argv[3] if len(sys.argv) > 3 else '0'     # pin 复用 mode

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

for i in range(12):
    if 'OK' in send(s, 'AT+PM=0', 0.8):
        break
time.sleep(0.2)
send(s, 'AT', 1.0)

# I2C 写模式（type=0）：从设备 ACK 地址则 succ；NACK/无线则 failed
send(s, f'AT+STARTI2CMASTER={ADDR},0,4,5,{MODE},{MODE},0', 25)
s.close()
