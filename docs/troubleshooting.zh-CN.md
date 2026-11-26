# 故障排查

[English](troubleshooting.md) | 简体中文

报告问题前，先保存一份[调试报告](firmware-menus.zh-CN.md#调试报告)，附到 [issue](https://github.com/gahingwoo/edk2-rk3576/issues) 里。

| 问题 | 解决办法 |
|---|---|
| 看不到 MaskROM 设备（`2207:350e`） | 泰山派 3M：上电前短接 `MSK` 焊盘，用标着 `OTG+DP` 的 USB-C 口。ROCK 4D：用 USB-A 对 USB-A 线接上方的 USB 3.0 口（USB-C 没有数据线）。CM5-IO：用 USB-C 口，不要接别的东西。在 Windows 上用 [Zadig](https://zadig.akeo.ie/) 装 WinUSB 驱动。 |
| `Opening loader failed` | `db` 用了 `binaries/rk3576_ddr.bin`。要用 RKLD loader（开头是 `LDR `），比如 rkbin 的 `rk3576_spl_loader_*.bin`。 |
| `db` 之后马上报 `Write LBA failed` | 等几秒再写一次。 |
| 写完后启动的还是旧固件 | 写入没到 100%。`rd` 之前重新写。 |
| CM5-IO 不从 SD 卡启动 | BootROM 先试 eMMC；只有 eMMC 上没有引导程序时才用 SD 卡。泰山派 3M 单独一张 SD 卡永远启动不了（见[刷写固件](flash.zh-CN.md#立创泰山派-3m)）。 |
| 更新后设置丢了 | 写入覆盖了存放设置的 <!-- generated:nvdata-sector -->43648<!-- /generated --> 扇区。用"保留分区"或[刷写固件](flash.zh-CN.md#只更新固件保留其他内容)里的 `dd` 范围。 |
| Linux 或 Windows 里加的启动项重启后没了 | eMMC 上要打开 "Save OS Variable Changes to eMMC"（见[固件菜单](firmware-menus.zh-CN.md#操作系统写入的启动项)）。恢复全部设置也会删掉手动加的启动项。 |
| 时间不对 | 断电后没有一块板子能保持时间。插网线（启动时 NTP 校时），再让操作系统同步。 |
| 泰山派 3M 屏幕只亮背光，触摸能用 | DSI 排线松了。断电后重新插好。 |
| Ventoy 做的 U 盘启动不了 WinPE | Ventoy 的 UDF 驱动在本固件上会触发同步异常。直接写入 WinPE 映像。 |
| Windows 11 24H2 启动不了 | RK3576 是 ARMv8.0，24H2 需要 ARMv8.1。用 23H2。 |
| Windows 启动时蓝屏 0x5C | 在设置里把 "CPU Idle States" 设为 Disabled（默认值）。 |
| ROCK 4D 的 Windows：以太网有链路，拿不到 DHCP 地址 | 把固件更新到 2026-11-26 或之后的 `ci` 构建（它会设置 RTL8211F 的 RGMII 延时）。 |

## 恢复

MaskROM 在 BootROM 里，这里没有任何东西会改写它，所以板子总能重新进入 MaskROM 再写一次。
