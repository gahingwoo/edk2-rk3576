# ArmSoM CM5-IO

[English](cm5io.md) | 简体中文

ArmSoM CM5 核心板（RK3576）配 CM5-IO 底板。从 NVMe 启动 Fedora，从 eMMC 启动
Windows 11 23H2。下面的结果都来自同一块 4 GB 的 CM5。核心板和底板由 ArmSoM 赞助。

## 能用什么

| | 固件里 | Windows 11 23H2 | Linux（Fedora） |
|---|---|---|---|
| HDMI | 可用，2560x1440，取自显示器 EDID | 可用（固件的帧缓冲） | 可用 |
| eMMC | 可用，设置能保存 | 可用，作为系统盘 | 可用 |
| SD 卡 | 可用；固件可以放在卡上 | 可用 | 可用 |
| NVMe | 可用 | 可用 | 可用，作为系统盘 |
| USB-A（经过 Hub） | 可用 | 可用 | 可用 |
| USB-C | USB 2.0 | USB 2.0 | 可用 |
| 以太网 | 可用 | 可用 | 可用 |
| GPU | 为系统上电 | wddm-mali-g52 的早期里程碑在这块板上跑过（2026-10-01 到 2026-10-03）；桌面驱动只在泰山派 3M 上跑过 | 可用（Panfrost） |
| HDMI 音频 | 已在 ACPI 里声明 | 未试过 | 无记录 |
| WiFi、蓝牙 | 不用 | 不能用 | 无记录 |

Windows 一栏大部分是 2026-10-01 用固件 0.2.0 和 woa-rk3576 0.1.0 看到的结果。这块板子用
0.4.x 固件启动过操作系统；0.4.0 的发布说明记录了它通过 HDMI 显示中英文菜单。

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：按住 MaskROM 键再上电。MaskROM 口就是 USB-C 口，先拔掉上面接的其他东西。
- BootROM 先试 eMMC 再试 SD 卡，所以只要 eMMC 上有引导程序，SD 卡就会被忽略。

## 安装固件

放在 eMMC 上（装 Windows 需要这样）：

1. 把 `CM5IO-emmc` 映像作为完整映像写入（[刷写固件](../flash.zh-CN.md#写入完整映像)）。这会替换 eMMC 上的全部内容。
2. 以后更新时选"保留分区"（[只更新固件](../flash.zh-CN.md#只更新固件保留其他内容)）。

或者放在 SD 卡上，前提是 eMMC 上没有引导程序：[刷写固件](../flash.zh-CN.md#armsom-cm5-io)。

接下来：

- Windows：看 [woa-rk3576 的安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)。概览见 [Windows](../windows.zh-CN.md)。
- Linux：见 [Linux](../linux.zh-CN.md)。

## 与泰山派 3M 对齐（分支 `cm5io-parity`）

泰山派 3M 固件里的每一项功能，都对照了这块板的硬件（主线设备树、ArmSoM 的厂商设备树、CM5 和
CM5-IO V1.1 原理图）。都已编译通过，本节的内容都还没有在板子上运行过。

| 功能 | 这块板 | 说明 |
|---|---|---|
| 调试报告（Ctrl+Alt+F11）、截图、屏幕键盘、网络对时、把系统变量存到 eMMC | 早已具备 | 公共代码，所有板子都有 |
| 温度区、115 C `_CRT`、120 C 硬件过温关机 | 早已具备 | `PcdTsadcEnable` |
| 视频解码、CPU 空闲状态设置项 | 早已具备 | 公共代码 |
| HDMI 音频（SAI6） | 早已具备 | `PcdHdmiAudioPreset` |
| 关机时通过 RK806 断电 | **新增** | 电源芯片接法和泰山派 3M 相同：RK806 在 I2C1 的 0x23，GPIO0_A3 = PMIC_PWR_CTRL1 |
| NPU 设置项 | **新增** | 固件能访问 RK806 后出现（vdd_npu_s0 = BUCK2） |
| CPU Maximum Frequency | **新增**，默认 1608 MHz | BUCK1/BUCK3 与泰山派 3M 相同；仍会检查 OTP 里的 SoC 型号 |
| GPU 电压上报（`GpuRail` 变量） | **新增**，只读 | 不升到 775 mV：这块板的 GPU 保持 594 MHz |
| 模拟音频（ES8388） | **新增** | I2C0 0x10，SAI1 M0（SDO0），输出到 3 针座子 CN3 和 40 针排针；没有耳机插入检测（CN3 没有检测脚），没有功放 |
| "Armbian (U-Boot)" 单次启动 | **新增** | SPL 改为用主线 U-Boot v2026.10 加 Armbian 的 CM5-IO 板级文件和单次启动补丁编译；现在先试 SD 卡，和泰山派 3M 一样 |
| 风扇 | 早已具备（"Fan Control by ACPI Thermal Zone"，默认关） | 极性仍未确定，见本板 RockchipPlatformLib.c 里的 `PwmFanSetSpeed`；开关（分支 `fan-onoff`）合并后也覆盖这块板 |
| 首次启动询问一次 CPU 频率 | 随 CPU Maximum Frequency 生效 | 分支 `cpu-ask-once`，尚未合并 |
| Wi-Fi、蓝牙（SYN43752） | 没有移植 | 这颗芯片没有 Windows 驱动；泰山派 3M 的驱动是给 BCM43456/BCM4345C5 的 |
| DSI 屏、触摸、触摸按键 | 不适用 | 底板有 DSI 接口，但没有测过屏；泰山派 3M 的代码针对它自己的屏 |
| USB-C 设备等待 / USB-A VBUS 设备树修正 | 不需要 | CM5-IO 的 USB-C 口有供电；USB-A 的稳压器在主线设备树里是 PHY 的电源 |

上板要检查的内容，先看固件：

- 串口日志：`Rk806I2cPowerOff` 找到电源芯片（`CHIP_NAME 0x80`），`GpuRail` 打印 vdd_gpu_s0，
  `Cm5ioAudio: mclk_sai1 = ...` 和 `SAI1 and I2C0 pins set`。
- 首页的 "Shut Down" 和 Shell 里的 `reset -s` 能让板子断电；重启仍然是重启。
- CPU 页面出现 "CPU Maximum Frequency"；选 Max 能启动，日志显示电压已升高。ACPI 页面出现 "NPU"。
- 插着 Armbian SD 卡、固件在 eMMC 上：正常启动进 UEFI；"Armbian (U-Boot)" 启动一次 Armbian；
  下次重启又回到 UEFI。还要确认换了 SPL 以后画面不变；如果变了，把 `boards/cm5io.conf` 里的
  `SPL_DEFCONFIG` 删掉，回到 `binaries/idblock_cm5io.bin`。
- Windows：关机能断电；`\_SB.SAI1` 和 `\_SB.I2C0.ES83` 绑定 csaudiork3x/rk3xi2sbus 和 es8323，
  没有黄色感叹号；CN3 和 40 针的线路输出有声音；打开 NPU 选项后 woa-rk3576-npu 能绑定；maliwddm
  读到 GpuRail 变量并运行在 594 MHz。
- Linux：和以前一样启动（音频、NPU 和电源芯片在 Linux 下归设备树管）；`poweroff` 仍能断电。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#armsom-cm5-io)。
