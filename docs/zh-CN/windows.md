# Windows

[English](../windows.md)（本页对应 2026-11-24 的英文版）

Windows 11 23H2 可以从 ArmSoM CM5-IO 和立创泰山派 3M 的 eMMC 启动。Radxa ROCK 4D 编译时不带 ACPI 表，不能启动 Windows。

## 用哪个版本

RK3576 是 ARMv8.0。Windows 11 24H2 及以后的版本需要 ARMv8.1，跑不了。请用 Windows 11
23H2（版本号 22631）。Windows 10 22H2 应该也能跑，但没有试过。

## 怎样安装

Windows 安装程序在这些板子上用不了：它没有 eMMC 控制器的驱动，也不接受没有 TPM 的机器。
所以改用 DISM 直接写入系统映像，有两个工具：

- [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) 的 WinPE U 盘：每一步见
  [安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/zh-CN/install.md)；
- [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：在电脑上通过 USB 安装，不用 U 盘。截至 2026-11-24 还没有在板子上跑过。

两种方法都要求 eMMC 上已经先用完整映像写好本固件（见[刷写固件](flash.md)）。

## 需要哪个固件

| Windows 下的功能 | 需要的固件 |
|---|---|
| Windows 本身：eMMC、SD、以太网、USB-A、NVMe（CM5-IO） | 0.4.0 或更新。更早的映像把 eMMC 跑在 148.5 MHz 而不是 52 MHz，Windows 下写入的文件可能读回来就变了，却不报任何错：泰山派 3M 是 0.3.0 之前，CM5-IO 是 0.4.0 之前。 |
| 泰山派 3M 的 DSI 屏 | 0.4.0 或更新 |
| 泰山派 3M 的触摸、蓝牙、耳机孔、HDMI 音频、USB-C | 下一个正式版发布前，用 `ci` 构建（见[刷写固件](flash.md#滚动的-ci-构建)） |

## 更多驱动

- GPU 和显示：[wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) 让 Mali-G52 绘制桌面，
  支持旋转、亮度、HDMI 热插拔和 HDMI 音频。在泰山派 3M 上跑过，还没有正式版。没有它时，桌面由 CPU 画到固件的帧缓冲里，只有固件设定的那一个分辨率。
- NPU：[woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu)，需要在设置里打开 "NPU (experimental)"。仅泰山派 3M。

## 时钟

泰山派 3M 的 RTC 保存不了有效时间，所以不插网线时（见[固件时钟](firmware-menus.md#时钟)），每次启动后时间都不对，直到
Windows 自己同步。把
`HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal`
设为 1（REG_DWORD），让 Windows 把固件时钟当作 UTC。更多见 woa-rk3576 的
[故障排查](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/zh-CN/troubleshooting.md)。
