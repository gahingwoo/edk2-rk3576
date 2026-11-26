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

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#armsom-cm5-io)。
