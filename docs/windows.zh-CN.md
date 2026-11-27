# Windows

[English](windows.md) | 简体中文

Windows 11 23H2（build 22631）可以从 ArmSoM CM5-IO 和立创泰山派 3M 的 eMMC 启动，也可以从 Radxa ROCK 4D
的 NVMe 启动。Windows 11 24H2 及以后的版本需要 ARMv8.1，在 RK3576（ARMv8.0）上跑不了。Windows 10 22H2
应该能跑，还没试过。

## 怎样安装

Windows 安装程序在这里用不了（没有 eMMC 驱动，也没有 TPM）。改用 DISM 写入系统映像，有两个工具：

- [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) 的 WinPE U 盘，按它的
  [安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)操作；
- [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：在电脑上通过 USB 安装，不用 U 盘。在 ROCK 4D 上跑过
  （把 Windows 装到 NVMe）。在 ROCK 4D 上它还能装 NVMe 上的 Windows 加 SD 卡上的 Armbian（固件 0.5.0 或更新）。

两种方法都要求先写入本固件（见[刷写固件](flash.zh-CN.md)）：作为完整映像写到 eMMC（CM5-IO、泰山派 3M），
或写到 SPI NOR（ROCK 4D）。

装好后固件按版本名列出 Windows（"Windows 11"），并且优先启动它（见[固件菜单](firmware-menus.zh-CN.md#windows-启动项)）。

"CPU Idle States" 要保持 Disabled（默认）：打开后 Windows 启动时蓝屏 0x5C
（见[固件菜单](firmware-menus.zh-CN.md#给操作系统用的选项)）。

## 需要哪个固件

| Windows 下的功能 | 需要的固件 |
|---|---|
| eMMC、SD、以太网、USB-A、NVMe（CM5-IO） | 0.4.0 或更新。更早的映像把 eMMC 跑在 148.5 MHz，Windows 下写入的文件可能悄悄损坏。 |
| 泰山派 3M 的 DSI 屏 | 0.4.0 或更新 |
| 泰山派 3M 的触摸、蓝牙、耳机孔、HDMI 音频、USB-C | [0.5.0](https://github.com/gahingwoo/edk2-rk3576/releases/tag/0.5.0) 或更新 |
| ROCK 4D 上的一切 | 0.5.0 或更新（更早的版本没有它的 ACPI 表） |

## 更多驱动

- GPU 和显示：[wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52)（旋转、亮度、HDMI 热插拔和音频），
  在 [woa-rk3576 0.5.0](https://github.com/gahingwoo/woa-rk3576/releases/tag/0.5.0) 的驱动包里。没有它时，桌面由 CPU 画到固件的帧缓冲里，只有一个固定分辨率。
- NPU：[woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu)，需要在设置里打开 "NPU (experimental)"。也在驱动包里。
- ROCK 4D 的 Wi-Fi 和蓝牙（USB 的 AIC8800D80）：还没有驱动。

## 时钟

泰山派 3M 的 RTC 保存不了有效时间，所以不插网线时（见[固件时钟](firmware-menus.zh-CN.md#时钟)），时间要等 Windows
自己同步才对。把 `HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal`
设为 1（REG_DWORD），让 Windows 把固件时钟当作 UTC。更多见 woa-rk3576 的
[故障排查](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/troubleshooting.zh-CN.md)。
