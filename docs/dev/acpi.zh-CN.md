# ACPI

[English](acpi.md) | 简体中文

固件在每块板子上告诉 ACPI 操作系统（Windows）哪些内容，以及由哪个构建设置或设置选项决定。以 2026-11-24 的 `main` 为准。

## 每块板子的配置表模式

| 板子 | 默认值（`PcdConfigTableModeDefault`） | 是否编译 ACPI 表 |
|---|---|---|
| ROCK 4D | 仅设备树 | 否；这块板子只编译 FDT，所以 Windows 无法在上面启动 |
| CM5-IO | ACPI + 设备树 | 是，`Platform/ArmSoM/CM5IO/AcpiTables/` |
| 泰山派 3M | ACPI + 设备树 | 是，`Platform/LCKFB/TaishanPi3M/AcpiTables/` |

在编译了 ACPI 的板子上，ACPI / Device Tree 设置页里的 "Config Table Mode"（配置表模式）可以在 ACPI + 设备树、仅 ACPI 和仅设备树之间切换。

SoC 级的表（MADT、GTDT、PPTT、MCFG、SPCR、DBG2、FADT）是共用的，在两块 ACPI 板子上编译出的字节完全相同。每块板子有自己的 DSDT，它包含 `Silicon/Rockchip/RK3576/AcpiTables/` 里的 SoC 设备文件，并把板级取值写在它们所对应的设备树那一行旁边。

## 设备

| 设备 | `_HID` | CM5-IO | 泰山派 3M | 由什么决定 |
|---|---|---|---|---|
| eMMC | `RKCP0D40` | 有 | 有 | 始终 |
| SD 卡 | `RKCPFE2C` | 有 | 有 | 始终 |
| GPIO，5 组 | `RKCP3002` | 有 | 有 | 始终 |
| I2C | `RKCP3001` | 有 | 有 | 始终 |
| SPI | `RKCP3003` | 有 | 有 | 始终 |
| DMA（PL330），3 个 | `ARMH0330` | 有 | 有 | 始终 |
| 以太网（GMAC0） | `RKCP6543` | 有 | 有 | 始终 |
| USB-C（XHC0，DRD0） | `PNP0D10` | 有 | 有 | 始终；在泰山派 3M 上，只有开机时已接上的受电设备才有 VBUS（[硬件](hardware.zh-CN.md)） |
| USB-A（XHC1，DRD1） | `PNP0D10` | 有 | 有 | 始终 |
| XHC2、SATA | | 有 | 无 | 仅 CM5-IO |
| PCIe | | 有 | 有 | 泰山派 3M 的 mini-PCIe 根复合体在 UEFI 里链路没有训练成功时会被隐藏（插槽里插着 USB 模块时通常就是这样） |
| GPU（Mali-G52） | `RKCP7402` | 有 | 有 | 始终；由固件给 `PD_GPU` 上电 |
| 显示（VOP2） | `RKCP7403` | 有 | 有 | 始终 |
| HDMI 音频（SAI6） | `RKCP3004` | 有 | 有 | `PcdHdmiAudioPreset`，两块板子上都是 TRUE |
| 耳机编解码器 ES8388 | `RKCP8388` | 无 | 有 | `PcdAnalogAudioPreset`，泰山派 3M 上为 TRUE |
| SAI1（模拟音频） | `RKCP3004` | 无 | 有 | 同上 |
| 触摸 GT9271（`\_SB.I2C0.TCH0`） | `RKCP9271` | 无 | 开机时屏幕有应答才有 | 屏幕检测 |
| 蓝牙 BCM4345C5（`\_SB.BTH0`） | `RKCP4345` | 无 | 有 | 泰山派 3M 的 DSDT |
| 蓝牙 BCM43752（`\_SB.BTH0`，BCM4362A2 内核，UART4） | `RKCP4362` | 有 | 无 | CM5-IO 的 DSDT |
| SDIO Wi-Fi 控制器（`\_SB.SDIO`，SSDT `RKWIFI`） | `RKCPFE2D` | 有 | 有 | 设置项“SDIO Wi-Fi”，默认关 |
| RTC HYM8563 | `HYMB0001` | 有 | 有 | 板级 DSDT |
| PMIC RK806（`\_SB.I2C1.PMC0`） | `RKCP0806`，`_CID` `PRP0001` | 有 | 有 | 板级 DSDT；没有 Windows 驱动，由固件管理（woa-rk3576 的 `drivers/null` 给它起名） |
| 引脚控制器（`\_SB.PINC`） | `RKCP3005`，`_CID` `PRP0001` | 有 | 有 | 始终；没有 Windows 驱动，同上 |
| UART2（`\_SB.UAR2`） | `HISI0031` | 有 | 无 | CM5-IO 的 DSDT；泰山派 3M 上不声明，它的任何引脚选项都没有引到接口上 |
| 温度区（TSADC 读数） | | 有 | 有 | 始终；不可信的读数显示为 100.0 C |
| 风扇（`PNP0C0B`，`Fan.asl`） | | 有 | 有 | 设置项 "Fan Control by ACPI Thermal Zone"（由 ACPI 温度区控制风扇），默认关闭 |
| CPU 空闲状态（SSDT `RKLPI`） | | 有 | 有 | 设置项 "CPU Idle States"，默认关闭 |
| NPU（`Npu.asl`） | `RKCP3576` | 无 | 有 | 设置项 "NPU (experimental)"（实验性），默认关闭；只在指明了 RK806 所在 I2C 总线（`PcdRk806I2cBase`）的板子上提供，截至 2026-11-24 只有泰山派 3M |
| 40 针 UART（SSDT `RKUART40`，`Uart40.asl`） | `RKCP3006`，`_CID` `PRP0001` | 无 | 有 | 设置项 "40-pin UART (experimental)"，默认关闭；只在设置了 `PcdPin40Supported` 的板子上提供（`main` 上是泰山派 3M）。`SerCx-FriendlyName` 让它们成为 COM 口接口，供 woa-rk3576 的 rk3xuart 使用 |
| RhProxy（SSDT `RKRHPX`，`\_SB.RHPX`，`RhProxy.asl`） | `MSFT8000` | 无 | 有 | 设置项 "40-pin GPIO/I2C/SPI for apps (experimental)"，默认关闭；板子同上。把排针上的 GPIO、I2C 和 SPI 交给 Windows.Devices.Gpio、.I2c、.Spi |

显示器的 EDID 以 `\_SB.GPU0.EDID` 的形式提供给 Windows 显示驱动，同时也挂在 GOP 句柄上。

## 给操作系统驱动用的变量

| 变量 | 由谁设置 | 由谁读取 |
|---|---|---|
| `GpuRail`（易失） | `GpuRail.c`：BUCK5 电压、GPU OTP 字节 | wddm-mali-g52，用来判断是否允许 702 MHz |
| `AcpiNpu` | 设置选项；也可以在 UEFI Shell 里设置：`setvar AcpiNpu -guid 8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c -bs -rt -nv =01` | 下次启动时的固件 |

## 去哪里看

- DSDT：`Platform/<vendor>/<board>/AcpiTables/Dsdt.asl`。
- SoC 设备：`Silicon/Rockchip/RK3576/AcpiTables/*.asl`。
- 设置选项及其默认值：`Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/`（`RK3576DxeHii.vfr`、`ConfigTable.c`）和 `Silicon/Rockchip/RK3576/RK3576.dec`。
- UEFI Shell 里的 `acpiview` 可以导出并检查已安装的表。
