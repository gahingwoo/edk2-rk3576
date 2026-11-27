# 已知问题

[English](known-issues.md) | 简体中文

截至 0.5.0 正式版（2026-11-27）。

| 方面 | 问题 |
|---|---|
| Windows | 11 24H2 及以后跑不了（RK3576 是 ARMv8.0），请用 23H2。 |
| Windows | 打开 "CPU Idle States" 后启动时蓝屏 0x5C（HAL_INITIALIZATION_FAILED）。保持 Disabled，即默认值。 |
| Windows | 所有板子的 WiFi 都不能用（ROCK 4D：它的 USB AIC8800D80 没有驱动）；蓝牙只有泰山派 3M 和 CM5-IO 能用。 |
| USB-C，泰山派 3M | 只给上电时插着的设备供 VBUS。接充电器或电脑前先重启。 |
| USB-C，ROCK 4D 和 CM5-IO | 在 Linux 以外只有 USB 2.0：SuperSpeed 建立不了链路（DRD0 一直停在 Rx.Detect）。 |
| 启动项 | 在 eMMC 上，操作系统写入的启动项只有打开 "Save OS Variable Changes to eMMC" 才能在复位后保留。 |
| 时钟，泰山派 3M | HYM8563 RTC 每次断电和复位都丢时间；在 NTP 或操作系统校时前，时钟和截图文件名都是错的。 |
| Linux，泰山派 3M | 设备树里关掉了 GPU（任务超时时 panfrost 触发 SError）；桌面没有声音（见 [Linux](linux.zh-CN.md#限制)）。 |
| 设备树 | ComboPHY 和 PCIe 修正在根节点下找 `/pcie@...`，而不是 `/soc/` 下，所以不起作用。NVMe 不依赖它们也能用。 |
