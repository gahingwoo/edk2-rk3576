# Radxa ROCK 4D

[English](rock4d.md) | 简体中文

固件放在 16 MB SPI NOR 上，从 NVMe 启动 Fedora。这块板子编译时不带 ACPI 表，所以不能启动 Windows。

## 能用什么

0.2.1 正式版之后在板子上测过（版本没有记录）；0.2.1 由一位用户在 v1.12 板（XMC SPI 闪存）上跑过。
之后的显示、I2C 和菜单改动是共用代码，在另外两块板上测过。

| | 固件里 | Linux（Fedora） |
|---|---|---|
| HDMI | 可用，2560x1440，取自 EDID | 可用 |
| NVMe | 可用 | 可用，作为系统盘 |
| USB | 可用；USB-C 为 USB 2.0 | 可用 |
| 以太网 | 可用 | 可用 |
| SD 卡 | 无记录 | 无记录 |
| UFS | 未试过 | 未试过 |
| Windows | 不支持（没有 ACPI 表） | |

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：用 USB-A 对 USB-A 线把上方的 USB 3.0 口接到电脑，按住 MaskROM 键再插电源。USB-C 口只供电。

## 安装固件

把 `ROCK4D-spi` 映像写到 SPI NOR（见[刷写固件](../flash.zh-CN.md#radxa-rock-4d)）。UEFI 设置在同一块闪存上，
写入会重置设置。然后装 [Linux](../linux.zh-CN.md)。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#radxa-rock-4d)。
