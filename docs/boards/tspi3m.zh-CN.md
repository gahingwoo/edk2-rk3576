# 立创泰山派 3M

[English](tspi3m.md) | 简体中文

这个项目大部分工作都在这块板子上做：eMMC 上跑 Windows 11 23H2 和 Armbian，
接立创的 DSI 屏和触摸，也接 HDMI。下面的结果都来自同一块板子。板子由嘉立创（JLC）赞助。

## 能用什么（2026-11-24）

| | 固件里 | Windows 11 23H2 | Linux（Armbian edge） |
|---|---|---|---|
| HDMI | 可用，分辨率取自显示器 EDID | 可用；配合 [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) 支持热插拔和 HDMI 音频 | 关掉"Force UEFI GOP Display"时未试过 |
| DSI 屏（立创 800x1280） | 可用，带触摸 | 可用；配合 wddm-mali-g52 和 gt9271 可横屏、可触摸 | 可用（Armbian 的屏驱动） |
| eMMC | 可用，设置能保存 | 可用，作为系统盘 | 可用 |
| SD 卡 | 可用 | 驱动能启动；没插卡试过 | 未试过 |
| USB-A（4 个） | 可用 | 可用 | 可用 |
| USB-C | 上电前插好的设备才有供电 | 上电前插好的设备可用 | 试过的硬盘不能用 |
| 以太网 | 可用 | 可用 | 可用 |
| 耳机孔 | 不用 | 播放和麦克风都可用 | ALSA 可以播放；桌面声音还没调通 |
| 蓝牙 | 不用 | 可用：配对、传文件；不能通话 | 未试过 |
| WiFi | 不用 | 不能用 | 未试过 |
| GPU | 为系统上电 | 配合 wddm-mali-g52 可用，最高 702 MHz | 固件设备树里关掉了 |
| NPU | 打开设置项时上电 | 配合 [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) 可用 | 固件设备树里关掉了 |
| 风扇 | 100% 转速 | 100%，或者打开设置项后按温度调速 | 保持 100% |

0.4.1 正式版（2026-11-17）已经支持 DSI 屏、菜单里的触摸、中文菜单和 Windows。
Windows 下的触摸、蓝牙、音频和 USB-C 要用 [`ci` 构建](../flash.zh-CN.md#滚动的-ci-构建)，等下一个正式版发布。

## 开始之前

- 串口（可选）：1500000 8N1。
- MaskROM：板子上没有按键。连好标着 `OTG+DP` 的 USB-C 口到电脑，短接 `RK3576`
  丝印正下方标着 `MSK` 的两个焊盘，再上电。已经在跑本固件的板子，也可以在启动倒计时按 F4 进入 MaskROM。
- BootROM 只从 eMMC 启动，单独一张 SD 卡启动不了。

## 安装固件

1. 把 `TaishanPi3M-emmc` 映像作为完整映像写到 eMMC
   （[刷写固件](../flash.zh-CN.md#立创泰山派-3m)）。这会替换立创的出厂系统。
2. 以后更新时选"保留分区"
   （[只更新固件](../flash.zh-CN.md#只更新固件保留其他内容)），Windows、Linux 和设置都会保留。

接下来：

- Windows：看 [woa-rk3576 的安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)，
  或者用 [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio) 不用 U 盘直接装。概览见 [Windows](../windows.zh-CN.md)。
- Linux：见 [Linux](../linux.zh-CN.md)。

## DSI 屏和触摸

- 固件在 I2C0 上找触摸芯片：找到就用屏幕，找不到就用 HDMI。
- 排线只能在断电时插拔。
- 菜单旋转成横屏显示。平台设置里的 "Screen Rotation" 可以改。操作系统拿到的是竖屏，自己负责旋转。
- 菜单里的触摸：拖动移动光标，点一下是回车，两指或长按是 Esc。启动管理器里的 "Touch Test"
  会显示触点位置。
- 右上角附近的小键盘图标会打开屏幕键盘。
- 显示设置页的 "Screen Brightness (%)" 可以把背光设为 10% 到 100%。

如果屏幕只亮背光、触摸却还能用，先断电重新插好排线，再排查别的。

## USB-C

只有上电时口上接着需要供电的设备（U 盘、硬盘、Hub），固件才会打开这个口的 5 V。之后没人管这个口：

- USB 设备要在上电前插好；
- 开机后再插的设备要等下次启动才有电；
- 要在这个口接充电器或电脑，先重启。

## 时钟

这块板子的 RTC 每次断电和复位都会丢时间。插着网线时，固件每次启动会通过 NTP 校时
（见[固件菜单](../firmware-menus.zh-CN.md#时钟)）；没插网线时，时间会一直不对，直到操作系统自己同步。

## 风扇

固件让 CN2 上的风扇全速运转并保持不变。Windows 下，在设置里打开
"Fan Control by ACPI Thermal Zone"（默认关）后按温度调速。Linux 下保持全速。

## 按键

板子上有两个按键：RECOVERY 和 RESET。电源管理芯片的开机（PWRON）线上没有焊按键，
所以没有真正的电源键，接上电源就开机。

| 按键 | 进入操作系统之前 | Windows 下（ACPI） | Linux 下（设备树） |
|---|---|---|---|
| RECOVERY | 无作用（固件不读它） | 电源按钮：短按执行 Windows 电源选项里"按电源按钮时"设定的动作（关机、睡眠等） | 按设备树，`KEY_VENDOR` |
| RESET | 复位开发板 | 复位开发板 | 复位开发板 |

RECOVERY 作为电源按钮，需要安装 woa-rk3576 的 rk3xsaradc 驱动，并打开设置项
"ADC keys for the OS"（这块板子上默认打开）。开机后要先看到按键松开，按键才生效，
所以开机时按住 RECOVERY 不会有任何动作。如果 Windows 在这个 ADC 设备上出了问题，
关掉这个设置项即可：固件就完全不描述、也不碰 ADC。这块板子上没有
"Power button (RK806)" 设置项，因为那条线上没有按键。

## 硬件细节

PHY、GPIO、I2C 地址、GPU 供电等：见[硬件](../dev/hardware.zh-CN.md#立创泰山派-3m)。
