# 故障排查

[English](../troubleshooting.md)（本页对应 2026-11-24 的英文版）

要报告问题，先保存一份[调试报告](firmware-menus.md#调试报告)，附在 [issue](https://github.com/gahingwoo/edk2-rk3576/issues) 里。

## 电脑上看不到 MaskROM 设备

- 泰山派 3M：上电之前就要短接 `MSK` 焊盘，并且用标着 `OTG+DP` 的 USB-C 口。
- ROCK 4D：用 USB-A 对 USB-A 线接上方的 USB 3.0 口；它的 USB-C 口没有数据线。
- CM5-IO：MaskROM 口是 USB-C 口，先拔掉上面接的其他东西。
- Windows 电脑：先用 [Zadig](https://zadig.akeo.ie/) 给设备装 WinUSB 驱动。
- 板子进入 MaskROM 后，电脑上会出现设备 `2207:350e`。

## rkdeveloptool 报错

- `Opening loader failed`：`db` 用的是 `binaries/rk3576_ddr.bin`。它需要 RKLD loader（文件开头是 `LDR `），比如 rkbin 里的 `rk3576_spl_loader_*.bin`。
- `db` 之后马上报 `Write LBA failed`：等几秒再写一次，设备还在重新枚举。
- 写完后启动的还是旧固件：写入没到 100%。在 `rd` 之前重新写。

## CM5-IO 不从 SD 卡启动

BootROM 先试 eMMC，只有 eMMC 上没有引导程序时才会用 SD 卡。泰山派 3M 的 SD 卡单独永远启动不了，eMMC 上必须有 SPL（见[刷写固件](flash.md#立创泰山派-3m)）。

## 更新后设置丢了

写入覆盖了 43648 扇区的设置区。请用网页刷机工具或 Maskrom Studio 的"保留分区"，或者[只更新固件](flash.md#只更新固件保留其他内容)里给出的 `dd` 范围。

## Linux 或 Windows 里加的启动项重启后没了

在 eMMC 上，只有打开 "Save OS Variable Changes to eMMC" 后，操作系统改的启动项才会保留（见[固件菜单](firmware-menus.md#操作系统写入的启动项)）。Reset All Settings 之后，手动加的启动项也会消失。

## 时间不对

这些板子断电后都不保留时间。插上网线（固件启动时会通过 NTP 校时），再让操作系统自己同步。

## 泰山派 3M 屏幕只亮背光

如果触摸还能用，多半是 DSI 排线松了。断电，重新插好，再上电。

## 用 Ventoy 做的 U 盘启动不了 WinPE

不要用 Ventoy 装 WinPE 映像：它的 UDF 驱动在本固件上出现了同步异常。直接把映像写进 U 盘。Ventoy 本身能启动，也成功引导过一次 Linux 磁盘映像。

## Windows 11 24H2 启动不了

RK3576 是 ARMv8.0，24H2 及以后需要 ARMv8.1。请用 23H2。

## 恢复

MaskROM 在 BootROM 里，这里的任何操作都不会改写它，所以板子总能进入 MaskROM 重新写入。
