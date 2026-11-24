# Linux

[English](../linux.md)（本页对应 2026-11-24 的英文版）

Linux 像在 PC 上一样通过固件启动：从 U 盘、NVMe 或 eMMC，经过 GRUB，使用固件自带的设备树。不需要往 ESP 里放任何东西。

## Radxa ROCK 4D 和 ArmSoM CM5-IO

Fedora（ROCK 4D 上是 44，CM5-IO 上是 45）用 Fedora ARM64 U 盘安装，从 NVMe 启动。CM5-IO 上的 GNOME 桌面通过 Panfrost 跑在 Mali-G52 上。

1. 写入固件（见[刷写固件](flash.md)）。
2. 在启动管理器里选 Fedora ARM64 安装 U 盘。
3. 安装到 NVMe。

## 立创泰山派 3M

在一块板子上跑过 Armbian edge（Linux 7.3.4，GNOME）：装在 eMMC 上 Windows 旁边的一个 6 GB 分区里，从启动管理器经 GRUB 启动。

固件的设备树在主线板级文件之外，还描述了 ES8388 耳机编解码器、GT9271 触摸屏、PWM 背光、DSI 屏（仅在接上时）、给 EC20 模块的 mini-PCIe 供电，以及 GPU（目前关闭，见下文）。

在这套系统上确认过：耳机可以播放（通过 ALSA）、触摸、`/sys/class/backlight` 调背光（需要 Armbian 带的 RK3576 PWM 驱动）、USB-A 口、以太网和 EC20。

### 屏幕的两种用法

ACPI / Device Tree 页的 "Force UEFI GOP Display" 决定：

- **关：由 Linux 驱动屏幕。** 固件在 ExitBootServices 时停掉自己的画面，交给内核的 VOP2 和 DSI 驱动，屏幕会显示 GNOME 登录界面。需要内核里有这块屏的驱动（Armbian 的 rocknix generic DSI 驱动；主线还没有）。GNOME 会按屏幕方向转成横屏，触摸也跟着转。从上电到这个驱动加载之前，屏幕是黑的。
- **开：Linux 沿用固件的画面**，通过 simpledrm 或 efifb：固定 800x1280 竖屏，由 CPU 绘制。GNOME 没法把触摸屏绑到这个输出上，所以要转成横屏，需要给触摸屏加一条 `LIBINPUT_CALIBRATION_MATRIX` udev 规则（输出向 `left` 旋转时是 `0 -1 1 1 0 0`）。这种模式下内核自己的显示驱动是关的，所以 Linux 下 HDMI 和 HDMI 音频不能用。

### 限制（2026-11-24）

- GPU 在固件设备树里是关的。打开后 panfrost 能加载，但 GPU 任务超时时内核会因 SError 崩溃。原因还没查明。
- Armbian 的板级支持还没考虑 UEFI。它的屏幕服务会写一个 U-Boot overlay 设置然后重启，在 UEFI 下会无限循环：请屏蔽 `lckfb-tspi-3m-screen.service`。
- Armbian 的 ALSA UCM 配置按声卡长名命名。UEFI 下内核按 SMBIOS 给声卡命名（`LCKFB-TaishanPi3M-0`），而不是 `rockchip-es8388`，所以 PipeWire 找不到它。把配置按新名字链接过去后声卡能初始化，但 GNOME 的输出仍然无声，麦克风录到的也是静音。
- 关掉 "Force UEFI GOP Display" 时的 HDMI 还没在这块板上试过。
- Linux 下 USB-C 口没能识别试过的硬盘（内核的 tcpm 接管了这个口），USB-A 正常。
- 安装程序不知道 Windows 也在 eMMC 上。上面的 Armbian 是手动写进 Windows 让出来的分区里的。
- RTC 里没有有效时间，看到 `rtc-hym8563: no valid clock/calendar values` 是正常的。时间来自网络。

Armbian 官方为泰山派 3M 和 CM5-IO 提供的映像用 U-Boot 启动，不经过本固件。
