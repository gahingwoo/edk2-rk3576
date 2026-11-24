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

## 硬件细节

见[硬件](../dev/hardware.md#armsom-cm5-io)（英文）。
