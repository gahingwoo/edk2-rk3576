# 硬件

[English](hardware.md) | 简体中文

固件所依赖的板级事实，以 2026-11-24 的 `main` 为准。每块板子上哪些功能可用，见各板子页面：[ROCK 4D](../boards/rock4d.zh-CN.md)、[CM5-IO](../boards/cm5io.zh-CN.md)、[泰山派 3M](../boards/tspi3m.zh-CN.md)。固件打开了哪些 ACPI 设备和设置，见 [ACPI](acpi.zh-CN.md)。

以前放在这一页的移植笔记已移到[历史记录](../history/2026-11-22-hardware-before-rewrite.md)。

## 所有板子共有

| | |
|---|---|
| SoC | Rockchip RK3576：4 个 Cortex-A72 + 4 个 Cortex-A53，ARMv8.0 |
| 中断控制器 | GIC-400（GICv2）。没有 redistributor，也没有 ITS。 |
| DRAM 起始地址 | `0x40000000` |
| 启动链 | BootROM、U-Boot SPL、TF-A BL31 v2.14.0、作为 BL33 的 EDK2 |
| 串口控制台 | UART0，地址 `0x2AD40000`，1500000 8N1，无流控 |
| PMIC | RK806，在 I2C1 上，地址 `0x23` |
| 过温关机 | 固件把 TSADC 硬件关机设在 120 C（`PcdTsadcTshutEnable`、`PcdTsadcTshutTemp`），再加上每个通道的 OTP 校准值 |

由于 SoC 是 ARMv8.0，Windows 11 24H2 及以后的版本无法在上面运行。

## Radxa ROCK 4D

| | |
|---|---|
| 固件存放位置 | 16 MB SPI NOR。v1.12 的板子用 XMC XM25QU128C，更早的板子用 Macronix 的芯片；SPL 从 0.2.1 起两种都支持。 |
| MaskROM 端口 | 上面那个 USB 3.0 USB-A 口（USB-C 口只供电） |
| 存储 | UFS、microSD、PCIe 上的 NVMe。没有 eMMC。 |
| 测试过的型号 | 12 GB LPDDR5 |

## ArmSoM CM5-IO

| | |
|---|---|
| 固件存放位置 | SD 卡或 eMMC。底板上的 SPI NOR 只有 64 KB，太小。 |
| 启动顺序 | BootROM 先试 eMMC，再试 SD 卡。 |
| MaskROM 端口 | USB-C |
| 存储 | CM5 核心板上的 eMMC、microSD、PCIe 上的 NVMe |
| 以太网 PHY | Motorcomm YT8531C，无晶振：25 MHz 参考时钟由 SoC 提供。 |
| USB-A | 接在板载的 4 口 hub 后面 |
| USB-C | FUSB302；固件从它读取插头方向，并在 ExitBootServices 时启动 USBDP PHY 的 USB3 部分。SuperSpeed 在这个口上还没有训练成功过。 |
| 音频 | ES8388，在 I2C0 和 SAI1 上，接到一个 3 针排针（CN3），没有耳机孔；固件不描述它。SAI6 上的 HDMI 音频固件会描述（`PcdHdmiAudioPreset` 为 TRUE）。 |
| 测试过的型号 | 4 GB |

## 立创泰山派 3M

这里的所有内容都来自同一块板子，第一次启动是 2026-11-12。

| | |
|---|---|
| BootROM 启动顺序 | 先 eMMC，再 USB。它不会尝试 SD 卡：SD 卡上放了有效的 idblock，结果没有任何输出（只试过一张卡）。启动顺序由 `SARADC_IN0_BOOT` 上的分压电阻决定。 |
| 固件存放位置 | SPL 在 eMMC 的 64 扇区。FIT（BL31 + EDK2）在 SD 卡或 eMMC 上；SPL 先试 SD 卡。 |
| MaskROM | 没有按键。上电时短接标有 `MSK` 的两个焊盘，它们就在 `RK3576` 丝印正下方。下载口是标有 `OTG+DP` 的 USB-C。启动倒计时中按 F4 也会复位进入 MaskROM。 |
| 存储 | 58 GiB eMMC（62008590336 字节）、microSD。mini-PCIe 插槽可以走 PCIe、SATA 或 USB 模块。 |
| 以太网 PHY | Maxio MAE0621A-Q3C，ID `7b74.4412`，MDIO 地址 1，同时也响应地址 0。延迟由 strap 设定：RX 延迟在 PHY 里，TX 延迟 `0x24` 在 MAC 里。固件不往它写任何东西。 |
| USB-A | 四个口，接在 DRD1 上的板载 hub 后面。固件的设备树把它们的 VBUS 供电标为常开（主线设备树没有让任何东西管它们，结果 Linux 把它们关掉了）。 |
| USB-C | FUSB302 在 I2C2 上；VBUS 开关 SY6280 由 GPIO0_B6 使能。开机时和 ExitBootServices 时，固件各读一次 CC，只有接的是受电设备（Rd）时才打开 VBUS；接充电器或电脑（Rp）时保持关闭。之后没有操作系统驱动监视这个口，所以开机后才插入的设备得不到 VBUS。没有 VCONN。 |
| DSI 屏幕 | 立创的 800x1280 屏幕，料号 C41409463：DSI 接口的 ILI9881C 面板，复位接 GPIO2_B4，背光接 PWM2 通道 2（GPIO3_C5），安装方向是横屏时面板左边朝上。由固件点亮；操作系统拿到的是竖屏。屏幕有应答时，固件的设备树会以设置里的亮度启用它的 pwm-backlight，并在 force GOP 关闭时启用 DSI 主机和面板（`rocknix,generic-dsi`，所以要用 Armbian 内核）；见 `FdtFixupDsiPanel`。只能在断电时插拔排线。 |
| 触摸 | Goodix GT9271，在 I2C0 上，地址 0x5d（GPIO0_C1、GPIO0_C2）。固件读取它来操作菜单，并根据它是否存在来判断屏幕有没有接上。固件从不写入控制器的配置。 |
| HDMI | GPIO4_C6 使能电平转换器。HPD 经过两级三极管（Q17、Q16）到达 SoC，所以没有反相：接上显示器时 GPIO4_C1 读到 1。接口的 5 V 来自系统电源轨，经过一个二极管，不受开关控制。 |
| 音频 | ES8388 在 I2C3 0x11，SAI1（DMA0 请求 2 和 3，lane 0 从 SDO2 输出），耳机功放使能 GPIO4 PB2，耳机插入检测 GPIO4 PB0，mclk_sai1 为 12.288 MHz，来自 clk_audio_frac_1。`PcdAnalogAudioPreset` 为 TRUE 时（这块板子就是）由 `Tspi3mAudio.c` 设置。 |
| 蓝牙 | AP6256（BCM4345C5），在 UART6 上（uart6m1，GPIO2 PD2 到 PD5），sclk_uart6 为 24 MHz，来自 xin24m；BT_REG_ON 为 GPIO1 PC7，BT_WAKE 为 GPIO1 PD4。32 kHz 睡眠时钟来自 HYM8563 的 CLKOUT，芯片出厂时就是打开的；固件不写它。 |
| RTC | HYM8563，在 I2C2 0x51。这块板子上每次断电和复位它都会丢失时间（没有电池），所以时间由网络（NTP）或操作系统设置。 |
| GPU 供电 | `vdd_gpu_s0` = RK806 BUCK5，上电时 750 mV。固件读取它，为了 702 MHz 工作点把它升到 775 mV（`PcdGpuRailRaise775`），并把读数以 `GpuRail` 变量的形式提供给 Windows GPU 驱动。除非驱动的 `GpuClkHigh` 设置打开，否则驱动保持在 594 MHz。 |
| 风扇 | 2 针接口 CN2，接 PWM2 通道 3（GPIO3_D0），高电平为开，由一个 P-MOSFET 在供电端开关。固件以 100%（`PcdPwmFanSpeedDefault`）、2 kHz 周期（`PcdPwmFanPeriodNs` 500000）启动它；在 60% 时能听到 2 kHz 的开关声。在 Windows 下，除非在设置里启用 "Fan Control by ACPI Thermal Zone"（默认关闭），风扇一直保持 100%。在 Linux 下，固件的设备树描述了 PWM 通道，但没有 `pwm-fan`，所以风扇停在固件留下的状态。 |
| 测试过的型号 | 4 GB LPDDR5，64 GB eMMC |
