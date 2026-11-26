# 固件菜单和功能

[English](firmware-menus.md) | 简体中文

没有特别说明的内容三块板都适用。串口：1500000 8N1。

| 按键 | 什么时候 | 作用 |
|---|---|---|
| F4 | 启动倒计时 | 重启进入 MaskROM |
| F9 | 启动倒计时 | 恢复全部设置 |
| Ctrl+Alt+F12（串口上是 F12） | 任何时候 | 截图 |
| Ctrl+Alt+F11（串口上是 F11） | 任何时候 | 调试报告 |

## 语言

可以在首页切换成简体中文。中文也会以 UTF-8 输出到串口。

## 截图

按 Ctrl+Alt+F12、串口上按 F12，或点屏幕键盘上的相机键。图片存为
`\uefi-screenshots\YYYYMMDD-HHMMSS.png`（时钟没设好时为 `shot-NNNN.png`），写到 FAT 格式的介质上。
平台设置里的 "Save Screenshots To" 选择位置：Automatic（先 U 盘，再 SD 卡，再 ESP）或只用其中一个。
左上角会闪一个方块：绿色表示已保存，黄色表示没地方写，红色表示失败。串口日志里有路径。

## 调试报告

Device Manager > Debug Report，或按 Ctrl+Alt+F11。它把 `\uefi-debug\YYYYMMDD-HHMMSS\`
写到和截图相同的位置：本次启动的固件日志、摘要（固件、板子、启动项、显示、PCI、USB、存储、内存映射）、
ACPI 表、设备树，从 Device Manager 进入时还有每个设置页的截图（运行期间不要碰任何东西）。
把这个文件夹附到 [issue](https://github.com/gahingwoo/edk2-rk3576/issues) 里。

## 恢复全部设置

在首页选 "Reset All Settings"，或在启动倒计时按 F9。它清空整个 UEFI 变量存储（设置项、语言、启动项和顺序、
超时、Secure Boot 密钥），不碰别的东西。在警告里选 Yes，板子会重启，像第一次启动一样。
自己手动加的启动项要重新加，比如在 UEFI Shell 里用 `bcfg boot add`。

## 时钟

断电后没有一块板子能保持时间。插着网线时，固件每次启动都用 NTP 校时（UTC，"Network Time Sync"，默认打开）。
它依次尝试 DHCP 服务器给的 NTP 服务器、pool.ntp.org 和 time.cloudflare.com，最后是固定的 Cloudflare 和
Google 地址。它在后台运行，不会拖慢启动；拿到地址后 10 秒放弃（没拿到地址时 30 秒）。
结果看串口日志里的 `NetworkTimeSync:` 和 `NetworkTimeSync: stage:` 两行。
"关于"页面每次打开时显示当前日期和时间，并注明"已通过网络校时"、"来自 RTC"或"未设置"。

## 操作系统写入的启动项

在 SPI NOR 上（ROCK 4D），`efibootmgr` 或 `bcdboot` 写的启动项会立即保存。在 eMMC 上（CM5-IO、泰山派 3M），
除了 BootNext，这些改动在下次复位时丢失，除非打开 "Save OS Variable Changes to eMMC"（实验性，默认关）；
打开后固件会在操作系统重启或关机时写入 eMMC。只在泰山派 3M 上测过。

## 显示

- 显示设置页："Display Mode"、"Output Priority"、"Force Output"、"Duplicate Output"、"HDMI Signaling"。
- "Screen Rotation" 旋转菜单、UEFI Shell 和触摸测试，不影响操作系统。
- "Screen Brightness (%)"：泰山派 3M 的 DSI 背光，10% 到 100%。
- "Force UEFI GOP Display"（ACPI / Device Tree 页）：Linux 沿用固件的画面，还是自己驱动显示（见 [Linux](linux.zh-CN.md)）。

## 屏幕键盘、鼠标和触摸

- 泰山派 3M 的 DSI 屏：右上角附近的键盘图标打开屏幕键盘，Hide 关闭。可在设置、UEFI Shell、GRUB 和
  Windows 启动管理器里使用。
- 任何板子插上 USB 鼠标：出现键盘和光标；滚轮是上下，左键是回车，在键盘外右键是 Esc。还没有在板子上跑过。
- 触摸（泰山派 3M）：拖动移动光标，点一下是回车，两指或长按是 Esc。启动管理器里的 "Touch Test" 显示触点。

## 关机

首页的 "Shut Down"、UEFI Shell 的 `reset -s`，或在 Windows 里关机。在泰山派 3M 和 ROCK 4D 上，固件通过 I2C1
关掉 RK806 电源管理芯片，并为 Windows 发出的 PSCI 调用做好准备。

## 给操作系统用的选项

在 ACPI / Device Tree 页和平台设置页，下次启动生效。哪块板子有哪个选项、默认值是什么，见[设置项参考](settings.zh-CN.md)。

| 选项 | 作用 |
|---|---|
| Config Table Mode | ACPI + 设备树、只用 ACPI，或只用设备树 |
| CPU Maximum Frequency | 两个簇都 1608 MHz，或 Max（2208 / 2016 MHz）。第一次带屏幕启动时固件会问一次（10 秒超时，记在变量 `CpuMaxFreqAsked` 里），答案改变设置时会重启 |
| GPU voltage（GPU 电压） | 775 mV（702 MHz）：在操作系统启动前把 GPU 供电（RK806 BUCK5）从 750 mV 升到 775 mV，Windows GPU 驱动就能跑 702 MHz 而不是 594 MHz。只用于普通 RK3576 且已启用过热关机。750 mV（594 MHz）：把升过的电压恢复到 750 mV。要开着风扇 |
| CPU Idle States | 关闭：只有 WFI。打开后增加核心掉电（PSCI CPU_SUSPEND），Windows 11 23H2 开机会蓝屏 0x5C（HAL_INITIALIZATION_FAILED，0x113）：用 Windows 时请保持关闭 |
| Fan Mode（风扇模式） | 常开：上电即全速，不会停；性能 / 均衡 / 静音：Windows 按大核温度开关风扇，50 / 60 / 70 C 开，低 10 C 关；关闭：风扇保持 0%，只适合被动散热足够的板子（115 C 的 _CRT 和 120 C 的 TSHUT 仍然有效） |
| NPU (experimental) | 给 NPU 上电并在 ACPI 里声明，供 [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) 使用 |
| 40-pin UART (experimental) | 把排针上的 UART 作为 COM 口，给 woa-rk3576 的 rk3xuart 驱动用（不包括调试串口） |
| 40-pin GPIO/I2C/SPI for apps | 排针上的 GPIO、I2C、SPI 给 Windows.Devices.Gpio、.I2c、.Spi 用（ACPI RhProxy） |
| Save OS Variable Changes to eMMC | 见[上文](#操作系统写入的启动项) |
