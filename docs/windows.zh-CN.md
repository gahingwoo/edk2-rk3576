# Windows

[English](windows.md) | 简体中文

Windows 11 23H2（build 22631）可以从 ArmSoM CM5-IO 和立创泰山派 3M 的 eMMC 启动。Windows 11 24H2
及以后的版本需要 ARMv8.1，在 RK3576（ARMv8.0）上跑不了。Windows 10 22H2 应该能跑，还没试过。
ROCK 4D 编译时不带 ACPI，所以不能启动 Windows。

## 怎样安装

Windows 安装程序在这里用不了（没有 eMMC 驱动，也没有 TPM）。改用 DISM 写入系统映像，有两个工具：

- [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) 的 WinPE U 盘，按它的
  [安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)操作；
- [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：在电脑上通过 USB 安装，不用 U 盘。还没有在板子上跑过。

两种方法都要求先把本固件作为完整映像写到 eMMC（见[刷写固件](flash.zh-CN.md)）。

## 需要哪个固件

| Windows 下的功能 | 需要的固件 |
|---|---|
| eMMC、SD、以太网、USB-A、NVMe（CM5-IO） | 0.4.0 或更新。更早的映像把 eMMC 跑在 148.5 MHz，Windows 下写入的文件可能悄悄损坏。 |
| 泰山派 3M 的 DSI 屏 | 0.4.0 或更新 |
| 泰山派 3M 的触摸、蓝牙、耳机孔、HDMI 音频、USB-C | 下一个正式版之前用 [`ci` 构建](flash.zh-CN.md#滚动的-ci-构建) |

## 更多驱动

- GPU 和显示：[wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52)（旋转、亮度、HDMI 热插拔和音频）。
  在泰山派 3M 上跑过，还没有正式版。没有它时，桌面由 CPU 画到固件的帧缓冲里，只有一个固定分辨率。
- NPU：[woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu)，需要在设置里打开 "NPU (experimental)"。仅泰山派 3M。

## 时钟

泰山派 3M 的 RTC 保存不了有效时间，所以不插网线时（见[固件时钟](firmware-menus.zh-CN.md#时钟)），时间要等 Windows
自己同步才对。把 `HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal`
设为 1（REG_DWORD），让 Windows 把固件时钟当作 UTC。更多见 woa-rk3576 的
[故障排查](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/troubleshooting.zh-CN.md)。
