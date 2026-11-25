# 立创泰山派 3M

[English](tspi3m.md) | 简体中文

eMMC 上跑 Windows 11 23H2 和 Armbian，接立创的 DSI 触摸屏，也接 HDMI。结果都来自同一块板子，由嘉立创（JLC）赞助。

## 能用什么

用截至 2026-11-24 的 `main` 构建在板子上测过。Windows 下的触摸、蓝牙、音频和 USB-C 需要
[`ci` 构建](../flash.zh-CN.md#滚动的-ci-构建)；0.4.1 正式版已经有 DSI 屏、菜单里的触摸、中文菜单和 Windows。

| | 固件里 | Windows 11 23H2 | Linux（Armbian edge） |
|---|---|---|---|
| HDMI | 可用，分辨率取自 EDID | 可用；配合 [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) 支持热插拔和 HDMI 音频 | 关掉"Force UEFI GOP Display"时未试过 |
| DSI 屏（立创 800x1280） | 可用，带触摸 | 配合 wddm-mali-g52 和 gt9271 可横屏、可触摸 | 可用（Armbian 的屏驱动） |
| eMMC | 可用，设置能保存 | 可用，作为系统盘 | 可用 |
| SD 卡 | 可用 | 驱动能启动；没插卡试过 | 未试过 |
| USB-A（4 个） | 可用 | 可用 | 可用 |
| USB-C | 只给上电时插着的设备供 VBUS | 同左 | 硬盘没能枚举 |
| 以太网 | 可用 | 可用 | 可用 |
| 耳机孔 | 不用 | 播放和麦克风 | ALSA 可以播放；桌面声音还没调通 |
| 蓝牙 | 不用 | 配对、传文件；不能通话 | 未试过 |
| WiFi | 不用 | 不能用 | 未试过 |
| GPU | 为系统上电 | wddm-mali-g52，最高 702 MHz | 固件设备树里关掉了 |
| NPU | 打开设置项时上电 | [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | 固件设备树里关掉了 |
| 风扇 | 100% | 100%，或打开设置项后按温度调速 | 100% |

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：没有按键。把标着 `OTG+DP` 的 USB-C 口接到电脑，短接 `RK3576` 丝印正下方标着 `MSK`
  的两个焊盘再上电。已经在跑本固件的板子，也可以在启动倒计时按 F4 进入 MaskROM。
- BootROM 只从 eMMC 启动，单独一张 SD 卡启动不了。

## 安装固件

1. 把 `TaishanPi3M-emmc` 映像作为完整映像写入（见[刷写固件](../flash.zh-CN.md#立创泰山派-3m)）。
   这会替换立创的出厂系统。
2. 以后更新时选"保留分区"（见[刷写固件](../flash.zh-CN.md#只更新固件保留其他内容)），Windows、Linux 和设置都会保留。

然后装 [Windows](../windows.zh-CN.md)（或用 [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)，不用 U 盘）
和 [Linux](../linux.zh-CN.md)。

## 板子说明

- DSI 屏：I2C0 上的触摸芯片有应答时固件用屏幕，否则用 HDMI。排线只能在断电时插拔。菜单以横屏显示
  （"Screen Rotation" 可以改），操作系统拿到的是竖屏。如果只亮背光而触摸还能用，先重新插好排线。
- USB-C：只有上电时接着需要供电的设备（U 盘、硬盘、Hub），固件才打开 5 V，之后没人管这个口。
  设备要在上电前插好；接充电器或电脑前先重启。
- 时钟：RTC 每次断电和复位都丢时间。插着网线时固件用 NTP 校时（见[固件菜单](../firmware-menus.zh-CN.md#时钟)）。
- 风扇（CN2）：上电即全速。Windows 下打开 "Fan Control by ACPI Thermal Zone"（默认关）后按温度调速；Linux 下保持全速。

## 按键

没有电源键（电源管理芯片的 PWRON 线上没有按键），接上电源就开机。

| 按键 | 进入操作系统之前 | Windows 下（ACPI） | Linux 下（设备树） |
|---|---|---|---|
| RECOVERY | 无作用 | 电源按钮：按 Windows 电源选项里的设定执行 | `KEY_VENDOR` |
| RESET | 复位开发板 | 复位开发板 | 复位开发板 |

RECOVERY 作为电源按钮，需要 woa-rk3576 的 rk3xsaradc 驱动和设置项 "ADC keys for the OS"（这块板子默认打开）。
开机时按住它没有作用：按键要先被看到松开才生效。如果 Windows 在 ADC 设备上出问题，关掉这个设置项，固件就不再碰 ADC。

## 硬件细节

见[硬件](../dev/hardware.zh-CN.md#立创泰山派-3m)。
