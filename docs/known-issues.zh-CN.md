# 已知问题

[English](known-issues.md) | 简体中文

截至 2026-11-24 的 `main`。

- Windows 11 24H2 及以后的版本跑不了：RK3576 是 ARMv8.0。请用 Windows 11 23H2。
- ROCK 4D 编译时不带 ACPI 表，不能启动 Windows。
- 泰山派 3M 的 USB-C：只有上电时就插着的设备才有 VBUS 供电，之后插的设备要等下次启动。要接充电器或电脑，先重启。
- ROCK 4D 和 CM5-IO 的 USB-C 在 Linux 以外只有 USB 2.0。固件在 ExitBootServices 时启动了 USBDP PHY 的 USB3 部分，解决了 Windows 关机蓝屏，但 SuperSpeed 没有建立链路：DRD0 一直停在 Rx.Detect。
- 在 eMMC 上，操作系统写入的启动项只有打开实验性的 "Save OS Variable Changes to eMMC" 才能在复位后保留。
- ComboPHY 和 PCIe 的设备树修正在根节点下找 `/pcie@...`，而主线放在 `/soc/` 下，所以不起作用。NVMe 不依赖它们也能用。
- 泰山派 3M 的 RTC（HYM8563）每次断电和复位都丢时间，所以在 NTP 或操作系统校时之前，固件时钟以及截图、调试报告的文件名都是错的。
- 泰山派 3M 上的 Linux：GPU 在固件设备树里是关的（panfrost 在任务超时时因 SError 崩溃），桌面没有声音（见 [Linux](linux.zh-CN.md)）。
- 两块板在 Windows 下 WiFi 都不能用；蓝牙只有泰山派 3M 能用。
- 泰山派 3M 在 Windows 下桌面以外的功能（触摸、蓝牙、音频、USB-C），在 0.4.1 之后的下一个正式版发布前，都要用 `ci` 构建。
