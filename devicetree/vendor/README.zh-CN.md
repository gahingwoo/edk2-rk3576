# 厂商 BSP 设备树

[English](README.md) | 简体中文

来自厂商内核的预编译 DTB，作为 `devicetree/mainline/` 构建出的主线设备树之外的另一种选择。

| 文件 | 板子 | 使用者 |
|---|---|---|
| `rk3576-rock-4d.dtb` | Radxa ROCK 4D | `Platform/Radxa/ROCK4D/DeviceTree/Vendor.inf` |

ROCK 4D 的 DTB（`compatible = "radxa,rock-4d", "rockchip,rk3576"`）是在 2026-08-04
的重构中加入这个仓库的。当时没有记录它是用哪个厂商内核、哪个提交构建的。

CM5-IO 也有一个 `Vendor.inf`，但它在板子的 DSC 和 FDF 里被注释掉了，它的 DTB
也不在这个目录里。添加方法写在该 INF 文件的开头注释里。

## 许可证

SPDX-License-Identifier: GPL-2.0-only
