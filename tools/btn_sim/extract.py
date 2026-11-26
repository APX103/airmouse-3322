#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 0001-sle-sample-airmouse.patch 抽出按键/输入基础设施源码，生成 btn_slice.inc。

抽的是"开关量输入"到"旋转编码器"两个标记注释之间的全部新增（'+'）行——
即 aim_pin_level / aim_input_config / aim_input_open_level / aim_input_diag /
aim_btn_init / aim_btn_scan 的真实固件源码，仿真测的就是这份代码本身。
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent
PATCH = ROOT.parent.parent / "patches" / "hs-fbb" / "0001-sle-sample-airmouse.patch"
BEGIN = "开关量输入"
END = "旋转编码器"


def main():
    lines = PATCH.read_text(encoding="utf-8").splitlines()
    start = next(i for i, l in enumerate(lines) if BEGIN in l)
    end = next(i for i, l in enumerate(lines) if END in l and i > start)
    out = [l[1:] for l in lines[start:end] if l.startswith("+") and not l.startswith("+++")]
    dest = ROOT / "btn_slice.inc"
    dest.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{dest.name}: {len(out)} lines (patch lines {start + 1}..{end})")


if __name__ == "__main__":
    main()
