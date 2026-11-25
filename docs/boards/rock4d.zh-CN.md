# Radxa ROCK 4D

[English](rock4d.md) | 简体中文

固件放在 ROCK 4D 的 16 MB SPI NOR 上，从 NVMe 启动 Fedora。映像现在和另外两块板子一样，在设备树之外同时带 ACPI 表；还没有在这块板子上启动过 Windows。

泰山派 3M 有、而这块板子也有对应硬件的功能，在分支 `rock4d-parity` 上，已构建，未在 ROCK 4D 上运行：温区、J16 上全速运行的风扇（以及 "Fan Control by ACPI Thermal Zone"）、通过 RK806 关机、"CPU Maximum Frequency"、NPU 选项、GPU 供电轨报告、HDMI 音频、ES8388 耳机插孔，以及针对 Armbian SD 卡的 "Armbian (U-Boot)"。逐板完整列表：[板子功能对照](../BOARD-FEATURES.zh-CN.md)。

## 能用什么

| | 固件里 | Linux（Fedora） |
|---|---|---|
| HDMI | 可用，2560x1440，取自显示器 EDID | 可用 |
| NVMe | 可用 | 可用，作为系统盘 |
| USB | 可用；USB-C 为 USB 2.0 | 可用 |
| 以太网 | 可用 | 可用 |
| SD 卡 | 无记录 | 无记录 |
| UFS | 未试过 | 未试过 |
| Windows | 未试过（ACPI 表已构建，未运行） | |

0.2.1 之后又在板子上测过，具体哪个版本没有记录。0.2.1 本身由一位用户在 v1.12 板（XMC SPI
闪存）上跑过：能进启动菜单，能从 USB 启动 Fedora 安装程序。之后的显示、I2C 和菜单改动是三块板共用的代码，在另外两块板上跑过。

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：用 USB-A 对 USB-A 线，把板子上方那个 USB 3.0 USB-A 口接到电脑，按住 MaskROM
  键再插电源。USB-C 口只供电。

## 安装固件

把 `ROCK4D-spi` 映像写到 SPI NOR（[刷写固件](../flash.zh-CN.md#radxa-rock-4d)）。UEFI 设置也存在这块闪存上，写入映像会清空设置。

接下来：[Linux](../linux.zh-CN.md)。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#radxa-rock-4d)。
