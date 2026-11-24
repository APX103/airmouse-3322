# 交接记录（最新：2026-11-25 05:00；历史部分见下方 2026-11-24 的记录）

给下一个会话：读完这个文件就知道上下文和接下来要做的事。细节文档在本仓库 `README.md` 和 `REVIEW.md`，长期记忆在会话自带的 memory 目录，这里不重复。

## 0. 现状（2026-11-25 05:00，全部叠加式提交，未推送）

**鼠标本体已定版**：v8.4 固件在板上，用户实测认可（"这个版本非常好"）。连接态空闲零卡顿、零漂移。

- 卡顿根因（已解决）：蓝牙栈侧任务周期性饿死低优先级主循环 + 零偏 EMA 死锁。修复 = 动态优先级（开机 28 号配对、连接后切 6 号）+ EMA 去保持窗 + IMU 读防御 + I2C 等待超时 100ms（补丁 0002）。数据对比见 `REVIEW.md`。
- 提交链（全部叠加）：`8ca2ba3`(tag v8.1 封版) → `5340d11`(README 原理图修正) → `c2faa01`(编码器实际接线 v8.2~v8.4)。
- 固件包：`.fbb/firmware/diting-community-feat-encoder-serial-v8*.fwpkg`（板上 = v8.4，sha256 前 16 位 `f768e4a56311e26a`）。
- 烧录唯一可靠流程：布防 `.fbb/tmp/flash/vXX_write.sh` → 拔调试器 USB 线 3 秒 → 插回。AT+REBOOT / CMD_RESET / USBDFUTRIGGER / DFU 都不行（见 memory「3322 serial flashing procedure」）。

**编码器：链路已通，剩两个小验证**（接线表见 README「编码器灵敏度旋钮」节，标准五线）：

- 已验证：SW 按下 4/4 识别；旋转改变增益（42→38，轮询采样工作）。
- 待验证：① 顺时针方向是否 gain 增加（反了改 `AIM_ENC_DIR_SIGN=-1`）；② 一格是否 ±2（两格才变则 `AIM_ENC_STEPS_PER_DETENT` 改 2）。
- 踩过的坑：J1902 的 DO/DI/CLK 组（ET5014）和 TX4 被板级驱动钉死在 0，编码器改用 UART4 组（RX3/CTS4/RTS4，空闲高）；**AGPIO 边沿中断注册成功但永不触发**，采样已退回 10ms 轮询（`AIM_ENC_IRQ=0`）。

**按钮：还没接线**（用户说明天接）。左键 `CS`(4脚)→另一脚接 `3.3V`(17脚)；右键 `RX4`(7脚)→另一脚接 `GND`(5脚)。固件按空脚电平自适应。

**REST/历史部分（2026-11-24 的交接内容）大多已过时**：恢复原厂 flash 的任务用户已明确改为"继续开发烧 v7+"（后续已烧到 v8.4）；"卡顿未定位"已解决；`bt_sle_is_connected` 已证实是本地函数。以下历史保留供参考。

---

# 以下为 2026-11-24 04:00 的历史记录（部分已过时，见上方 2026-11-25 现状）

## 0. 一句话现状（历史）

3322 空鼠项目在 `feat/encoder` 分支上做编码器和手感修正，做到一半。**用户睡前提的最后一件事还没做：把最开始备份的整片 flash 烧回 3322，恢复成今晚动手之前的样子。** 下一步先做这件事。

## 1. 下一步任务：恢复原始固件

用户原话（2026-11-24 03:55）：“帮我把最开始备份的二进制烧录到3322里吧。不想浪费时间了。”

### 备份文件

- 路径：`.fbb/backup/3322-20261123/nor1_0x26000000_16M.bin`（NOR1 整片，起始地址 0x26000000）
- 大小 16,777,216 字节；sha256 `62c2de2f6b10c46ea60d6cec26e09b196e2e10df31f9c1e084c6ec6587e9b3fd`
- 2026-11-24 04:00 重新校验过，和当初记录的 `.sha256` 文件一致。
- 这是今晚第一次写 flash **之前**读出来的，内容完整可信。

### 为什么必须整片写回，不能用“重烧一版 main”代替

今晚核对过（脚本逻辑见本节末）：

- 备份里的引导镜像（`ssb`、`ssb_backup`，0x26001000 / 0x26011000）和 SDK 打出来的包里的**不一样**。
- 备份里的主程序（0x260C0000）和 `.fbb/firmware/diting-community-main.fwpkg` 里的**不一样**（从镜像偏移 0x124 起就有差异）。
- NV（0x26FFA000）有 124 字节不同。
- 标准固件包覆盖的分区之外还有约 1570 段非空数据（从 0x26021000 开始）。
- 只有 rootkey 区（0x26000000，2432 字节）和包里的一致。

所以板子原来的固件是用户更早用别的版本烧的，本机没有对应的包。要恢复只能把整片备份按地址写回去。

### 工具的约束（已查清）

- 板子的 USB 数据线不通，只能走串口。烧录工具是 `.fbb/tools/hs-flash/26.09.1/hsflash`。
- 串口方式下 `hsflash write -f` 只认 `.fwpkg`，或者一个 `.xml` 分区表（格式未知，工具包里没有样例，`strings` 也没找到足够的标签名）。原始 `.bin` 直接写只在 USB DFU 方式下支持（手册里的 `hsflash write -d -f app.bin`）。
- `hsflash parse -f <fwpkg>` 不接板子，能打印包里每个镜像的地址、写入大小、擦除大小、类型。当前包的内容：

  | 镜像 | 地址 | 写入大小 | 擦除大小 | 类型 |
  |---|---|---|---|---|
  | loaderboot_signed.bin | 0x00000000 | 0x15650 | 0x10000 | LOADER (0) |
  | rootkey_partition_cfg.bin | 0x26000000 | 0x980 | 0x1000 | NORMAL (1) |
  | ssb_signed.bin | 0x26001000 | 0xB2E0 | 0x10000 | NORMAL (1) |
  | ssb_backup_signed.bin | 0x26011000 | 0xB2E0 | 0x10000 | NORMAL (1) |
  | application_signed.bin | 0x260C0000 | 约 0x842000 | 0x880000 | NORMAL (1) |
  | 3322_all_nv.bin | 0x26FFA000 | 0x2000 | 0x6000 | NORMAL (1) |
  | database.7z | 0x0F000000 | — | — | DATABASE (16) |

  镜像在包里基本是原样连续存放的（在包里搜备份分区的头 256 字节能直接找到偏移）。

- 备份当初是这样读出来的（在 `.fbb/tmp/flash/backup_watch.sh` 里）：
  `hsflash read -c 3322 -p <串口> -b 750000 -a 0x26000000 -z 0x1000000 -f <含 loader 的 fwpkg> -o <输出文件>`

### 建议路线（都没验证，按这个顺序试）

1. **把整片备份打成一个固件包再烧**（最可行）。包 = loader 镜像 + 一个地址 0x26000000、大小 0x1000000、类型 NORMAL 的镜像（内容就是备份文件）。打包逻辑在 SDK 里：
   - 入口 `sdk/hs-fbb/src/tools/pkg/packet.py <chip> <target> <defines> <sector_cfg>`，3322 会转到 `sdk/hs-fbb/src/tools/pkg/chip_packet/3322/packet.py` 的 `make_all_in_one_packet(pack_style_str)`（540 行）。
   - 那里用 `"<文件路径>|<类型>|<擦除大小>|<标志>"` 这样的字符串列镜像（例如 loader 是 `loaderboot_signed.bin|0|0x10000|0`），最后交给 `tools/pkg/packet_create.py` 和 `tools/pkg/bin/` 生成 `.fwpkg`。镜像地址来自分区表（`find_addr_len_by_name`）。
   - 可以照着写一个只含两个镜像的小脚本，不要改 SDK 原文件。先用 `hsflash parse -f` 确认新包的镜像表正确，再上板。
2. 路线 1 走不通时，再研究 `.xml` 分区表的格式，或者直接解析 fwpkg 的头部自己拼（头部里各字段可以拿现有包对照 `parse` 的输出反推）。
3. 不要动 USB DFU 路线（板子不支持）。

### 烧录步骤（串口，官方的断电上电法，今晚成功过三次）

1. 板子**断电**。
2. 电脑这边先启动等待程序：`.fbb/tmp/flash/poweron_write.sh [最长等待秒数]`。脚本顶部的 `PKG=` 写的是要烧的包的路径，**要先改成恢复包的路径**。脚本做的事：盯着串口 `/dev/cu.usbmodem1301`，串口一回来就启动 `hsflash write -c 3322 -p <串口> -b 750000 --switch-after-loader -f <包> --config <cfg>`，握手失败就每分钟重试，成功或“握手之后的失败”就停。
3. 板子上电。引导 ROM 在开机几秒内应答握手（握手是 115200，之后切 750000），然后开始写。
4. 写入期间不能断电。9 MB 的包约 5 分钟，16 MB 估计 8~10 分钟，另加擦除时间。
5. `.fbb/tmp/flash/` 整个目录被 gitignore，理论上可能被清理；如果没了，流程和要点都在 README 的「3322 串口烧录」一节里，重写一个也不难。

### 烧完的验证

- 再读一遍整片（`hsflash read`，同样要走断电上电握手），和备份逐字节比较，sha256 应该完全一致。注意读操作的握手波特率没有专门验证过（当初是碰巧成功的），必要时参照写入的做法加 `--switch-after-loader`。
- 板子能正常开机，串口日志是原来的固件。

### 风险

- 整片写会覆盖 rootkey 区。备份里这段和 SDK 生成的一致，而且普通固件包每次都会写这段，应该没问题，但这是推断。
- 备份里那些“标准包之外的区域能否写入”没有验证过。如果工具对某些地址拒绝写入，把拒绝的地址记下来再想办法。
- 写入中途断电会留下半成品，但引导 ROM 的握手不依赖 flash 内容，重新来一遍即可。
- 恢复之后板子上就是原来的固件，今晚的修正全都不在板上了。用户如果之后还想继续，需要再烧 v6（见第 2 节），或者再恢复一次备份，两个方向都可以反复做。

## 2. 板子和仓库现在的状态

- **板子上跑的是 v5 固件**（feat/encoder 的代码，带第 3 节的全部修正，但没有两条调试 AT 命令）。连上 dongle 能当鼠标用，但有第 3 节说的空闲卡顿。
- 烧录等待程序已经撤掉，没有任何后台任务在跑，夜里不会自动烧任何东西。
- 固件包都在 `.fbb/firmware/`：
  - `diting-community-feat-encoder-serial.fwpkg`：**v6**（sha256 前 16 位 `4032bac87f21e9cd`，9,140,160 字节，7 镜像，可直接串口烧），= v5 + 分步计时 + `AT+AIM` / `AT+AIMDBG` 调试命令。已编译，**还没烧上板**。
  - `diting-community-feat-encoder-serial-v5.fwpkg`：板子上现在这版（`175322f967b45021`）。
  - `...-v4.fwpkg`、`...-v3.fwpkg`、`...-v2.fwpkg`：中间版本，都没在板上。
  - `diting-community-main.fwpkg`：按 main 分支补丁编的，6 镜像，只能走 USB DFU，和板子原来的固件**不是**同一份。
- git：分支 `feat/encoder`，有两个本地提交，**没有推上去**（用户那边没有代理，推送取消）：
  - `7173aa0` 环境：编译烧录环境收进仓库目录（env.sh、.gitignore），修 apply.sh 的两处错
  - `acd9375` feat/encoder：旋转编码器调灵敏度、输入按空脚电平判闭合、运动管线修正
  - `acd9375` 里的补丁对应的就是 v6 的源码。
  - **2026-11-24 review 会话后，工作区不再干净**（都未提交，遵守"不主动提交"）：`patches/hs-fbb/0001-*.patch` 重新导出（IMU 读防御+按键/退避重构）、新增 `patches/hs-fbb/0002-i2c-bounded-wait-timeout.patch`（I2C 超时 100ms）、`apply.sh`（加了 0002）、`REVIEW.md`（完整 review 报告：架构/时序图/嫌疑排序/实验矩阵）、本文件（错误修正）。**补丁 = 最新源码，但还没有对应的固件包（v7 未打）。**
  - SDK 目录 `sdk/hs-fbb` 里两个补丁已应用，方向校验正反都过（0001 覆盖 `samples/native_samples/sle`，0002 覆盖 `diting_community.config`）。

## 3. 今晚的结论

已经修好、并且有 Mac 端仿真验证的（`tools/motion_sim/run.sh` 17 项，`tools/enc_sim/run.sh` 152 项，都通过）：

- **陀螺仪低字节取错位置**：`aim_ekf_step` 里原来是 `raw[7 + i * 2 + 1]`，应为 `raw[6 + i * 2 + 1]`。从仓库第一次提交（279e51e）就在，main 分支和 tag v0.1-feel-good 里也有。影响：每个轴只有高 8 位有效，读数约 2°/s 一档。
- 主循环固定节拍，位移按真实经过的时间积分。
- 每拍位移的零头累积到下一拍；低于静止判据（1°/s）的拍不积分。
- 按键和编码器按开机时量到的空脚电平判闭合（实测 MGPIO12~15 空脚读 0，AGPIO12 读 1，芯片内部上下拉压不过板上的转换器）。
- 编码器改成双边沿 GPIO 中断驱动。
- 默认灵敏度 50 改成 42（旧版实际手感约等于现在真实定义下的 42）。这是我的决定，已告诉用户。

**没解决的（导致用户说“鼠标不平滑、动一下停一下”）**：连上 dongle 且空闲时，主循环每约 25 拍卡一次、每次约 600ms（统计行约 3.3 秒一行，`loop n=100 maxgap≈650ms late=4`）。

已经在上板排除的，不要再重复：

- 编码器的 1kHz 轮询任务。我曾下这个结论并写进过文档，是错的：v5 已经没有周期唤醒，卡顿完全不变。
- 按键引脚的配置和读取（把两个按键脚临时改回复用功能，卡顿不变；读各引脚约 2.8ms）。
- 系统睡眠或冻住：同时从电脑每 20ms 发一次 AT 探测，8 秒 390 次全部 1.4ms 响应，只有主循环被挡。
- `AT+PM=0`（无效）。

主循环里会等外部的只有：`osal_msleep`、`bt_sle_is_connected()`、`uapi_i2c_master_writeread`（读 IMU）、`bs_sle_ssapc_write`。600ms 花在哪一步还不知道。”发包时循环就快”这个说法来自更早的观察，现在无法复核，未证实。

**2026-11-24 review 会话的修正与补充（详见仓库 REVIEW.md，此处只列改变判断的）**：

- **改判**：`bt_sle_is_connected()` 不是闭源库——它就是 `sle_com.c` 里的本地函数（mutex 保护读一个回调置的标志，微秒级）。上一行把它当”闭源库”是错的。
- **首要嫌疑（静态实锤）**：3322 目标配置 `diting_community.config` 的 `CONFIG_I2C_WAIT_CONDITION_TIMEOUT=0xFFFFFFFF`（无限等）；SDK 里**其他所有** 3322 目标都是 0x64（100ms）。IMU 读（INT 模式，`i2c.c` 的 `osal_sem_down_timeout(sem, 超时)`）完成中断若被拖延，aim_demo 就停在驱动里多久。与”只有主循环被挡、AT 1.4ms 正常、600ms 自愈”全部观测吻合。
- I2C 驱动的 `i2c_port_clock_dynamic_enable` LPM 钩子在 3322 上是 weak 空实现（porting 未覆写），驱动层自身不做动态时钟开关；时钟门控若有，在 PM 子系统层面（API：`uapi_clocks_config_mode` / `uapi_clocks_switch`）。
- **修复已落地源码级（未打包未烧录）**：0002 补丁把该超时改回 0x64；0001 补丁重新导出，含主循环 IMU 读防御（失败静默一拍 + “过快成功”判废）和按键/退避重构（按键沿不等退避、发出成功才记按键状态）。两仿真全过（17/152 项）+ 语法编译过 + 补丁双向校验过。
- **注意：`.fbb/firmware/` 里的 v6 包不含这些新改动。**要上板验证需先打 v7 包。

定位手段（需要把 v6 烧上板）：

- `AT+AIM`：打印主循环每一步（fsm/imu/conn/btn/write/stat）的最大、平均耗时，超过 20ms 的次数，相邻两拍唤醒间隔的分布，`osal_msleep` 多睡的时间。读完清零。
- `AT+AIMDBG=<mask>`：运行时关掉某一步。0x1 不读按键，0x2 查连接改用回调置的标志，0x4 不读 IMU，0x8 不跑连接状态机，0x10 空闲也强制发包，写 0 恢复。
- 批量实验脚本：`.fbb/tmp/flash/aim_probe.py <串口> [场景...]`，场景名 base / conn / imu / fsm / btn / tx / all。
- 有了这两条命令，之后的实验不需要再烧录。

## 4. 环境速查

```bash
cd /Users/lijialun/work/airmouse-3322 && source ./env.sh
# 编译 3322 固件（约 6~10 分钟，慢在生成反汇编清单）
cd "$HS_FBB/src" && python3 build.py diting-community -release
# 只编译样例查语法（几秒；不会重新链接，也不产生新固件包）
python3 build.py diting-community -component=sle_sample
# 两份仿真（Mac 上跑，测的是从固件源码里抽出来的真实代码）
./tools/motion_sim/run.sh && ./tools/enc_sim/run.sh
```

- 串口：3322 的调试器是 `/dev/cu.usbmodem1301`，应用日志和 AT 命令都是 750000 波特。板子断电时串口会消失，上电后重新枚举。
- `AT+REBOOT` 可以软复位抓开机日志；`APP|run:<ms>` 是运行时长，用它判断板子是否真的重启过。
- 改了 `sdk/hs-fbb/samples/native_samples/sle/` 下的源码之后要重新导出补丁：
  `cd sdk/hs-fbb && git diff -- samples/native_samples/sle > ../../patches/hs-fbb/0001-sle-sample-airmouse.patch`，
  然后做反向检查（`git apply --check -R`）和正向检查（临时 index 里 `git apply --cached --check`）。
- `.fbb/tmp/flash/` 下今晚用过的脚本：`poweron_write.sh`（烧录等待）、`verify_v4.py`（测统计行间隔并软复位抓开机日志）、`pm_test.py`、`aim_probe.py`。都在被忽略的目录里。

## 5. 用户的偏好和约束

- 下载、安装的东西都放在仓库目录里，并加进 `.gitignore`。
- 不主动提交、不主动推送。现在有两个本地提交没推，用户没有代理，推送的事先不提。
- 接线说明写板上丝印（`CS`、`RX4`、`DO`、`C0_C`、`H_15` 这种），不写裸脚号。完整对照表在 README 的「排针丝印对照表」。
- 用户要的是“真正的鼠标”的平滑度，不平滑是首要缺陷。
- 用户今晚对进度不满：反复让他断电上电、我凭直觉下结论又推翻。以后先量再下结论，能不烧录就不烧录，一次上电尽量多验证几件事。
- 解释时用具体原因（“数据取错了”“计时不准”），不泛泛说“调度问题”。

## 6. 恢复备份之后可以做的事（看用户意愿）

1. 先打 **v7** 包（当前源码 = v6 + review 会话的防御修复，v6 包不含），烧上去后用 `AT+AIM` / `AT+AIMDBG` 定位空闲卡顿在哪一步（实验矩阵在 REVIEW.md §6），修掉，再请用户实测手感。如果 0002 的超时修复起效，卡顿应从 ~600ms 缩到 ≤100ms 或消失。
2. 陀螺仪字节下标的修正要不要移到 main 分支，由用户定。
3. 真编码器接上板实测（接法见 README 编码器一节），调方向和每格步数。
4. 两个本地提交等有代理时再推。
