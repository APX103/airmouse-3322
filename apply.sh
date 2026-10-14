#!/bin/bash
# AirMouse-3322 补丁应用：把我们的修改打进两份官方 SDK checkout。
# 用法: ./apply.sh <hs-fbb目录> <fbb_bs2x目录>   （目录 = SDK 仓库根，含 samples/ 或 src/）
set -e
HSFBB=${1:?用法: ./apply.sh <hs-fbb目录> <fbb_bs2x目录>}
FBBBS2X=${2:?缺 fbb_bs2x 目录}

echo "== 3322 端 (hs-fbb) =="
cd "$HSFBB"
git apply --check "$(dirname "$0")/patches/hs-fbb/0001-sle-sample-airmouse.patch" && \
git apply "$(dirname "$0")/patches/hs-fbb/0001-sle-sample-airmouse.patch"
echo "补丁已应用。EKF 文件已随补丁新增（intent-to-add 导出，含完整内容）。"

echo "== dongle 端 (fbb_bs2x) =="
cd "$FBBBS2X"
git apply --check "$(dirname "$0")/patches/fbb_bs2x/0001-dongle-usb-hid.patch" && \
git apply "$(dirname "$0")/patches/fbb_bs2x/0001-dongle-usb-hid.patch"
echo "补丁已应用。别忘了 dongle 的 Kconfig（见 README §dongle 编译）。"
echo "== 完成 =="
