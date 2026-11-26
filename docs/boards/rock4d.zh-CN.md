# Radxa ROCK 4D

[English](rock4d.md) | 简体中文

固件放在 16 MB SPI NOR 上，从 NVMe 启动 Fedora 或 Windows 11 23H2。映像在设备树之外同时安装 ACPI 表。

## 能用什么

固件和 Linux：0.2.1 正式版之后在板子上测过（版本没有记录）；0.2.1 由一位用户在 v1.12 板（XMC SPI 闪存）上跑过。
Windows：2026-11-26 的 `main`，12 GB 板。

| | 固件里 | Linux（Fedora） | Windows 11 23H2 |
|---|---|---|---|
| HDMI | 可用，2560x1440，取自 EDID | 可用 | 可用，GPU 驱动，1920x1080 60 Hz |
| NVMe | 可用 | 可用，作为系统盘 | 可用，作为系统盘 |
| USB | 可用；USB-C 为 USB 2.0 | 可用 | 无记录 |
| 以太网 | 可用 | 可用 | 可用，1 Gbps |
| 音频 | | 无记录 | 耳机、耳机麦克风、HDMI |
| NPU、视频解码器 | | 无记录 | 可用 |
| Wi-Fi、蓝牙（USB 模块） | | 无记录 | 没有驱动 |
| SD 卡 | 无记录 | 无记录 | 无记录 |
| UFS | 未试过 | 未试过 | 没有驱动 |

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：用 USB-A 对 USB-A 线把上方的 USB 3.0 口接到电脑，按住 MaskROM 键再插电源。USB-C 口只供电。

## 安装固件

把 `ROCK4D-spi` 映像写到 SPI NOR（见[刷写固件](../flash.zh-CN.md#radxa-rock-4d)）。UEFI 设置在同一块闪存上，
写入会重置设置。然后装 [Linux](../linux.zh-CN.md) 或 Windows（见下文）。

## Windows

- 用 [`ci` 构建](../flash.zh-CN.md#滚动的-ci-构建)：0.4.1 正式版没有这块板的 ACPI 表。以太网需要 2026-11-26
  或之后的构建（RTL8211F 的 RGMII 延时）；更早的构建能连上链路，但拿不到 DHCP 地址。
- 用 [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio) 安装（把 Windows 装到 NVMe）。它还能在 Windows
  之外把 Armbian 装到 SD 卡上；这需要分支 `rock4d-parity` 的 SPL，`main` 上还没有。
- "CPU Idle States" 保持 Disabled（默认），否则 Windows 启动时蓝屏 0x5C。
- 已构建、还没跑过：LED、EEPROM、40-pin 排针（默认关）、J16 风扇控制、Maskrom 键和耳机插孔检测（SARADC）、
  775 mV GPU 选项。详见[板子功能对照](../BOARD-FEATURES.zh-CN.md)、[Windows](../windows.zh-CN.md)。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#radxa-rock-4d)。
