#!/bin/bash
# AirMouse-3322 目录内开发环境：  source env.sh
#
# 工具链与官方 SDK 全部锁在本目录内（已被 .gitignore 排除），删目录即清理干净：
#   .fbb/         FBB_INSTALL_DIR —— fbb 托管的 venv + 工具链（bisheng / hs-flash / ccache）
#   sdk/hs-fbb    3322 端官方 SDK（本地分支 airmouse-base，已打补丁）
#   sdk/fbb_bs2x  dongle 端官方 SDK（master，已打补丁）
# 不 source 也能用 fbb，但必须先 export FBB_INSTALL_DIR，否则 fbb 会回落到 ~/.hs-fbb。

AIRMOUSE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export FBB_INSTALL_DIR="$AIRMOUSE_ROOT/.fbb"
export FBB_SDK_ROOT="$AIRMOUSE_ROOT/sdk"
export HS_FBB="$AIRMOUSE_ROOT/sdk/hs-fbb"
export FBB_BS2X="$AIRMOUSE_ROOT/sdk/fbb_bs2x"
export FBB_PY="$FBB_INSTALL_DIR/venv/bin/python"   # 带 pyserial：tools/*.py 用它跑
export FBB_NO_UPDATE_CHECK=1                        # fbb 每条命令都会查新版本；离线（回家）时省掉这次联网

# 版本目录取字典序最后一个（bisheng 目前 26.08.1 darwin-arm64）
bisheng_bin="$(ls -d "$FBB_INSTALL_DIR"/tools/bisheng/*/bin 2>/dev/null | tail -1)"
export HSFLASH="$(ls "$FBB_INSTALL_DIR"/tools/hs-flash/*/hsflash 2>/dev/null | tail -1)"

# 等价于 README 的 export PATH="$HOME/.hs-fbb/tools/bisheng/26.08.1/bin:$HOME/.hs-fbb/venv/bin:$PATH"
if [ -z "${AIRMOUSE_ENV_ACTIVE:-}" ]; then
    if [ -d "$FBB_INSTALL_DIR/venv/bin" ]; then
        PATH="$FBB_INSTALL_DIR/venv/bin:$PATH"
    fi
    if [ -n "$bisheng_bin" ]; then
        PATH="$bisheng_bin:$PATH"
    fi
    export PATH
    AIRMOUSE_ENV_ACTIVE=1
fi
