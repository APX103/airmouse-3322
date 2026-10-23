# AirMouse-3322

3322（HiSpark 谛听 HiDiTing V100）+ ICM20948 + EB21duino(BS21) 的星闪空鼠——**手感终版（2026-10-14 用户实测满意，tag v0.1-feel-good）**：两块板上电即用，转手腕/摆臂屏幕光标实时跟随，拔插 dongle 自动重连。

**信号链（陀螺直出架构）**：`陀螺 − 零偏(静止EMA) → 一阶滤波(τ≈1ms) → 静止门(1°/s·0.3s) → ×50px/deg → SLE(双端压缩连接参数) → USB HID`——零姿态计算（CPU 5%），全链延迟体感即时。EKF 版保留在 stable/m4-gain50 分支备胎（供锚定/混合模式演进）。

本仓库是**补丁集叠加层**：不含官方 SDK 全量，只含我们对官方库的修改（补丁）、自研资产（EKF/工具）和全部文档。官方库更新时在官方 checkout 上 `git pull` 后重放补丁即可跟随（改动的文件面很小，冲突概率低）。

## 完整文档（note 库系列，四篇闭环）

本仓库的完整原理、参数、代码片段与复现流程沉淀在笔记库（github.com/APX103/note，`embedded/` 目录）：

1. [Hi3322 ↔ EB21duino 星闪链路验证](https://github.com/APX103/note/blob/main/embedded/hi3322-sle-dongle-link-verification.html) —— 技术路线调研与源码级实锤
2. [3322 空鼠垂直切片实录](https://github.com/APX103/note/blob/main/embedded/hi3322-airmouse-vertical-slice.html) —— M0-M3：链路/挂死结案/IMU/EKF
3. [3322 空鼠调通实录与完全指南](https://github.com/APX103/note/blob/main/embedded/hi3322-airmouse-complete-guide.html) —— M4 端到端 + 环境/烧录/接线全套
4. [3322 空鼠手感终版](https://github.com/APX103/note/blob/main/embedded/hi3322-airmouse-feel-good-final.html) —— 陀螺直出架构 + 延迟追凶全案 + **13 段完整代码附录（可凭文复现本项目）**

## 仓库结构

```
patches/hs-fbb/     3322 端补丁（业务全量：IMU驱动+EKF+映射+发送+自动连接FSM+AT钩子+ekf7新增）
patches/fbb_bs2x/   dongle 端补丁（write回调→USB HID + keyscan 索引共享）
ekf7/               七态 EKF 完整源（零依赖 C99，源自 quat-zupt-ekf 差分验证版；本仓库为权威副本）
tools/              at3322.py（AT交互）/ oneshot.py（手动全序列）/ oneshot_i2c.py（I2C验线）
apply.sh            一键把补丁打进两份官方 SDK checkout
README.md           本文件
```

## 硬件条件

| 器件 | 角色 |
|---|---|
| HiSpark 谛听 V100（Hi3322，SLE 2.0） | 鼠标端：ICM20948 采样 + EKF 姿态 + SLE Central 发送 |
| 利尔达 EB21duino（BS21，SLE 1.0） | dongle 端：SLE Server 收包 + USB HID 转换 |
| Waveshare 10 DOF IMU-D（ICM20948） | I2C 接 3322 |

**接线**（3322 侧，官方排针表核对版）：

| IMU | 3322 开发板 |
|---|---|
| VCC 3.3V | J1902 PIN17/18（VDD3V3） |
| GND | J1901 PIN9/10 |
| SDA | J1901 PIN12（丝印 C0_D = I2C0_SDA/MGPIO_5） |
| SCL | J1901 PIN11（丝印 C0_C = I2C0_SCL/MGPIO_4） |

拨码 SW1901-5 保持默认 OFF（=I2C0 3.3V）。J1901 PIN7/8（I2C1）板内接屏 TP 勿用。
串口：3322 用 CH347 接 UART2（750000 波特，**适配器必须供 1.8V**）；dongle Type-C1 的 CP2105 双口，`...FDE11`=烧录+日志（115200），Type-C2=USB HID 插电脑。

## 软件条件与开发环境（Mac Apple Silicon）

1. fbb CLI + `~/.hs-fbb/`（bisheng darwin-arm64、venv 含 pyserial）——3322 编译烧录
2. hsflash 26.09.1（`~/.hs-fbb/tools/hs-flash/26.09.1/`，带 config/ 与 ChipProperties/）
3. Docker Desktop（amd64 容器）+ `fbb-wsl:toolchain` 镜像——dongle 编译
4. 官方 SDK 两份 checkout：gitcode.com/HiSpark 的 `hs-fbb` 与 `fbb_bs2x`

镜像制作（一次性）：`curl -O https://hispark-obs.obs.cn-east-3.myhuaweicloud.com/fbb_ws63_wsl.tar && docker import --platform linux/amd64 fbb_ws63_wsl.tar fbb-wsl:toolchain`

## 部署流程（从官方库到可用的空鼠）

```bash
# ① 应用补丁（SDK checkout 是干净的官方 master）
./apply.sh /path/to/hs-fbb /path/to/fbb_bs2x

# ② 编译 3322 固件（~1 分钟）
export PATH="$HOME/.hs-fbb/tools/bisheng/26.08.1/bin:$HOME/.hs-fbb/venv/bin:$PATH"
cd /path/to/hs-fbb/src && python3 build.py diting-community

# ③ 编译 dongle 固件（先 setconfig 一次，再 Docker 编译）
cd /path/to/fbb_bs2x/src && ~/.hs-fbb/venv/bin/python build/script/usr_config.py \
  --command setconfig --chip bs21e --target standard-bs21e-1100e \
  SAMPLE_ENABLE=y ENABLE_PRODUCTS_SAMPLE=y SAMPLE_SUPPORT_SLE_UART=y \
  SAMPLE_ENABLE_SLE_UART_SERVER=y SAMPLE_SUPPORT_USB_KEYBOARD=y SUPPORT_SLE_PERIPHERAL=y
docker run --rm --platform linux/amd64 -v /path/to/fbb_bs2x:/sdk -w /sdk/src fbb-wsl:toolchain \
  sh -c 'python3 build.py standard-bs21e-1100e -c'

# ④ 烧录 3322（DFU 自动 ~97s；板子醒着，AT 口可响应。手动兜底见下节）
cd /path/to/hs-fbb/src && fbb flash -f output/3322/fwpkg/diting-community.fwpkg --chip 3322 -d --json-summary

# ⑤ 烧录 dongle（DE11 口；提示 reset 时拔功耗跳帽 2s 再插回，或 UART 发 AT+REBOOT 软复位）
cd ~/.hs-fbb/tools/hs-flash/26.09.1
./hsflash write -c bs21e -p /dev/cu.usbserial-01C7FDE11 -b 115200 \
  -f /path/to/fbb_bs2x/src/output/bs21e/fwpkg/standard-bs21e-1100e/bs21e_all_in_one.fwpkg

# ⑥ 使用：dongle Type-C2 插电脑 → 3322 上电 → 15 秒内自动连接 → 转板子，屏幕光标动
```

**3322 烧录手动兜底**（AT 无响应时）：`~/.hs-fbb/tools/hs-flash/26.09.1/hsflash write -c 3322 -p /dev/cu.usbmodem11401 -b 750000 -f <fwpkg>`，看到 `Please reset` 后给板子断电 2 秒再上电。

## 调参速查（都在 patches/hs-fbb 的 sle_client.c 里）

| 旋钮 | 宏/位置 | 当前值 |
|---|---|---|
| 灵敏度 | `AIM_RATE_GAIN` | 50 px/deg |
| 死区 | `AIM_DEADBAND_DEG` | 0.03° |
| 轴方向 | aim_ekf_step 尾部两行符号 | dx=-Δyaw(绕Z), dy=+Δroll(绕X) |
| 跟手度（滤波） | ekf7/ekf7_filter.c:18-19 | gyro 0.5 / acc 0.3 |
| 发送节拍 | `AIM_DEMO_TICK_MS` | 10（100Hz） |

改动后重编译重烧（§部署 ②④，约 3 分钟一轮）。

## 设计要点（为什么是现在这个样子）

- **Central-Server 架构**：dongle 广播做 server，3322 做 Central 主动连（连接发起权在算力强的一端，两端固件可全自产）。
- **3322 开机钩子 = AT 框架初始化**（sle_com.c 的 cmd_register 尾部）——`app_run` 的 initcall 在 3322 OpenHarmony 基线不被驱动；任务不能挂在 SLE INIT 里（FSM 又在任务里，鸡生蛋死锁）。
- **无确认写**（is_need_confirm=false）：确认写每包等对端 RTT~110ms（36Hz 刷新+延迟双杀）；无确认即发即走，背压靠写失败退避。曾因确认事件无人消费积压挂死（0x8000600d/0x80006016 系列）——回调注册要用刚发现的 service uuid（官方 g_client_app_uuid 时序 bug）。
- **轴映射实测定稿**：绕 Z→dx、绕 X→dy（安装姿态下左右摆=绕Z、翻腕=绕X；pitch 不参与）。
- **EKF 无条件每拍跑**，绝不 gate 在连接状态后（调算法不被链路绑架）。
- 完整踩坑史与判据：note 库《3322 空鼠调通实录与完全指南》（hi3322-airmouse-complete-guide.html）。

## 已知问题 / Roadmap

- ICM20948 bank2 量程写入不生效（按默认 ±2g/±250dps 刻度用，重力实测校准过）
- 静止时姿态显示缓慢漂移（motionless 冻结输出挡住位移，不影响使用）
- 待做：按键（keys 字段已预留）、下行双工（notify+CCCD）、走廊式混合控制律（姿态锚定）、量产换 UUID
- **提速路线图（2026-10-14 教训）**：200Hz 全链节拍实测打死板子（EKF double 软浮点每拍 2-4ms 超预算→看门狗复位循环；dongle 半开连接加重连失败）。正确路径：① 传感器 ODR 解耦（ICM20948 内部 562Hz 过采样 + 固件 100Hz 直读最新样本，省 ~4ms 样本老化，前置=修 bank2 写入）② EKF 单精度 float 化（计算量降 5-10×，之后 200Hz 才可行，用 quat-zupt-ekf 差分框架重验证）③ gyro_filt_k 试探 0.7。本分支=100Hz 稳定态，实验请开新分支。

## 输入设备扩展（feat/buttons 分支）

### 已实现：鼠标左右键（本分支）

按钮：轻触开关 ×2（4 脚的挑对角两脚用），**零电阻**（3322 内部上拉）：

| 按钮 | 一脚接 J1902 | 另一脚接 | 3322 引脚号 |
|---|---|---|---|
| 左键 | PIN 4（丝印 `CS`） | GND（PIN 5/6） | S_MGPIO15 |
| 右键 | PIN 7（丝印 `RX4`） | GND（PIN 5/6） | S_AGPIO12 |

- 原理：内部上拉抬到 3.3V 读 1，按下接地读 0；2 拍消抖（20ms）
- 拨码全默认（SW1901-1/-3 OFF = 3.3V 域）
- 协议：keys 字节 bit0=左键 bit1=右键（bit2/3 预留 Q/E）；**dongle 端零改动不用重烧**
- 烧录本分支后验证：串口看 `[AIM][BTN] keys=0x1/0x2/0x0`；插 USB 口直接点选/拖拽

### 排队中（方案已定稿：work/sle-link-test/input-devices-plan.md）

**Q/E 倾斜（FPS lean，零硬件）**：俯仰 ≥45° 触发（+侧 Q/−侧 E）、35° 释放；
加速度计算俯仰角（atan2 + 轻低通），不依赖 EKF；dongle 需加键盘 report 分支（HID usage Q=0x14/E=0x08）。

**编码器灵敏度旋钮（微雪 20 脉冲/圈带按键）**：

| 编码器 | 接 J1902 | 说明 |
|---|---|---|
| SIA | PIN 1（`DO`，S_MGPIO13） | 正交解码 A |
| SIB | PIN 2（`DI`，S_MGPIO12） | 正交解码 B |
| SW | PIN 3（`CLK`，S_MGPIO14） | 按下=灵敏度复位 50 |
| VCC | PIN 17（3.3V） | 模块板载上拉供电 |
| GND | PIN 5 | 共地 |

行为：1kHz 独立任务正交解码，旋一格 ±2 px/deg（范围 10~150），串口实时打印 gain。

### J1902 总占用图（最终形态）

```
PIN 1  ← 编码器 SIA     PIN 10 ─（空）
PIN 2  ← 编码器 SIB     PIN 11 ─（空，预留）
PIN 3  ← 编码器 SW      PIN 12 ─（空，预留）
PIN 4  ← 左键           PIN 13 ─（空）
PIN 5  ← GND 公共       PIN 14 ─（空）
PIN 6  ← GND 备用       PIN 15 ─（空）
PIN 7  ← 右键           PIN 16 ─（空）
PIN 8  ─（避开：文档勘误脚） PIN 17 ← 3.3V（编码器 VCC）
PIN 9  ─（原 R 键位，已取消） PIN 18 ← 3.3V 备用
```
