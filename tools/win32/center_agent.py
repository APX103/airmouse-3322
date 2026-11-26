# -*- coding: utf-8 -*-
"""AirMouse 置中代理（Windows）：把"中键按下"翻译成"光标移到屏幕中心"。

背景（README「输入设备扩展」节）：3322 的置中键以 bit2 经 dongle 原样透传为 USB 中键；
标准 HID 鼠标只有相对位移，固件不知道指针位置，"回屏幕中心"只能在 PC 侧做。
本脚本全局钩住中键按下 -> SetCursorPos 到屏幕中心，并吞掉这次中键（down+up 成对吞），
避免触发浏览器中键滚动等默认行为。

用法：
    python center_agent.py            # 常驻，Ctrl+Alt+C 切换 开/关，Ctrl+C 退出
    python center_agent.py --x 0.5 --y 0.5   # 中心比例（0~1），默认屏幕正中央
    python center_agent.py --px 960 --py 540 # 直接指定像素坐标

没运行本脚本时，置中键就是普通中键（优雅降级）。
依赖：仅标准库 ctypes（python 3.8+，无需 pip 安装任何东西）。
"""
import argparse
import ctypes
import ctypes.wintypes as wt
from ctypes import wintypes

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WH_MOUSE_LL = 14
WM_MBUTTONDOWN = 0x0207
WM_MBUTTONUP = 0x0208
WM_MBUTTONDBLCLK = 0x0209
WM_HOTKEY = 0x0312
MOD_CONTROL = 0x0002
MOD_ALT = 0x0001
MOD_NOREPEAT = 0x4000
HK_TOGGLE = 1


class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("pt", wt.POINT),
        ("mouseData", wt.DWORD),
        ("flags", wt.DWORD),
        ("time", wt.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]


HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wt.WPARAM, wt.LPARAM)

_state = {
    "active": True,       # 置中开关（Ctrl+Alt+C 切换）
    "swallow_up": False,  # 已经吞掉一次 down，配对的 up 也要吞，否则应用看到"按下没松开"
    "moves": 0,
}


def target_pos(args):
    if args.px is not None and args.py is not None:
        return int(args.px), int(args.py)
    w = user32.GetSystemMetrics(0)  # SM_CXSCREEN 主屏
    h = user32.GetSystemMetrics(1)  # SM_CYSCREEN
    return int(w * args.x), int(h * args.y)


def make_proc(args):
    tx, ty = target_pos(args)

    def _proc(n_code, w_param, l_param):
        if n_code >= 0 and _state["active"]:
            if w_param in (WM_MBUTTONDOWN, WM_MBUTTONDBLCLK):
                # 先挪光标再吞键：顺序无所谓，吞键只影响这次点击的投递
                user32.SetCursorPos(tx, ty)
                _state["moves"] += 1
                _state["swallow_up"] = True
                print(f"[center] -> ({tx},{ty})  累计 {_state['moves']} 次", flush=True)
                return 1  # 非零 = 吞掉这次中键按下
            if w_param == WM_MBUTTONUP and _state["swallow_up"]:
                _state["swallow_up"] = False
                return 1  # 配对吞掉 up，应用侧不残留"按下半截"状态
        return user32.CallNextHookEx(None, n_code, w_param, l_param)

    return HOOKPROC(_proc)


def main():
    ap = argparse.ArgumentParser(description="AirMouse 中键置中代理")
    ap.add_argument("--x", type=float, default=0.5, help="目标点横向比例 0~1（默认 0.5）")
    ap.add_argument("--y", type=float, default=0.5, help="目标点纵向比例 0~1（默认 0.5）")
    ap.add_argument("--px", type=int, default=None, help="目标点像素 x（指定后 --x 失效）")
    ap.add_argument("--py", type=int, default=None, help="目标点像素 y（指定后 --y 失效）")
    args = ap.parse_args()

    if (args.px is None) != (args.py is None):
        ap.error("--px 和 --py 要成对给出")

    proc = make_proc(args)
    # hMod 传 NULL：低层钩子不需要 DLL 注入，本进程回调即可
    hook = user32.SetWindowsHookExW(WH_MOUSE_LL, proc, None, 0)
    if not hook:
        raise SystemExit(f"SetWindowsHookExW 失败：{kernel32.GetLastError()}")
    if not user32.RegisterHotKey(None, HK_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, ord("C")):
        print("[center] 警告：注册 Ctrl+Alt+C 热键失败，切换功能不可用", flush=True)

    tx, ty = target_pos(args)
    print(f"[center] 置中代理已启动：中键按下 -> ({tx},{ty})；Ctrl+Alt+C 切换，Ctrl+C 退出", flush=True)
    try:
        msg = wt.MSG()
        while True:
            # 阻塞取消息；钩子回调由系统在取消息期间派发
            r = user32.GetMessageW(ctypes.byref(msg), None, 0, 0)
            if r == 0 or r == -1:  # 0=收到 WM_QUIT
                break
            if msg.message == WM_HOTKEY:
                _state["active"] = not _state["active"]
                print(f"[center] {'启用' if _state['active'] else '停用（中键恢复正常）'}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        user32.UnhookWindowsHookEx(hook)
        user32.UnregisterHotKey(None, HK_TOGGLE)
        print("[center] 已退出", flush=True)


if __name__ == "__main__":
    main()
