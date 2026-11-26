# ACPI

[English](acpi.md) | 简体中文

固件在每块板子上向 ACPI 操作系统（Windows）描述了什么，以及由哪个编译设置或设置项决定。

## 每块板子的配置表模式

| 板子 | 默认值（`PcdConfigTableModeDefault`） | ACPI 表 |
|---|---|---|
| ROCK 4D | ACPI + 设备树 | `Platform/Radxa/ROCK4D/AcpiTables/` |
| CM5-IO | ACPI + 设备树 | `Platform/ArmSoM/CM5IO/AcpiTables/` |
| 泰山派 3M | ACPI + 设备树 | `Platform/LCKFB/TaishanPi3M/AcpiTables/` |

在编译了 ACPI 的板子上，设置里的 "Config Table Mode" 可以在 ACPI + 设备树、只用 ACPI、只用设备树之间切换。
SoC 级的表（MADT、GTDT、PPTT、MCFG、SPCR、DBG2、FADT）是共用的，在三块板子上完全相同。
每块板子的 DSDT 包含 `Silicon/Rockchip/RK3576/AcpiTables/` 里的 SoC 设备文件，并注明每个板级取值来自设备树的哪一行。

## 设备

C = CM5-IO，R = ROCK 4D，T = 泰山派 3M。"设置项"指固件设置里的选项，没有注明时默认关闭。

| 设备 | `_HID` | 板子 | 由什么决定 |
|---|---|---|---|
| eMMC | `RKCP0D40` | C T | 总是 |
| SD 卡 | `RKCPFE2C` | C R T | 总是 |
| GPIO，5 组 | `RKCP3002` | C R T | 总是 |
| I2C | `RKCP3001` | C R T | 总是 |
| SPI | `RKCP3003` | C R T | 总是 |
| DMA（PL330），3 个 | `ARMH0330` | C R T | 总是 |
| 以太网（GMAC0） | `RKCP6543` | C R T | 总是 |
| USB-C（XHC0，DRD0） | `PNP0D10` | C R T | 总是；T 上只给启动时接着的 sink 供 VBUS |
| USB-A（XHC1，DRD1） | `PNP0D10` | C R T | 总是 |
| XHC2、SATA | | C | 仅 CM5-IO |
| EEPROM BL24C16F（`\_SB.I2C6.EEP0`） | `RKCP2416`，`_CID` `PRP0001` | R | 板级 DSDT；只读，供 rkeeprom 使用 |
| PCIe | | C R T | T 和 R 上链路在 UEFI 里没建立时隐藏（T 上插 USB 模块时通常如此） |
| GPU（Mali-G52） | `RKCP7402` | C R T | 总是；固件给 `PD_GPU` 上电 |
| 显示（VOP2） | `RKCP7403` | C R T | 总是；显示器 EDID 放在 `\_SB.GPU0.EDID` 和 GOP 句柄上 |
| HDMI 音频（SAI6） | `RKCP3004` | C R T | `PcdHdmiAudioPreset`（三块板都是 TRUE） |
| ES8388 编解码器、SAI1 | `RKCP8388`、`RKCP3004` | R T | `PcdAnalogAudioPreset` |
| 触摸 GT9271（`\_SB.I2C0.TCH0`） | `RKCP9271` | T | 启动时屏幕有应答 |
| 蓝牙 BCM4345C5（`\_SB.BTH0`） | `RKCP4345` | T | 板级 DSDT |
| RTC HYM8563 | `HYMB0001` | C R T | 板级 DSDT |
| PMIC RK806（`\_SB.I2C1.PMC0`） | `RKCP0806`，`_CID` `PRP0001` | C R T | 板级 DSDT；没有 Windows 驱动（woa-rk3576 的 `drivers/null` 认领它） |
| 引脚控制器（`\_SB.PINC`） | `RKCP3005`，`_CID` `PRP0001` | C R T | 总是；没有 Windows 驱动 |
| UART2（`\_SB.UAR2`） | `HISI0031` | C R | 板级 DSDT（T 上没有引脚方案能引到接口） |
| 温度区（TSADC） | | C R T | 总是；不可信的读数显示为 100.0 C |
| 风扇（`PNP0C0B`，`Fan.asl`） | | C R T | 设置项 "Fan Mode"（非 "Always on"） |
| CPU 空闲状态（SSDT `RKLPI`） | | C R T | 设置项 "CPU Idle States" |
| NPU（`Npu.asl`） | `RKCP3576` | R T | 设置项 "NPU (experimental)"；需要 `PcdRk806I2cBase` |
| 40-pin UART（SSDT `RKUART40`） | `RKCP3006`，`_CID` `PRP0001` | R T | 设置项 "40-pin UART (experimental)"，需要 `PcdPin40Supported`；`SerCx-FriendlyName` 让 rk3xuart 生成 COM 口 |
| RhProxy（SSDT `RKRHPX`，`\_SB.RHPX`） | `MSFT8000` | R T | 设置项 "40-pin GPIO/I2C/SPI for apps"，板子同上 |
| 硬件看门狗（WDAT） | | C R T | 设置项 "Hardware watchdog (experimental)"，见下文 |
| 电源键（SSDT `RKPWRKEY`，`\_SB.I2C1.PWRK`） | `RKCP806B` | R | 设置项 "Power button (experimental)"；需要 `PcdAcpiPwrKeySupported`（ROCK 4D SW1、CM5-IO SW4）和 `PcdRk806I2cBase`；`main` 上只有 ROCK 4D 两者都设。供 rk806pwrkey 使用 |
| LED（SSDT `RKLED`，`\_SB.LEDS`） | `RKCP1ED0` | R T | 设置项 "Board LEDs (experimental)"，需要 `PcdAcpiLedsSupported`；供 rkled 使用 |
| SARADC 和按键（SSDT `RKSARADC`，`\_SB.ADC0`） | `RKCP0ADC` | R T | 设置项 "ADC keys (experimental)"，需要 `PcdAcpiSaradcSupported`（T 上默认打开）；R 的 DSDT 里总是有。关闭时连 SARADC 时钟也不设置。T 的 Recovery 键上报 HID System Power Down（`0x00010081`），即 Windows 的电源按钮。供 rk3xsaradc 使用 |

可选 SSDT 的规则：只能用 `External` 引用 DSDT 里的对象，不能引用另一个可选 SSDT 里的设备。SSDT 按 FV 顺序安装，
Windows 加载这张表时那个对象可能还不存在（这样写的 RhProxy SSDT 曾让 Windows 启动不了）。
如果一张表需要知道另一张是否安装，由 RK3576AcpiPlatformDxe 在安装时修改它里面的一个 `Name`，比如 `\_SB.RHPX.UAON`。

## 硬件看门狗

WDAT 描述的是 WDT_NS（`0x2ACE0000`），即 TF-A 留给非安全世界的 DesignWare 看门狗。Windows 要求 5 分钟，
超过了这个模块能数的范围，所以表里声明 1 秒一计、最多 600，而固件总是设成最长周期：24 MHz 的 2^31 个周期，
两次（先中断，再复位），约 179 秒。寄存器细节见 `Wdat.aslc` 和 `RK3576AcpiPlatformDxe/Watchdog.c`。
CRU 保留了看门狗复位记录时，串口日志显示 "last reset WAS the watchdog"，否则显示 "last reset cause unknown"：PMIC 断电重上电会清掉这个记录，而泰山派 3M 上的看门狗复位就是这样（2026-11-26）。

板上测试：

1. 打开设置项，Windows 空闲再加负载，共一小时：不复位。
2. 用 woa-rk3576 的 `tools/wdthang` 让 Windows 卡死：约 3 分钟内复位，之前日志里没有 ResetSystem。下次启动日志显示 "last reset WAS the watchdog"，或在 PMIC 给芯片断电重上电的板子（泰山派 3M）上显示 "last reset cause unknown"。
3. 关闭设置项，同样卡死：板子一直卡住。

## 给操作系统驱动用的变量

| 变量 | 由谁设置 | 由谁读取 |
|---|---|---|
| `GpuRail`（易失） | `GpuRail.c`：BUCK5 电压、GPU OTP 字节 | wddm-mali-g52，决定能否跑 702 MHz |
| `AcpiNpu` | 设置项，或在 UEFI Shell 里 `setvar AcpiNpu -guid 8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c -bs -rt -nv =01` | 下次启动时的固件 |

## 去哪里看

- DSDT：`Platform/<厂商>/<板子>/AcpiTables/Dsdt.asl`。
- SoC 设备：`Silicon/Rockchip/RK3576/AcpiTables/*.asl`。
- 设置项和默认值：`Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/`（`RK3576DxeHii.vfr`、`ConfigTable.c`）和
  `Silicon/Rockchip/RK3576/RK3576.dec`。
- UEFI Shell 里的 `acpiview` 可以导出并检查已安装的表。
