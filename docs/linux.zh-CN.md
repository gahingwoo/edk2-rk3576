# Linux

[English](linux.md) | 简体中文

Linux 像在 PC 上一样通过固件启动：从 U 盘、NVMe 或 eMMC，经过 GRUB，使用固件自带的设备树。不需要往 ESP 里放任何东西。

## Radxa ROCK 4D 和 ArmSoM CM5-IO

Fedora（ROCK 4D 上是 44，CM5-IO 上是 45）用 Fedora ARM64 U 盘装到 NVMe。CM5-IO 上 GNOME 通过 Panfrost 跑在 Mali-G52 上。

1. 写入固件（见[刷写固件](flash.zh-CN.md)）。
2. 从启动管理器启动 Fedora 安装 U 盘。
3. 装到 NVMe 盘上。

## 立创泰山派 3M

跑的是 Armbian edge（Linux 7.3.4，GNOME），装在 eMMC 上 Windows 旁边一个 6 GB 的分区里，经 GRUB 启动。
固件设备树在主线板级文件之外还描述了：ES8388 耳机编解码器、GT9271 触摸屏、PWM 背光、DSI 屏（接上时）、
给 EC20 模块的 mini-PCIe 供电，以及 GPU（已关闭）。验证过：耳机（ALSA）、触摸、背光（需要 Armbian 带的
RK3576 PWM 驱动）、USB-A、以太网、EC20。

### 屏幕的两种用法

ACPI / Device Tree 页的 "Force UEFI GOP Display"：

- **关：Linux 驱动屏幕。** 固件在 ExitBootServices 时停掉自己的画面，由内核的 VOP2 和 DSI 驱动接管。
  内核要有这块屏的驱动（Armbian 的 rocknix generic DSI 驱动；主线还没有）。GNOME 会把画面和触摸转成横屏。
  在驱动启动之前屏幕是黑的。
- **开：Linux 沿用固件的画面**（simpledrm 或 efifb），固定的 800x1280 竖屏，由 CPU 绘制。要横屏，给触摸屏加一条
  udev 规则，`LIBINPUT_CALIBRATION_MATRIX` 设为 `0 -1 1 1 0 0`，并把输出旋转为 `left`。这种模式下 HDMI 和 HDMI 音频不能用。

### 限制

- 固件设备树里关掉了 GPU：打开后 panfrost 能探测到，但 GPU 任务超时时内核会因 SError 崩溃。原因不明。
- Armbian 的屏幕服务会写 U-Boot overlay 设置然后重启，在 UEFI 下会无限循环：屏蔽 `lckfb-tspi-3m-screen.service`。
- UEFI 下声卡按 SMBIOS 命名（`LCKFB-TaishanPi3M-0`），而不是 `rockchip-es8388`，所以找不到 Armbian 的 ALSA UCM 配置。
  把配置按新名字链接后声卡能初始化，但 GNOME 输出和麦克风仍然没有声音。
- 关掉 "Force UEFI GOP Display" 时的 HDMI 还没试过。
- USB-C 没能枚举硬盘（内核的 tcpm 接管了这个口）；USB-A 正常。
- 安装程序不知道 Windows 也在这块 eMMC 上。上面的安装是手动写进 Windows 腾出来的分区的。
- `rtc-hym8563: no valid clock/calendar values` 是正常的；时间来自网络。

Armbian 官方给泰山派 3M 和 CM5-IO 的映像用 U-Boot 启动，不用本固件。
