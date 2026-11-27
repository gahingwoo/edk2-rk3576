# 一次性启动 Armbian："Armbian (U-Boot)"

[English](armbian-boot.md) | 简体中文

写给在用这个 UEFI 固件（通常还装了 Windows）、又想在同一块板子上用 Armbian 的人。开发者请看[设计说明](ARMBIAN-CHAINLOAD.zh-CN.md)。

## 它做什么

正常开机启动的是 UEFI 固件，再由它启动 Windows 或启动顺序里排第一的系统。启动管理器里多一个启动项
**Armbian (U-Boot)**。选中它，板子会重启进入 Armbian 自己的 U-Boot，再由它用 Armbian 自己的内核和设备树启动
Armbian。这一次启动不经过 UEFI 固件。

只管一次。之后的重启、断电再开机或者死机，都会回到 UEFI 固件。只有固件找到一个能启动的 Armbian U-Boot 时，这个启动项才会出现。

## 需要什么

| 板子 | 固件 | Armbian 可以放在哪 | 测试情况 |
|---|---|---|---|
| 立创泰山派 3M | eMMC 上装 [0.5.0](https://github.com/gahingwoo/edk2-rk3576/releases/tag/0.5.0) 或更新的版本 | SD 卡、eMMC 上和 Windows 并排的分区，或 U 盘/移动硬盘 | SD 卡：在一块板上测过。eMMC 分区和 USB 盘：还没测 |
| ArmSoM CM5-IO | 0.5.0 或更新 | SD 卡，固件在 eMMC 上 | SD 卡：在一块板上测过 |
| Radxa ROCK 4D | 0.5.0 或更新 | 只能是 SD 卡 | SD 卡：在一块板上测过 |

需要一个给你这块板子做的 Armbian 映像，而且要用 Armbian 的主线 U-Boot。泰山派 3M 上测的是 Armbian edge。
用 Rockchip 厂商 U-Boot 的映像（比如板厂自己的 Debian 或 Ubuntu）不支持。CM5-IO 请用 edge 映像。

泰山派 3M 上已经装了 Windows 的话，用 0.5.0 映像按
[只更新固件，保留其他内容](flash.zh-CN.md#只更新固件保留其他内容)写入固件。Windows 和设置都会保留，
同时也会写入这个启动项需要的新引导程序。

泰山派 3M 上 Armbian 放在哪：

- **SD 卡。** 用任意映像写入工具把 Armbian 映像写到卡上，开机前插好卡。固件在 eMMC 上时，
  这张卡不会再自己启动 Armbian，只有选这个启动项才会。
- **eMMC 上和 Windows 并排的分区。** 在 Maskrom Studio 的高级模式里安装 Windows 时，勾选
  "同时在 eMMC 上安装 Armbian"。不要同时在板子上插一张写了同一个 Armbian 映像的 SD 卡。
- **USB 盘**，插在 USB-A 口上（U-Boot 里 USB-C 不能用）。每次选这个启动项，固件都会把盘上
  Armbian 的 U-Boot 复制到 eMMC 上一块空闲区域。

## 怎么用

1. 开机。倒计时期间按 Esc 或 F2。用触摸屏的话，两根手指点屏幕。
2. 打开 **Boot Manager**（启动管理器）。
3. 选 **Armbian (U-Boot)**。屏幕上会说找到了什么，然后显示 "Request written. Restarting"。板子重启进入 Armbian。

在首页用 "选择语言"（Select Language）可以把菜单换成简体中文。启动项名字不变，它输出的提示仍是英文。

## 回到 UEFI 和 Windows

在 Armbian 里重启或关机。下一次启动就是 UEFI 固件。

泰山派 3M 上不能从 Windows 的重启选项里选 Armbian，因为板子在重启时会丢掉这个选择。请用启动管理器。

## 保护好固件

不要运行 `armbian-install`，也不要用 `armbian-config` 里的引导程序更新。它们可能会把 Armbian 的 U-Boot
写到 eMMC 上的固件上面。那样要恢复固件就得进 MaskROM（见[写入固件](flash.zh-CN.md)）。

## 排查问题

**启动管理器里没有这个启动项。**

- 固件比 0.5.0 旧。
- SD 卡或 USB 盘是开机之后才插的。固件每次启动只查一次。重启即可。
- 卡或盘上没有 Armbian 的 U-Boot，比如映像用的是别的 U-Boot，或者卡上只复制了 Armbian 的分区。
- Armbian 是手工装进 eMMC 分区的。固件启动时不搜索文件系统。Maskrom Studio 的安装会把 Armbian 的
  U-Boot 放到固件能找到的位置。

**选中后显示错误。** 提示里会说明原因。按任意键回到菜单。

**选中后屏幕是黑的。** 黑一阵子是正常的。泰山派 3M 上 Armbian 的 U-Boot 不使用屏幕，要等 Armbian
的内核启动它的显示驱动才有画面。几分钟后还没画面的话，接 1500000 8N1 的串口可以看到走到了哪一步；
以 `ArmbianOnce/1` 开头的行来自引导程序。断电再开机就回到 UEFI 固件。

**Armbian 启动了，但没有画面。** 走这条路时 Armbian 用它自己的设备树和设置驱动显示，固件的显示选项不起作用。
HDMI 测过。泰山派 3M 上，Armbian 第一次看到 DSI 屏时会打开它的屏幕设置然后重启。这次重启会回到 UEFI，
所以再选一次 Armbian (U-Boot)。DSI 屏走这条路还没测过。

**我想默认启动 Armbian。** 不支持。这个启动项从不进入正常的启动顺序，所以死机或断电后总能回到可用的 UEFI。
如果只想用 Armbian，就不要装这个固件。
