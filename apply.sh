#!/bin/bash
# AirMouse-3322 补丁应用：把我们的修改打进两份官方 SDK checkout。
# 用法: ./apply.sh <hs-fbb目录> <fbb_bs2x目录>   （目录 = SDK 仓库根，含 samples/ 或 src/）
set -e
HSFBB=${1:?用法: ./apply.sh <hs-fbb目录> <fbb_bs2x目录>}
FBBBS2X=${2:?缺 fbb_bs2x 目录}
# 补丁路径转绝对：下面会 cd 进 SDK 目录，相对路径（./apply.sh 调用时）会失效
PATCHDIR="$(cd "$(dirname "$0")" && pwd)/patches"

echo "== 3322 端 (hs-fbb) =="
# check 与 apply 分开两条命令：set -e 才能在 check 失败时中止，而不是继续打印"已应用"
git -C "$HSFBB" apply --check "$PATCHDIR/hs-fbb/0001-sle-sample-airmouse.patch"
git -C "$HSFBB" apply "$PATCHDIR/hs-fbb/0001-sle-sample-airmouse.patch"
echo "补丁已应用。EKF 文件已随补丁新增（intent-to-add 导出，含完整内容）。"

echo "== dongle 端 (fbb_bs2x) =="
git -C "$FBBBS2X" apply --check "$PATCHDIR/fbb_bs2x/0001-dongle-usb-hid.patch"
git -C "$FBBBS2X" apply "$PATCHDIR/fbb_bs2x/0001-dongle-usb-hid.patch"
echo "补丁已应用。别忘了 dongle 的 Kconfig（见 README §dongle 编译）。"
echo "== 完成 =="
