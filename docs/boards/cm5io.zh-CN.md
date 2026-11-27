# ArmSoM CM5-IO

[English](cm5io.md) | 简体中文

ArmSoM CM5 核心板（RK3576）配 CM5-IO 底板。从 NVMe 启动 Fedora，从 eMMC 启动 Windows 11 23H2。
结果都来自同一块板子（4 GB CM5），由 ArmSoM 赞助。

## 能用什么

上板情况：用 0.4.x 固件启动过操作系统；Windows 一栏大部分是用固件 0.2.0 和 woa-rk3576 0.1.0 看到的。

| | 固件里 | Windows 11 23H2 | Linux（Fedora） |
|---|---|---|---|
| HDMI | 可用，2560x1440，取自 EDID | 可用（固件的帧缓冲） | 可用 |
| eMMC | 可用，设置能保存 | 可用，作为系统盘 | 可用 |
| SD 卡 | 可用；固件可以放在卡上 | 可用 | 可用 |
| NVMe | 可用 | 可用 | 可用，作为系统盘 |
| USB-A（经过 Hub） | 可用 | 可用 | 可用 |
| USB-C | USB 2.0 | USB 2.0 | 可用 |
| 以太网 | 可用 | 可用 | 可用 |
| GPU | 为系统上电 | wddm-mali-g52 桌面驱动只在泰山派 3M 上跑过 | 可用（Panfrost） |
| HDMI 音频 | 已在 ACPI 里声明 | 未试过 | 无记录 |
| WiFi、蓝牙 | 不用 | 不能用 | 无记录 |

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：按住 MaskROM 键再上电。MaskROM 口就是 USB-C 口，先拔掉上面接的其他东西。
- BootROM 先试 eMMC 再试 SD 卡，所以只要 eMMC 上有引导程序，SD 卡就会被忽略。

## 安装固件

- eMMC（装 Windows 需要）：把 `CM5IO-emmc` 映像作为[完整映像](../flash.zh-CN.md#写入完整映像)写入；
  以后用["保留分区"](../flash.zh-CN.md#只更新固件保留其他内容)更新。
- SD 卡（eMMC 上没有引导程序时）：见[刷写固件](../flash.zh-CN.md#armsom-cm5-io)。

然后装 [Windows](../windows.zh-CN.md) 或 [Linux](../linux.zh-CN.md)。

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
| GPU 电压上报（`GpuRail` 变量） | **新增** | 每次启动读取并交给 Windows GPU 驱动 |
| GPU voltage for 702 MHz (775 mV) | **新增**，默认关 | vdd_gpu_s0 是 I2C1 0x23 上 RK806S-5 的 BUCK5（CM5 原理图第 2、13 页；`rk3576-armsom-cm5.dtsi` dcdc-reg5，550000 到 900000 uV），和泰山派 3M 相同。开：在过热关机已启用时从 750 mV 以 6.25 mV 一步升到 775 mV，每步读回，maliwddm 运行 702 MHz；关：750 mV、594 MHz |
| 模拟音频（ES8388） | **新增** | I2C0 0x10，SAI1 M0（SDO0），输出到 3 针座子 CN3（OUT1；OUT2 只到测试点 T36/T37），麦克风焊盘 T34/T35 接 LIN2/RIN2 差分；`_DSD` `rockchip,output-route` 3、`rockchip,input-select` 4；没有耳机插入检测（CN3 没有检测脚），没有功放 |
| "Armbian (U-Boot)" 单次启动 | **新增** | SPL 改为用主线 U-Boot v2026.10 加 Armbian 的 CM5-IO 板级文件和单次启动补丁编译；现在先试 SD 卡，和泰山派 3M 一样 |
| 风扇开关（"Fan"，默认开） | 早已具备（main，`fan-onoff`） | 极性仍未确定，见本板 RockchipPlatformLib.c 里的 `PwmFanSetSpeed`："开"会把引脚拉低，只有反相的解读正确时才是全速 |
| 首次启动询问一次 CPU 频率 | 随 CPU Maximum Frequency **新增** | `cpu-ask-once`（已在 main）会在所有提供 Max 的板子上询问 |
| Wi-Fi、蓝牙（BW3752-50B1 模组上的 BCM43752） | **已加** | 蓝牙：`\_SB.BTH0`（`RKCP4362`），UART4 M1，BT_REG_ON 为 GPIO1_C7，BT_WAKE 为 GPIO1_D4，配 woa-rk3576 的 bcm4345bt 和 BCM4362A2.hcd。Wi-Fi：设置项“SDIO Wi-Fi”（默认关）打开后固件配置 SDIO 并探测一次网卡，再装 `RKCPFE2D` 的 SSDT，给 bcmwlsdio 用（woa-rk3576 的 `cm5io-wifi-bt` 分支，没跑过）。引脚取自 ArmSoM-CM5-1V1 原理图第 11、17 页，和泰山派 3M 相同，只有串口不同 |
| DSI 屏、触摸、触摸按键 | 不适用 | 底板有 DSI 接口，但没有测过屏；泰山派 3M 的代码针对它自己的屏 |
| USB-C 设备等待 / USB-A VBUS 设备树修正 | 不需要 | CM5-IO 的 USB-C 口有供电；USB-A 的稳压器在主线设备树里是 PHY 的电源 |

上板要检查的内容，先看固件：

- 串口日志：`Rk806I2cPowerOff` 找到电源芯片（`CHIP_NAME 0x80`），`GpuRail` 打印 vdd_gpu_s0，
  `Cm5ioAudio: mclk_sai1 = ...` 和 `SAI1 and I2C0 pins set`。
- "Fan" 分别设为开和关：记下哪一个真正让 CN32 上的风扇转起来（这能确定极性）。
- 首页的 "Shut Down" 和 Shell 里的 `reset -s` 能让板子断电；重启仍然是重启。
- 刷写后第一次启动：HDMI 上出现一次性的 CPU 频率询问。CPU 页面出现 "CPU Maximum Frequency"；选 Max 能启动，日志显示电压已升高。ACPI 页面出现 "NPU"。
- 插着 Armbian SD 卡、固件在 eMMC 上：正常启动进 UEFI；"Armbian (U-Boot)" 启动一次 Armbian；
  下次重启又回到 UEFI。还要确认换了 SPL 以后画面不变；如果变了，把 `boards/cm5io.conf` 里的
  `SPL_DEFCONFIG` 删掉，回到 `binaries/idblock_cm5io.bin`。
- Windows：关机能断电；`\_SB.SAI1` 和 `\_SB.I2C0.ES83` 绑定 csaudiork3x/rk3xi2sbus 和 es8323，
  没有黄色感叹号（es8323 需要 woa-rk3576 `es83-optional-irq` 或更新：旧版会创建一个这块板 `_CRS` 里没有的中断）；CN3 和 T36/T37 测试点有声音；T34/T35 上接的麦克风能录音（LIN2/RIN2 差分）；codec 的 `Device Parameters` 里 `ActiveOutputRoute` 为 3、`ActiveInputSelect` 为 4、`JackInterrupt` 为 0；打开 NPU 选项后 woa-rk3576-npu 能绑定；maliwddm
  读到 GpuRail 变量并运行在 594 MHz。
- GPU 电压（风扇开着）：把 "GPU voltage for 702 MHz (775 mV)" 设为开并重启。串口日志：
  `GpuRail: raised BUCK5 0x28 -> 0x2C, read back 775000 uV`（如果出现的是 `not raising BUCK5: ...`，
  记下原因：这块板的 RK806 还没读过）。Windows：没有存 `GpuClk`、没设 `GpuClkHigh` 时，
  `m5test clk`（或 GPU 时钟性能计数器）显示 702 MHz；FurMark 跑 60 秒没有 TDR；记下分数和 GPU、CPU
  最高温度。然后把选项设为关并重启：日志显示 `lowered BUCK5 0x2C -> 0x28, read back 750000 uV`，
  Windows 运行 594 MHz。
- 蓝牙（Windows）：`ACPI\RKCP4362` 装上 bcm4345bt；它的 Device Parameters 里 `ResetStatus` 为 0，
  有 `RomVersion`，用 BCM4362A2.hcd 时 `FirmwareLoaded` 为 1，`Baud` 为 1500000。`ResetStatus` 超时说明
  模组没上电、没有 32 kHz 时钟，或串口引脚不对。
- 打开 "SDIO Wi-Fi"：串口日志里有 `CM5-IO Wi-Fi: CIS vendor 0x02d0 device 0xaae8`、
  `chip ID ... chip 0xaae8 (43752) rev N` 和 `probe PASS`；记下版本号（Armbian 的固件是给 rev 2 的）。
  Windows 里 `\_SB.SDIO` 绑定 dwcmshc，并出现 `SD\VID_02D0&PID_AAE8` 的卡。
- Linux：和以前一样启动（音频、NPU 和电源芯片在 Linux 下归设备树管）；`poweroff` 仍能断电。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#armsom-cm5-io)。
