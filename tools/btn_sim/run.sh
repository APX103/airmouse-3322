#!/bin/bash
# 3322 按键扫描的主机仿真：从补丁抽真实源码 -> 编译 -> 跑时间线断言。
# Mac/Linux 直接 ./run.sh（cc）。Windows/Git Bash 若 MinGW 完好也直接跑；
# 本机 MinGW 的 cc1 有 DLL 导出缺失（静默 exit 127）时，自动退到 esp-clang 语法级检查——
# 能拦住全部编译错误，运行时断言仍需一台有正常 cc/gcc 的机器（如 Mac）。
set -e
cd "$(dirname "$0")"

PY=python
command -v "$PY" >/dev/null 2>&1 || PY=py
"$PY" extract.py

try_cc="${CC:-}"
for c in $try_cc cc gcc; do
    [ -n "$c" ] || continue
    command -v "$c" >/dev/null 2>&1 || continue
    if "$c" -std=c99 -Wall -Wextra -O1 -o btn_sim btn_sim.c 2>/dev/null; then
        ./btn_sim
        exit $?
    fi
done

CL=/d/Espressif/tools/esp-clang/16.0.1-fe4f10a809/esp-clang/bin/clang.exe
RES=/d/Espressif/tools/esp-clang/16.0.1-fe4f10a809/esp-clang/lib/clang/16/include
if [ -x "$CL" ] && [ -d /d/MinGW/include ] && [ -d "$RES" ]; then
    "$CL" -fsyntax-only --target=i686-pc-windows-gnu -nostdinc \
        -I/d/MinGW/include -I"$RES" -Wall -Wextra btn_sim.c
    echo "[run.sh] 语法检查通过；本机无可用主机链接器，运行时断言请在 Mac 跑 ./run.sh"
    exit 0
fi

echo "[run.sh] 未找到可用 C 编译器（试过 \$CC/cc/gcc/esp-clang）" >&2
exit 1
