#!/bin/bash
# 运动管线（陀螺 → 每拍光标位移）的 Mac 端仿真：从打完补丁的 SDK 源码里抽出解算段，编译后喂转动曲线并断言。
# 测的是固件里的真实代码：走对的距离、慢速也匀速出点、循环晚醒不丢距离、手抖不漂移。
# 用法: tools/motion_sim/run.sh [sle_client.c 路径]    默认取 source env.sh 后的 $HS_FBB
set -eo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
# shellcheck disable=SC1091
if [ -z "${HS_FBB:-}" ] && [ -f "$ROOT/env.sh" ]; then
    source "$ROOT/env.sh"
fi
SRC="${1:-${HS_FBB:-}/samples/native_samples/sle/sle_client.c}"
if [ ! -f "$SRC" ]; then
    echo "找不到 sle_client.c: $SRC（先在 SDK 上打补丁，或把路径作为参数传入）" >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 -I "$HERE/extract.py" "$SRC" "$WORK/motion_slice.inc"
cc -std=c99 -Wall -Wextra -Wno-unused-variable -Wno-unused-function -O1 \
    -I "$WORK" -o "$WORK/motion_sim" "$HERE/motion_sim.c"
# 解算段自己的串口打印（[SNAP]）只留结论行
"$WORK/motion_sim" | grep -vE '^\[SNAP\]'
