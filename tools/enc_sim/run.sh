#!/bin/bash
# 编码器解码算法的 Mac 端仿真：从打完补丁的 SDK 源码里抽出解码段，编译后回放波形并断言。
# 测的是固件里的真实代码（不是手抄副本），改了解码段或调了 AIM_ENC_* 宏就重跑一次。
# 用法: tools/enc_sim/run.sh [sle_client.c 路径]    默认取 source env.sh 后的 $HS_FBB
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

python3 -I "$HERE/extract.py" "$SRC" "$WORK/enc_slice.inc"
cc -std=c99 -Wall -Wextra -Wno-unused-variable -Wno-unused-const-variable -O1 \
    -I "$WORK" -o "$WORK/enc_sim" "$HERE/enc_sim.c"
# 解码器自己的串口打印有几千行，这里只留配置行、FAIL 行和最终结论
"$WORK/enc_sim" | grep -vE '^\[AIM\]\[ENC\] (up|down|sw press)'
