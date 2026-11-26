# AirMouse-3322

3322（HiSpark 谛听 HiDiTing V100）+ ICM20948 + EB21duino(BS21) 的星闪空鼠——**手感终版（2026-10-14 用户实测满意，tag v0.1-feel-good）**：两块板上电即用，转手腕/摆臂屏幕光标实时跟随，拔插 dongle 自动重连。

**信号链（陀螺直出架构）**：`陀螺 − 零偏(静止EMA) → 一阶滤波(τ≈1ms) → 静止门(1°/s·0.3s) → ×50px/deg → SLE(双端压缩连接参数) → USB HID`——零姿态计算（CPU 5%），全链延迟体感即时。EKF 版保留在 stable/m4-gain50 分支备胎（供锚定/混合模式演进）。注意：v0.1-feel-good 里读陀螺仪的字节下标有错（见「平滑度修正」），feat/encoder 分支已修，默认灵敏度相应改为 42。

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
tools/              at3322.py（AT交互）/ oneshot.py（手动全序列）/ oneshot_i2c.py（I2C验线）/ enc_sim/（编码器解码的 Mac 端仿真）/ motion_sim/（陀螺→光标位移的 Mac 端仿真）/ btn_sim/（按键扫描仿真，从补丁抽真实源码）/ win32/center_agent.py（置中键的 PC 代理：中键→光标置屏幕中心）
apply.sh            一键把补丁打进两份官方 SDK checkout
README.md           本文件
```

## 硬件条件

| 器件 | 角色 |
|---|---|
| HiSpark 谛听 V100（Hi3322，SLE 2.0） | 鼠标端：ICM20948 采样 + EKF 姿态 + SLE Central 发送 |
| 利尔达 EB21duino（BS21，SLE 1.0） | dongle 端：SLE Server 收包 + USB HID 转换 |
| Waveshare 10 DOF IMU-D（ICM20948） | I2C 接 3322 |

**接线**（3322 侧，以板上丝印为准，丝印对照见文末「排针丝印对照表」）：

| IMU | 3322 开发板丝印 | 排针 / 脚号 | 说明 |
|---|---|---|---|
| VCC 3.3V | `3.3V` | J1902 第 17 脚 | 旁边第 18 脚是 `1.8V`，别接 |
| GND | `GND` | J1901 第 9 或 10 脚 | |
| SDA | `C0_D` | J1901 第 12 脚 | I2C0_SDA / MGPIO_5 |
| SCL | `C0_C` | J1901 第 11 脚 | I2C0_SCL / MGPIO_4 |

拨码 SW1901-5 保持默认 OFF（=I2C0 3.3V）。J1901 的 `I2C1_SCL`/`I2C1_SDA`（第 7、8 脚）板内接屏 TP，勿用。
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

**3322 串口烧录**（AT 口没响应、或板子的 USB 没有数据线时用；2026-11-24 实测）：

1. 包里必须带 loader 镜像。只编应用打出的 6 镜像包只能走 USB DFU，串口工具会报 `fwpkg does not contain a loader image`。先 `python3 build.py 3322-loaderboot`，再 `python3 tools/pkg/packet.py 3322 diting-community None None`，得到 7 镜像包。
2. 板子断电。主机先开工具等握手：`hsflash write -c 3322 -p <串口> -b 750000 --switch-after-loader -f <7镜像包>`。
3. 板子上电。引导 ROM 在开机约 10 秒内应答（实测 3 秒内），之后 loader 切到 750000 写入，约 5 分钟。

- **握手阶段是 115200**，不是 AT 口的 750000；`--switch-after-loader` 负责在 loader 之后才切到 750000。直接 `-b 750000` 握手不会成功。
- debugger 通常由板子供电，板子断电时串口设备消失、上电后重新枚举。主机侧要在串口一回来就重新启动工具，原来握着旧句柄的进程等不到。
- 写入期间不要动电源。进度条只在传数据时走，擦除时停着不动是正常的。

## 调参速查（都在 patches/hs-fbb 的 sle_client.c 里）

| 旋钮 | 宏/位置 | 当前值 |
|---|---|---|
| 灵敏度 | 默认 `AIM_GAIN_DEFAULT`，运行时变量 `g_aim_gain` | 默认 42 px/deg（等于旧版标称 50 的实际手感，见「平滑度修正」）；编码器旋钮可调，范围 10~150 |
| 死区 | `AIM_DEADBAND_DEG` | 0.03° |
| 轴方向 | aim_ekf_step 尾部两行符号 | dx=-Δyaw(绕Z), dy=+Δroll(绕X) |
| 跟手度（滤波） | `AIM_GYRO_LPF_K`（陀螺直出） | 0.8 |
| 静止判据 | `AIM_STILL_DPS` / `AIM_STILL_HOLD_S` | 1°/s 持续 0.3 秒 |
| 采样节拍 | `AIM_DEMO_TICK_MS` | 10（固定节拍，真实 100Hz） |

改动后重编译重烧（§部署 ②④）。

## 平滑度修正（2026-11-24，feat/encoder 分支）

用户反馈光标"动一下、停一下"。查出三层数据通路上的原因，都已在固件里修掉，并在 Mac 上用仿真验证（`tools/motion_sim/run.sh`，测的是从固件源码里抽出来的真实代码）。另外还有一个卡顿没定位到，见本节的"未解决"。

| 原因 | 表现 | 修法 |
|---|---|---|
| 读陀螺仪时每个轴的低字节取错了位置（取到下一个轴的高字节，Z 轴取到温度的高字节）。从初次提交起就在，v0.1-feel-good 里也有 | 每个轴只有高 8 位有效，读数被粗化成约 2°/s 一档：手慢慢加速时光标速度一级一级跳，很慢时干脆不动 | 改正字节下标 |
| 主循环"干完活再睡 10ms"，位移却按固定 10ms 算 | 周期实际约 11.8ms，还随干活时间抖动；循环一慢，光标走的距离就按比例缩水 | 固定节拍（到点就采样）+ 按真实经过时间积分 |
| 每拍位移直接取整，零头扔掉 | 慢速时每拍不到 1 像素，光标不动 | 零头累积到下一拍 |

**未解决：连上 dongle 且空闲时，主循环每约 25 拍卡一次、每次约 600 毫秒**（统计行约 3.3 秒一行，`loop n=100 maxgap≈650ms late=4`），光标因此"动一下、停一下"。上板已排除：

- 编码器的周期轮询任务（改成边沿中断后卡顿不变）。
- 按键引脚的配置和读取（把两个按键脚临时改回复用功能，卡顿不变；读各引脚都是约 2.8 毫秒）。
- 系统睡眠或冻住（同时从电脑每 20 毫秒发一次 AT 探测，8 秒 390 次全部 1.4 毫秒响应；只有主循环被挡）。
- `AT+PM=0`（无效）。

原因还没定位。为此在固件里加了定位手段，之后的实验不用再烧录：`AT+AIM` 打印主循环每一步的最大/平均耗时和唤醒间隔分布；`AT+AIMDBG=<mask>` 运行时关掉某一步。

- **灵敏度默认值从 50 改成 42**：旧版循环 11.8ms 一拍却按 10ms 积分，标称 50 的实际手感约等于现在的 42。想要现在定义下的 50，把 `AIM_GAIN_DEFAULT` 改回 50 或用旋钮调。
- 低于静止判据（1°/s）的拍不积分，手抖不会因为零头累积变成漂移。
- 发不出去的位移（退避中、写失败）留在"待发"里下一拍接着发，运动不丢；未连接时清零，重连后不会跳。
- 仿真结果（`tools/motion_sim/run.sh`，17 项全过）：慢速 2°/s 转 2 秒走出 167 像素（理想 168，旧固件 0）；循环晚醒（10/10/40/10/10/25ms）时走出 13229 像素（理想 13230，旧固件 7200）；0.7°/s 的手抖 10 秒走 0 像素。**上板手感待用户确认。**

在串口上看是否正常（统计行每 100 拍打一行，移动和静止时都应该一样）：

- `loop n=100 maxgap=10ms late=0`：n 是这一行的拍数，maxgap 是相邻两拍的最大间隔，late 是间隔超过 11ms 的拍数。
- `fail=0`：发包失败数。增长说明链路在排队，位移会先攒着、之后补发。

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
- 待做：下行双工（notify+CCCD）、走廊式混合控制律（姿态锚定）、量产换 UUID（按键和编码器已实现，见下文）
- **提速路线图（2026-10-14 教训）**：200Hz 全链节拍实测打死板子（EKF double 软浮点每拍 2-4ms 超预算→看门狗复位循环；dongle 半开连接加重连失败）。正确路径：① 传感器 ODR 解耦（ICM20948 内部 562Hz 过采样 + 固件 100Hz 直读最新样本，省 ~4ms 样本老化，前置=修 bank2 写入）② EKF 单精度 float 化（计算量降 5-10×，之后 200Hz 才可行，用 quat-zupt-ekf 差分框架重验证）③ gyro_filt_k 试探 0.7。本分支=100Hz 稳定态，实验请开新分支。

## 输入设备扩展（feat/buttons → feat/encoder 分支）

### 已实现：鼠标三个键（feat/buttons → feat/center-key）

左/右键：轻触开关 ×2（4 脚的挑对角两脚用），一脚接排针，另一脚接 GND。置中键：**板上现成的"自定义按键"**（SW2102，丝印 `AIO17_KEY`，在开机键旁边），零接线。

| 按钮 | 接哪里 | 另一脚 | 3322 引脚（编号） | 空脚电平 |
|---|---|---|---|---|
| 左键 | J1902 `A_20`（14 脚） | `GND`（5/6 脚） | S_AGPIO20（95） | 1 |
| 右键 | J1902 `RX4`（7 脚） | `GND`（5/6 脚） | S_AGPIO12（87） | 1 |
| 置中键 | 板上自定义按键 SW2102 | —（板上已接好） | S_AGPIO17（92） | 1 |

> **2026-11-26 起左键从 `CS`+3.3V 方案迁到 `A_20`+GND**：3.3V 引脚全被编码器/IMU 占用，且实测 J1902 上一批脚（CS/DO/DI/CLK 的 ET5014 组、TX3/TX4 的 UART TX）被板级驱动钉死在 0，"空闲高、按下拉低"的脚只剩 UART4/UART3 组的输入类脚（RX4/CTS4/RTS4/RX3/A_20）。`CS` 方案电气上仍成立，只是没有空闲的 3.3V。
>
> **2026-11-27 置中键（原理图 `Sunniwell HiDiTing DevBoard 1.1_SCH.pdf` 第 21 页 KEY 页实锤）**：板上三个按键通道电路完全相同 = 开关接地 + 100nF 电容到地（硬件消抖）+ ESD3V3A 防静电 + 0Ω 串联——"有电阻保护的 IO 按钮接地触发"的现成实例。自定义按键 = `S_AGPIO17`、旁边还有一个同电路备用键 SW2103 = `S_AGPIO2`（77，丝印在中键位），中间是 `PWR_KEY`（PMU 电源键，**不能碰**）。交叉引用验证：S_AGPIO17/S_AGPIO2 除 KEY 页外不接任何其他电路，与 J1902 接线零冲突。板上无外部上拉，空闲高靠芯片内部上拉（固件已开）；100nF 上电后几毫秒充到高电平，远在开机 120ms 采样窗之前。

- **不用接任何电阻**，道理见下一节。三个键都是"空闲高、按下接地"（左/右接线见上表，置中键板上已接好）。
- 固件开机时先量每个脚空着时的电平，记作"没按下"；之后电平与它相反才算"按下"。没接按钮的空脚永远不会误判成按下。
- 开机后约 0.1 秒是采样时间，这期间不要按着按键上电，否则那个键会判反（重新上电恢复）。
- 拨码全默认（SW1901-1/-3 OFF = 3.3V 域）
- 协议：keys 字节 bit0=左键 bit1=右键 bit2=置中键；**dongle 端零改动不用重烧**（bit2 过 `&0x07` 就是 USB 中键）
- 置中语义在 PC 侧：`tools/win32/center_agent.py`（中键按下→光标移屏幕中心并吞键，`Ctrl+Alt+C` 切换、Ctrl+C 退出；没运行时置中键=普通中键）。标准 HID 鼠标只有相对位移、固件不知道指针位置，"回屏幕中心"只能在 PC 做
- 验证：串口每秒统计行里有 `btn[L R M]=<当前电平> open=<空脚电平>`，行尾 `keys=<三键>`。不按时 `keys=000` 且 `btn` 等于 `open`；按下左/右/置中 `keys` 变 `100`/`010`/`001`。dongle 插电脑后可直接点选、拖拽；置中键配 center_agent 验证
- 逻辑仿真：`tools/btn_sim/`（从补丁抽真实 init/scan 源码跑时间线断言）

### 空脚要不要接电阻

按钮和编码器的触点只是开关：闭合时把排针脚接到另一根线上，断开时这根脚就**悬空**——没有东西规定它的电压，芯片可能随机读成 0 或 1。上拉/下拉电阻的用处就是开关断开时给脚一个确定的电平（接 3.3V 的叫上拉，断开读 1；接 GND 的叫下拉，断开读 0），同时避免闭合时把电源直接短路。

这块板子上**不用自己加电阻**，因为每根脚断开时的电平板子已经定好了：

- 芯片自带内部上拉，固件里已打开，寄存器读回也确认置位了（`AT+IOCONFIG=5,<引脚号>,0`）。但 J1902 的信号脚到芯片之间还有一级自动辨向电平转换器（左键和编码器三个脚是 ET5014，右键脚是 UM3204QT），芯片内部上下拉压不过它。原理图上两侧预留的 2.2K 上拉也都没贴。
- 实测（2026-11-24 上板，固件开机自检，每格是 20 次采样里读到高的次数）：

| 脚 | 无上下拉 | 内部下拉 | 内部上拉 |
|---|---|---|---|
| 编码器 A/B/SW、左键（S_MGPIO12–15） | 0 | 0 | 0 |
| 右键（S_AGPIO12） | 20 | 20 | 20 |

- 五个脚的空脚电平都由板子钉死：前四根是 0，右键是 1。所以断开读 0 的脚，开关闭合要接 3.3V 才变成 1；断开读 1 的脚，闭合要接 GND 才变成 0。另一脚接对，电阻就省了。
- 开机日志 `[AIM][IO] … none=… down=… up=…` 就是这张表：同一根脚三个数一样，说明芯片内部上下拉对它不起作用。

只有想用 GND 做公共端（编码器接法 B）时才需要在排针侧加上拉电阻。这类转换器输出很弱，TI 同架构芯片（TXB0104）的手册写的是外部上拉/下拉不建议加、必须加时大于 50kΩ。

### 已实现：编码器灵敏度旋钮（feat/encoder 分支，dongle 零改动）

Waveshare [Rotation Sensor](https://www.waveshare.net/wiki/Rotation_Sensor)（EC11 类增量编码器，20 脉冲/圈，带按键；工作电压 3.0–5.3V）。**接线 = 官方标准五线**（2026-11-25 上板实测可用）：

| 编码器 | 接 J1902 丝印 | 脚号 | 3322 引脚 |
|---|---|---|---|
| SIA | `RX3` | 13 | S_AGPIO8 |
| SIB | `CTS4` | 8 | S_AGPIO13 |
| SW | `RTS4` | 10 | S_AGPIO14 |
| VCC | `3.3V` | 17 | — |
| GND | `GND` | 5 | — |

- **断电接线**；拨码 SW1901 保持全 OFF（J1902 为 3.3V 域）。**第 18 脚 `1.8V` 不是 3.3V**。
- 为什么不用 DO/DI/CLK（U1801/ET5014 那组）：那组排针侧被转换器**死钉在 0**（实测内部上拉都拉不动，模块 10K 更翻不动），开关闭合造不出跳变。UART4 组（U1804/UM3204QT）排针侧空闲高电平（右键 RX4 同组实测佐证），标准接法下闭合拉低 = 官方文档行为。`TX4` 虽同组但被板级驱动钉在 0，实测弃用。
- 固件按**开机实测空脚电平**判闭合（极性自适应，标准接法下 open=111）；三根线纯输入，接错不损坏。
- 行为：10ms 轮询采样 + 查表正交解码；旋一格 ±2 px/deg，钳位 10~150；SW 复位 42。**采样不用边沿中断：2026-11-25 实测 AGPIO 边沿中断注册成功但永不触发**（旋转时电平明明在变、irq 恒 0），退回轮询（`AIM_ENC_IRQ=0`）；轮询任务 p5+10ms 占空约 0.1%，动态优先级方案下无碍。
- 解码算法在 Mac 上仿真验证过（`tools/enc_sim/`，152 项全过，含触点抖动）。**上板已验证**：SW 按下识别 4/4、旋转改变增益（42→38）；**方向映射与每格步数（±2/格）尚未复核**——顺时针若让增益变小，把 `AIM_ENC_DIR_SIGN` 改 `-1` 重烧即可。

上板调试（串口 750000）：

1. 每秒统计行：`gain=… enc[A B SW]=<当前电平> open=<空脚电平>`，标准接法下两者均 `111`；不转不动时 `enc` 恒等于 `open`。
2. 旋一格看 `[AIM][ENC] up/down gain=…`；按一下 SW 看 `sw press: gain reset 42`。
3. 空闲健康检查（已达标，2026-11-25）：连 dongle 静置，`loop n=100 maxgap=10ms late=0`，`AT+AIM` 里 `>500ms` 卡顿为 0、`sleep overrun 0ms`（v8.1 动态优先级修复）。

板载 AT 命令（串口 750000，可用 `tools/at3322.py send <串口> "<命令>"` 发）：

| 命令 | 作用 |
|---|---|
| `AT+IOCONFIG=5,<引脚号>,0` | 读回管脚寄存器：`mode ds st ie pull`（pull：0 无、1 下拉、2 上拉） |
| `AT+IOCONFIG=8,<引脚号>,0` | 读引脚的方向和电平：`dir val` |
| `AT+REBOOT` | 软复位，不用断电就能重抓开机日志（串口烧录的上电握手仍需真正断电） |
| `AT+PM=0` | 关低功耗，复位后恢复 |
| `AT+AIM` | 本项目加的：打印主循环每一步（状态机/读传感器/查连接/读按键/发包/统计行）的最大、平均耗时和卡住的次数，以及相邻两拍的唤醒间隔分布。读完清零 |
| `AT+AIMDBG=<mask>` | 本项目加的：运行时关掉主循环里的某一步，用来逐项排除。0x1 不读按键，0x2 查连接改用回调置的标志，0x4 不读 IMU，0x8 不跑连接状态机，0x10 空闲也强制发包；写 0 恢复 |

引脚号（AT+IOCONFIG 用，出自 product_pin.h：MGPIO_n=32+n、AGPIO_n=75+n）：编码器 SIA/SIB/SW = 83/88/89，左键 `A_20`=S_AGPIO20=95，右键=87，置中键（板上自定义按键）=92，备用板上键 SW2103=77。

调试点（都在 sle_client.c 的编码器段）：

| 现象 | 改哪里 |
|---|---|
| 顺时针反而是减 | `AIM_ENC_DIR_SIGN` 改 `(-1)`，或把 SIA/SIB 两根线对调 |
| 转两格才变一次 | `AIM_ENC_STEPS_PER_DETENT` 改 `2`（半定位编码器） |
| 转了没反应 | 看 `enc` 转动时是否变化；不变说明触点闭合前后电平一样——接法 B 缺上拉，或模块自带上拉又用了接法 A |
| 转动时 `enc` 会变但不出格、`sample mode` 是 `poll` | 中断没注册上，退回了 100Hz 轮询；把开机日志发出来看 |
| 静止时乱跳格 | 看 `enc` 是否稳定等于 `open`；不稳定说明空脚电平在漂，改成接法 B 并各加 100kΩ 上拉 |

### 排队中（方案已定稿：work/sle-link-test/input-devices-plan.md）

**Q/E 倾斜（FPS lean，零硬件）**：俯仰 ≥45° 触发（+侧 Q/−侧 E）、35° 释放；
加速度计算俯仰角（atan2 + 轻低通），不依赖 EKF；dongle 需加键盘 report 分支（HID usage Q=0x14/E=0x08）。

### 排针丝印对照表

来自板子的官方文档《Sunniwell HiDiTing DevBoard 使用指南》3.3 节（hs-fbb 仓库里 `vendor/Sunniwell_HiDiTing_Dev_Board/hardware/hardware-design-guide/sunniwell-hiditing-dev-board.md`，只在 `origin/master` 上有），已和板子照片、原理图核对。

**J1902（1.8V/3.3V GPIO 排针，本项目主要用的这排）**：2×9，第 1 脚在靠近 MIPI 屏座的那一端。单数脚是靠板内侧那排，双数脚是靠板边那排，每列并排印着两个丝印。

| 丝印 | 脚号 | 位置 | 芯片引脚 / 功能 | 本项目用途 |
|---|---|---|---|---|
| `DO` | 1 | 内排 | S_MGPIO13，SPI0_DO | 编码器 SIA |
| `DI` | 2 | 边排 | S_MGPIO12，SPI0_DI | 编码器 SIB |
| `CLK` | 3 | 内排 | S_MGPIO14，SPI0_CLK | 编码器 SW |
| `CS` | 4 | 边排 | S_MGPIO15，SPI0_CS | 左键 |
| `GND` | 5 | 内排 | 地 | 右键的另一脚；接法 B 的编码器公共端 |
| `GND` | 6 | 边排 | 地 | 空 |
| `RX4` | 7 | 内排 | S_AGPIO12，UART4_RXD | 右键 |
| `CTS4` | 8 | 边排 | S_AGPIO13，UART4_CTS（见下方勘误） | 空 |
| `TX4` | 9 | 内排 | S_AGPIO11，UART4_TXD | 空 |
| `RTS4` | 10 | 边排 | S_AGPIO14，UART4_RTS | 空 |
| `TX3` | 11 | 内排 | S_AGPIO7，UART3_TXD | 空 |
| `PWM` | 12 | 边排 | S_MGPIO16，PWM | 空（预留） |
| `RX3` | 13 | 内排 | S_AGPIO8，UART3_RXD（见下方勘误） | 空 |
| `A_20` | 14 | 边排 | S_AGPIO20 | 空（预留） |
| `GND` | 15 | 内排 | 地 | 空 |
| `GND` | 16 | 边排 | 地 | 空 |
| `3.3V` | 17 | 内排 | 3.3V 电源 | 编码器公共端、左键的另一脚 |
| `1.8V` | 18 | 边排 | 1.8V 电源 | 别当 3.3V 用 |

拨码 SW1901 全 OFF 时，`DO` 到 `CS` 和 `RX4` 到 `RTS4` 这些信号脚都是 3.3V 电平。

**J1901（1.8V/3.3V I2C 排针）**：2×6。

| 丝印 | 脚号 | 芯片引脚 / 功能 | 本项目用途 |
|---|---|---|---|
| `I2C3_SCL` | 1 | S_MGPIO10，I2C3_SCL | 空 |
| `I2C3_SDA` | 2 | S_MGPIO11，I2C3_SDA | 空 |
| `GND` | 3、4 | 地 | 空 |
| `I2C2_SCL` | 5 | S_MGPIO8，I2C2_SCL | 空 |
| `I2C2_SDA` | 6 | S_MGPIO9，I2C2_SDA | 空 |
| `I2C1_SCL` | 7 | S_MGPIO6，I2C1_SCL | 勿用：板内接着屏幕触摸 |
| `I2C1_SDA` | 8 | S_MGPIO7，I2C1_SDA | 勿用：板内接着屏幕触摸 |
| `GND` | 9、10 | 地 | IMU 的 GND |
| `C0_C` | 11 | S_MGPIO4，I2C0_SCL | IMU 的 SCL |
| `C0_D` | 12 | S_MGPIO5，I2C0_SDA | IMU 的 SDA |

**J1402（1.8V GPIO 排针）**：2×9，信号脚**只有 1.8V 电平**，本项目没用。丝印有 `H_15`（第 1 脚）、`H_14`（3）、`H_10`（5）、`H_12`（2）、`H_13`（4）、`H_11`（6）这六个 SDIO 脚，`A_21`、`A_22`、`A_23`、`A_24` 四个 I2S 脚，`ADC3`、`ADC2` 两个 ADC 脚，以及 `GND`、`3.3V`、`1.8V`。

官方文档和原理图有出入的地方，上表按原理图写：

- J1902 第 8 脚 `CTS4`：文档写 MGPIO_14，原理图是 S_AGPIO_13。
- J1902 第 13 脚 `RX3`：文档写 MGPIO_11，原理图是 S_AGPIO_8。
