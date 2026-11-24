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
echo "0001 已应用（应用代码+EKF 文件）。"
# 0002：3322 目标配置把 I2C 等待超时从 0xFFFFFFFF 改回 100ms（其余 3322 目标本来就是 0x64；
# 无限等会让 IMU 读阻塞主循环，见 REVIEW.md §6）
git -C "$HSFBB" apply --check "$PATCHDIR/hs-fbb/0002-i2c-bounded-wait-timeout.patch"
git -C "$HSFBB" apply "$PATCHDIR/hs-fbb/0002-i2c-bounded-wait-timeout.patch"
echo "0002 已应用（I2C 等待超时 100ms）。"

echo "== dongle 端 (fbb_bs2x) =="
git -C "$FBBBS2X" apply --check "$PATCHDIR/fbb_bs2x/0001-dongle-usb-hid.patch"
git -C "$FBBBS2X" apply "$PATCHDIR/fbb_bs2x/0001-dongle-usb-hid.patch"
echo "补丁已应用。别忘了 dongle 的 Kconfig（见 README §dongle 编译）。"
echo "== 完成 =="
