# 固件菜单和功能

[English](firmware-menus.md) | 简体中文

操作系统启动之前，固件本身提供的功能。除非注明板子，三块板都适用。

串口：1500000 8N1。启动倒计时里的按键：F4 重启进入 MaskROM，F9 开始 Reset All Settings。

## 语言

在首页可以把设置菜单切换成简体中文。从 0.4.1 起，中文也会以 UTF-8 输出到串口。

## 截图

在键盘上按 Ctrl+Alt+F12，在串口终端按 F12，或者点屏幕键盘上的相机键。截图保存为
`\uefi-screenshots\YYYYMMDD-HHMMSS.png`（时钟没设置时是 `shot-NNNN.png`），写到 FAT 格式的介质上。

平台设置里的 "Save Screenshots To" 决定写到哪里：Automatic（先 U 盘，再 SD 卡，再 ESP），或只写其中一种。
保存成功时左上角闪绿色方块，没地方写时闪黄色，失败时闪红色。串口日志里有文件路径。

## 调试报告

要报告问题，先保存一份调试报告：Device Manager > Debug Report，或者按 Ctrl+Alt+F11（串口终端按 F11）。
它写到截图所在的同一个地方，目录为 `\uefi-debug\YYYYMMDD-HHMMSS\`：

- 这次启动的固件日志；
- 摘要：固件、板子、启动项、显示、PCI、USB 和存储设备、内存表；
- ACPI 表和设备树；
- 从 Device Manager 进入时，还有每个设置页面的截图。遍历页面期间左上角会一直显示
  "Collecting debug report, please do not touch anything..."。

把这个目录附在 [issue](https://github.com/gahingwoo/edk2-rk3576/issues) 里。

## Reset All Settings（恢复全部设置）

首页的 "Reset All Settings"，或启动倒计时按 F9（串口终端也可以），会让固件回到刚写入时的状态。
它清空整个 UEFI 变量区：所有设置、语言、启动项和启动顺序、超时时间以及安全启动密钥。变量区以外的东西都不动。

1. 选择它。会先弹出红色警告，默认选中 No。
2. 选 Yes。板子重启，清空变量区，像第一次启动一样初始化。
3. 手动加过的启动项（操作系统、第二个引导程序）需要重新添加，比如在 UEFI Shell 里用 `bcfg boot add`。

## 时钟

三块板的固件在断电后都不保留时间。插着网线时，固件每次启动会通过 NTP 校时
（平台设置里的 "Network Time Sync"，默认开）：

- 依次询问 DHCP 服务器给的 NTP 服务器、pool.ntp.org 和 time.cloudflare.com，最后是 Cloudflare 和 Google 的固定地址；
- 在启动菜单倒计时期间后台运行，不会拖慢启动；
- 拿到地址后 10 秒没回应就放弃（拿不到地址时 30 秒）；
- 时钟按 UTC 保存。

串口日志里的 `NetworkTimeSync:` 行给出校时前后的时间，`NetworkTimeSync: stage:`
行说明进行到哪一步（没有链路、没有租约、DNS 超时、NTP 超时，或已设置）。

## 操作系统写入的启动项

在 SPI NOR 上（ROCK 4D），操作系统写入的启动项（`efibootmgr`、`bcdboot`）会马上写进闪存。

在 eMMC 上（CM5-IO、泰山派 3M），这些启动项只保存在内存里，下次复位就没了（BootNext 除外），
除非打开 "Save OS Variable Changes to eMMC"（默认关，标为实验性）。打开后，操作系统重启或关机时，
固件会把它们写进 eMMC。2026-11-22 在泰山派 3M 上用 `efibootmgr` 测过 3 次。

## 显示

在显示设置页：

- "Display Mode"、"Output Priority"、"Force Output"、"Duplicate Output"、"HDMI Signaling" 用来选分辨率和输出口。
- "Screen Rotation" 旋转菜单、UEFI Shell 和触摸测试。操作系统拿到的是屏幕原本的方向，自己负责旋转。
- "Screen Brightness (%)" 设置泰山派 3M DSI 屏的背光，10% 到 100%。

ACPI / Device Tree 页的 "Force UEFI GOP Display" 决定 Linux 沿用固件的画面，还是自己驱动显示（见 [Linux](linux.zh-CN.md)）。

## 屏幕键盘和鼠标

在泰山派 3M 的 DSI 屏上，右上角附近的小键盘图标会打开屏幕键盘，点上面的 Hide 关闭。
它的按键可以用在设置菜单、UEFI Shell、GRUB 和 Windows 启动管理器里。

插上 USB 鼠标后，每块板都会出现同样的键盘和鼠标指针：滚轮是上下，左键是回车，在键盘以外的地方点右键是 Esc。
鼠标部分截至 2026-11-24 还没有在板子上跑过。

## 触摸（泰山派 3M）

菜单里的触摸相当于按键：拖动移动光标，点一下是回车，两指或长按是 Esc。启动管理器里的 "Touch Test" 会显示触点位置。

## 关机

首页的 "Shut Down"、UEFI Shell 里的 `reset -s`，以及在 Windows 里关机，都会让板子断电。在泰山派 3M
（以及分支 `rock4d-parity` 上的 ROCK 4D，未在其上运行过）上，固件自己通过 I2C1 关掉 RK806 电源芯片，并为 Windows 发出的 PSCI 调用做好准备。

## 给操作系统用的选项

在 ACPI / Device Tree 页和 CPU 页。都在下次启动时生效。除了 Config Table Mode、CPU Maximum Frequency 和泰山派 3M 的 GPU 电压，其余默认都是关。

| 选项 | 作用 |
|---|---|
| Config Table Mode | ACPI + 设备树（CM5-IO、泰山派 3M 以及分支 `rock4d-parity` 上 ROCK 4D 的默认值）、只用 ACPI，或只用设备树 |
| CPU Maximum Frequency | 两组核心都是 1608 MHz（默认），或 Max（2208 / 2016 MHz）。第一次接着屏幕开机时固件会问一次用哪个（英文界面；10 秒不选就保持当前设置，没在设置里选过 Max 就是 1608 MHz；上下键或拖动移动，回车或轻点选择），答案改变了设置就重启一次；问过的记录在变量 `CpuMaxFreqAsked` 里，没有屏幕时不问。泰山派 3M，以及分支 `rock4d-parity` 上的 ROCK 4D |
| GPU voltage for 702 MHz (775 mV) | 开：进系统之前固件把 GPU 供电（RK806 BUCK5）从 750 mV 以 6.25 mV 一步升到 775 mV，每步读回，只在普通 RK3576 且过热关机已启用时进行，不会更高；Windows GPU 驱动随后运行 702 MHz 而不是 594 MHz。关：750 mV，之前某次启动升过的电压会被降回。会更热，请保持风扇运转。泰山派 3M 默认打开；只在提供这个选项的板子上显示 |
| CPU Idle States | 把核心的空闲状态告诉操作系统；关闭时只用 WFI |
| Fan | 开（默认）：板载风扇从上电起全速运行，进系统后也一直开着；关：风扇保持停止。没有调速：部分转速要用 PWM 脉冲，脉冲会让风扇啸叫。泰山派 3M 和 CM5-IO 有这个选项，分支 `rock4d-parity` 上的 ROCK 4D 也有（J16）。如果打开了 Fan Control by ACPI Thermal Zone，操作系统仍然可以把风扇打开 |
| Fan Control by ACPI Thermal Zone | 让 Windows 按大核温度控制风扇；关闭时风扇保持固件设定的转速 |
| NPU (experimental) | 给 NPU 上电并在 ACPI 里声明，供 [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) 使用；泰山派 3M，以及分支 `rock4d-parity` 上的 ROCK 4D |
| 40-pin UART (experimental) | 把排针上的 UART 引脚切到 UART，并在 ACPI 里把这些 UART 声明为 COM 口，供 woa-rk3576 的 rk3xuart 驱动使用；调试串口永远不在其中。泰山派 3M；CM5-IO 和 ROCK 4D 在各自的板子分支上 |
| 40-pin GPIO/I2C/SPI for apps (experimental) | 把排针上的 I2C 和 SPI 引脚切到这些总线，并声明 ACPI RhProxy 设备，让 Windows 应用通过 Windows.Devices.Gpio、.I2c、.Spi 使用排针上的 GPIO、I2C 和 SPI。板子同上 |
| Save OS Variable Changes to eMMC | 见上文 |
