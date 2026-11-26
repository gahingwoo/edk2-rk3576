# 板子功能对照：泰山派 3M、CM5-IO、ROCK 4D

[English](BOARD-FEATURES.md) | 简体中文

2026-11-25 为功能冻结所做的清单：逐块板子列出每个设置选项、ACPI 设备、固件功能和
Windows 驱动的匹配情况，以立创泰山派 3M 为基准。

* **泰山派 3M** 和 **CM5-IO**：`main`（0b03605）构建出来的样子。CM5-IO 一列不包括
  正在并行开发、会改变这一列的分支 `cm5io-parity`。
* **ROCK 4D**：分支 `rock4d-parity` 构建出来的样子。和 `main` 不同的地方，格子里会写
  （"main: ..."）。
  在 `main` 上 ROCK 4D 完全不构建 ACPI 表（只有设备树），所以 ROCK 4D 一列里的每个
  ACPI 设备和每个 Windows 驱动都只存在于 `rock4d-parity` 上。
* 任何板子的 `main` 上都还没有的分支，列在最后。

标记：**yes** = 有；**missing** = 硬件有，固件或 ACPI 没有；**n/a** = 这块板子没有这个
硬件。"未运行"指已构建但从未在那块板子上运行过。`rock4d-parity` 的 ROCK 4D 一列里的
内容都没有在 ROCK 4D 上运行过。

硬件事实来自各板的主线设备树（`devicetree/mainline/`）、原理图（ROCK 4D V1.12、
CM5-IO 1V1、立创泰山派 3M v1.4）和厂商内核设备树；ROCK 4D 的出处写在
`Platform/Radxa/ROCK4D/` 里。

## 设置选项

| 选项（页面） | 显示条件 | 泰山派 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| Config Table Mode（ACPI / Device Tree） | 所有板子 | yes，ACPI + DT | yes，ACPI + DT | yes，ACPI + DT（main：只有设备树） |
| ACPI PCIe ECAM 兼容模式 | ACPI 开启时 | yes | yes | yes |
| Fan Mode（风扇模式） | `PcdAcpiFanControlSupported` | yes，默认常开 | yes，默认常开 | yes，默认常开（main：missing） |
| CPU Idle States（`_LPI`） | 所有板子 | yes | yes | yes |
| NPU（实验性） | `PcdRk806I2cBase` != 0 | yes | missing（I2C1 0x23 上有 RK806） | yes（main：missing） |
| 视频解码器（VDPU383） | 所有板子 | yes | yes | yes |
| SDIO Wi-Fi | `PcdAcpiSdioWifiSupported` | yes（AP6256） | main 上 missing（SDIO 上的 SYN43752；也没有 Windows 驱动） | n/a：Wi-Fi 是 USB 模块（FCU760K，AIC8800D80） |
| 设备树：兼容模式、Force UEFI GOP、DTB 覆盖、overlay、fixup | DT 开启时 | yes | yes | yes |
| CPU Maximum Frequency | `PcdCpuMaxFreqSupported` | yes，1608 MHz | missing（RK806 供电轨相同） | yes，1608 MHz（main：missing） |
| GPU voltage for 702 MHz (775 mV) | `PcdGpuRail775Supported` | yes，开 | main 上 missing（分支 `cm5io-parity`：yes，关） | yes，关（main：missing） |
| 首次启动时询问一次 CPU 频率（接了屏幕时） | `PcdCpuMaxFreqSupported` | yes | 否（不提供该选项） | yes（main：否） |
| 截图保存位置 | 所有有显示的板子 | yes | yes | yes |
| Network Time Sync | 所有有 IPv4 的板子 | yes | yes | yes |
| Save OS Variable Changes to eMMC | 所有板子 | yes | yes | 会显示，但用不到：变量区在 SPI NOR 上，OS 的写入会立即写到那里 |
| 显示：模式、优先级、强制输出、复制输出、旋转、HDMI 信令 | 有显示时 | yes | yes | yes |
| 显示：屏幕亮度 | `PcdDisplayConnectors` 里有 MIPI DSI | yes | n/a（不支持 DSI 屏） | n/a：板上有 DSI 接口（"MIPI CSI/DSI" 页），但不支持任何屏 |
| ComboPHY 模式 | 所有板子 | yes | yes | yes |
| 语言（英文 / 简体中文） | 所有板子 | yes | yes | yes |

## 固件功能

| 功能 | 泰山派 3M | CM5-IO | ROCK 4D |
|---|---|---|---|
| Debug report（Ctrl+Alt+F11，把启动日志存到 ESP） | yes | yes | yes |
| 截图 | yes | yes | yes |
| 屏幕键盘和 USB 鼠标 | yes（也能触摸） | yes | yes |
| 菜单里的触摸、触摸测试 | yes | n/a | n/a |
| Reset to MaskROM（F4） | yes | yes | yes |
| Reset All Settings | yes | yes | yes |
| 启动时 NTP 校时 | yes | yes | yes |
| 带电池的 RTC（I2C2 上的 HYM8563 系列） | yes（未装电池） | yes | yes（TCS8563，电池座 J6） |
| NV 变量区 | eMMC 上，在固件区域里 | 启动介质上（eMMC 或 SD） | SPI NOR 的 0xFC0000 |
| 通过 RK806 关机（Shut Down、`reset -s`、Windows） | yes | missing（I2C1 0x23 上有 RK806） | yes（main：missing） |
| GPU 供电轨报告（`GpuRail` 变量，只读） | yes | missing | yes（main：missing） |
| 为 702 MHz 把 GPU 供电轨升到 775 mV | yes，设置选项默认开 | 分支 `cm5io-parity` 上是设置选项，默认关 | 设置选项，默认关（main：missing）；开时 Windows 驱动跑 702 MHz，关时 594 MHz |
| TSADC 读数和 120 C TSHUT | yes | yes | yes |
| 风扇以 100% 启动 | yes（CN2，PWM2 ch3） | yes（CN32，PWM2 ch6，极性有争议） | yes，J16，PWM2 ch5，GPIO4_C5，正常极性（main：没有风扇） |
| HDMI 音频预设（SAI6，48 kHz） | yes | yes | yes（main：关） |
| 模拟音频预设（SAI1 + I2C3 上的 ES8388） | yes，带功放和插孔检测 | main 上没有 | yes：耳机插孔和它的麦克风，没有功放（未贴片），插孔检测走 SARADC 通道 3，由 rk3xsaradc 读取（分支，未运行）（main：missing） |
| "Armbian (U-Boot)" 一次性启动 | yes（请求放在 eMMC） | missing（它的 SPL 没有用 `build-spl.sh` 重新构建） | `rock4d-win-test` 上没有（需要新 SPL，板子跑过之前不放进来） |
| 从固定版本的源码构建 SPL（`build-spl.sh`） | yes | 否（预编译的 `idblock_cm5io.bin`） | yes，U-Boot v2026.10（main：预编译的 2026.04） |
| MIPI DSI 屏 | yes | n/a | missing：有接口，没有屏 |
| FUSB302 控制 USB-C VBUS | yes | yes | n/a：USB-C 只用于供电 |
| 为 OS 给蓝牙上电 | yes（UART6） | missing | ACPI 中 n/a：蓝牙在 USB Wi-Fi 模块上 |

## ACPI 设备

| 设备 | `_HID` | 泰山派 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| CPU，`_LPI`（RKLPI / RKWFI SSDT） | ACPI0007 | yes | yes | yes |
| PCIe 根复合体（PCI0、PCI1） | PNP0A08 | yes（没链路时隐藏） | yes | yes（没链路时隐藏） |
| eMMC | RKCP0D40 | yes | yes | n/a：没有 eMMC，它的引脚给了 SPI NOR |
| SD 卡 | RKCPFE2C | yes | yes | yes |
| SDIO Wi-Fi 主机（RKWIFI SSDT） | RKCPFE2D | yes，设置选项 | missing | n/a（USB Wi-Fi） |
| GPIO 组、引脚控制器 | RKCP3002、RKCP3005 | yes | yes | yes |
| DMA（PL330） | ARMH0330 | yes | yes | yes |
| UART2 | HISI0031 | 没有（引脚不可用） | yes | yes |
| I2C0..I2C9 | RKCP3001 | yes | yes | yes |
| SPI0..SPI4 | RKCP3003 | yes | yes | yes（SFC0 的 `_STA` 为 0） |
| RK806 PMIC | RKCP0806 | yes | yes | yes |
| HYM8563 RTC | HYMB0001 | yes | yes | yes |
| 触摸（GT9271） | RKCP9271 | yes | n/a | n/a |
| 蓝牙（BCM4345C5） | RKCP4345 | yes | missing | n/a（USB） |
| USB XHC0 / XHC1 | PNP0D10 | yes | yes（另有 XHC2 空壳） | yes |
| GMAC0 | RKCP6543 | yes | yes | yes |
| 显示（VOP2） | RKCP7403 | yes | yes | yes |
| GPU（Mali-G52） | RKCP7402 | yes | yes | yes |
| 温区 TZ00..TZ02，`_CRT` 115 C | （ThermalZone） | yes | yes | yes |
| 风扇 SSDT（RKFAN，3 个 PNP0C0B） | PNP0C0B | yes | yes | yes，所有档位 100% |
| HDMI 音频 SAI6 | RKCP3004 | yes | yes | yes |
| SAI1 + ES8388 | RKCP3004、RKCP8388 | yes | main 上没有 | yes，没有插孔检测 |
| NPU（RKNPU SSDT） | RKCP3576 | yes | 已构建，从不安装（选项隐藏） | yes |
| 视频解码器（RKVDEC SSDT） | RKCP0383 | yes | yes | yes |
| SATA | RKCP0161 | n/a | 只有空壳 | n/a |
| UFS | | n/a | n/a | missing：没有 Windows 驱动 |
| I2C EEPROM（BL24C16F，I2C6 0x50） | | n/a | n/a | 未声明：没有 Windows 驱动 |

## Windows 驱动

每个驱动绑定的都是三块板子的 DSDT 共用的 ACPI `_HID`，所以没有哪个 INF 需要为 ROCK 4D
加板子专用的硬件 ID。需要板级接线的驱动都从 ACPI（`_CRS`、`_DSD`）读取。

| 驱动（仓库） | 绑定 | 泰山派 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| rk3xi2c、rk3xgpio、rk3xspi、pl330dma（woa-rk3576） | RKCP3001、RKCP3002、RKCP3003、ARMH0330 | yes | yes | yes |
| dwcsdhc（eMMC）、dwcmshc（SD） | RKCP0D40、RKCPFE2C | yes | yes | 只有 SD |
| dwc_eqos（以太网） | RKCP6543 | yes | yes | yes |
| rk3xi2sbus、csaudiork3x（HDMI、耳机） | RKCP3004 | yes | HDMI | HDMI 和耳机（无插孔检测） |
| es8323（ES8388） | RKCP8388 | yes | main 上 n/a | yes；需要 woa-rk3576 `es83-optional-irq` 或更新（旧版 es8323 会创建一个这里 `_CRS` 没有的中断）；`_DSD` `rockchip,output-route` 1、`rockchip,input-select` 1（OUT1 接 J18，LIN1 耳机麦克风） |
| rk3576-null（PMIC、RTC、引脚控制器、UART2） | RKCP0806、HYMB0001、RKCP3005、HISI0031 | yes | yes | yes，PMIC 现在是 RKCP0806 |
| bcm4345bt | RKCP4345 | yes | n/a | n/a |
| gt9271 | RKCP9271 | yes | n/a | n/a |
| vdpu383 | RKCP0383 | yes | yes | yes |
| bcmwlsdio（分支 `tspi3m-wifi-driver`） | RKCPFE2D 下的 SDIO 卡 | 分支 | 可以给 SYN43752 复用 | n/a |
| USB 上的 Wi-Fi + 蓝牙（AIC8800D80） | USB | n/a | n/a | missing：Mario Bălănică 的 aicwlan 需要 Windows 24H2，这颗 SoC 跑不了；未移植 |
| malidod、maliwddm、malikm（wddm-mali-g52） | RKCP7403、RKCP7402 | yes | yes | yes。三块板子的 HPD 都在 GPIO4_C1 |
| rknpu（woa-rk3576-npu） | RKCP3576 | yes | 选项隐藏 | yes，需打开 NPU 选项 |
| 系统自带：usbxhci、pci、ACPI 温区和风扇 | PNP0D10、PNP0A08、PNP0C0B | yes | yes | yes |

## ROCK 4D 还缺什么，为什么

* **GPU 702 MHz：可选，未在板上跑过。** vdd_gpu_s0 是和泰山派 3M 相同的 RK806S-5 的 BUCK5
  （原理图 V1.12 第 25 页 VOUT5/FB5 到 VDD_GPU_S0，第 5 页 I2C1_M0 在 GPIO0_B2/B3；
  `rk3576-rock-4d.dts` dcdc-reg5，550000 到 900000 uV，`&gpu` 的 mali-supply）。
  "GPU voltage for 702 MHz (775 mV)" 默认关：在打开这个选项并通过下面的板上测试之前，
  Windows 驱动跑 594 MHz。
* **Windows 下的 Wi-Fi 和蓝牙。** USB 的 AIC8800D80；没有能在 23H2 上运行的驱动。已记录，未移植。
* **耳机插孔检测。** 是 SARADC 输入（通道 3，J18 的麦克风触点）。已写好、未运行：\_SB.ADC0 带有按原理图计算的阈值（低于 1.45 V 为插入，高于 1.52 V 为拔出）、woa-rk3576 的 rk3xsaradc 驱动，以及跟随它的 es8323。没有该驱动时耳机端点一直显示"已插入"。
* **UFS、DSI 接口、EEPROM、LED。** 没有 Windows 驱动，没有屏，没有可驱动的东西。
* **通过 "Armbian (U-Boot)" 启动 USB 或 NVMe 上的 Armbian。** SPL 只能启动 SD 卡上的 FIT；
  其他板子用的 eMMC 槽位在这里不存在。

## 板上测试：702 MHz 用的 GPU 电压

风扇开着：

1. 把 "GPU voltage for 702 MHz (775 mV)" 设为开并重启。串口日志显示
   `GpuRail: raised BUCK5 0x28 -> 0x2C, read back 775000 uV`。如果显示的是
   `not raising BUCK5: ...`，记下原因（这块板的 RK806 还没读过）；这时什么都没写。
2. Windows 下，没有存 `GpuClk`、没设 `GpuClkHigh` 时：`m5test clk`（或 GPU 时钟性能计数器）显示 702 MHz。
3. FurMark 跑 60 秒：没有 TDR；记下分数和 GPU、CPU 最高温度。
4. 把选项设为关并重启：日志显示 `lowered BUCK5 0x2C -> 0x28, read back 750000 uV`，
   Windows 跑 594 MHz。

## 任何板子的 main 上都没有的

`tspi3m-gpu-volt-read`。`fan-onoff` 和 `cpu-ask-once` 已于 2026-11-25 合并；ROCK 4D 按它的
PCD（`PcdPwmFanSpeedDefault`、`PcdCpuMaxFreqSupported`）自动得到这两项，不需要自己的板级代码。
