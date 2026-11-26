# 测试

[English](testing.md) | 简体中文

## CI 检查什么

`.github/workflows/build.yml` 在每次 push 到 `main` 和每个 pull request 上构建所有板子，并检查每个 SD 和 eMMC
映像里的变量存储是否在 `package.sh` 预期的位置。它不在硬件上运行任何东西。`main` 构建成功后会替换 `ci` 预发布版。

## 在板子上

每种板子只有一块。结果写在合并提交里（"Board-verified on the TaishanPi 3M on <date>: ..."）和正式版说明里
（"How it was tested"）。经常把几个分支合在一个 `test-all-<date>` 映像里一起在泰山派 3M 上跑，合并信息里会写明是哪个映像。

## 最近记录的板上运行

| 板子 | 最近一次 | 说明 |
|---|---|---|
| 泰山派 3M | 到 2026-11-24 为止的 `main` | 从 eMMC 启动 Windows 11 23H2 和 Armbian |
| CM5-IO | 固件 0.4.x | 用 0.4.x 启动过操作系统 |
| ROCK 4D | 2026-11-26 的 `main` | 从 NVMe 启动 Windows 11 23H2（用 Maskrom Studio 安装），12 GB 板。Fedora 最后一次是在 0.2.1 之后跑的；0.2.1 的 SPL 由一位用户在 v1.12 板子（XMC 闪存）上跑过 |

## 只在泰山派 3M 上跑过

- 网络校时、RTC 读取、CPU 空闲状态、CPU 频率选项、120 C 过温关机；
- 在 ResetSystem 时把操作系统对变量的修改写到 eMMC（`efibootmgr -n` 和 `-o`）；
- 启动时接着受电设备时打开 USB-C VBUS，接充电器时关闭；
- GPU 供电 775 mV，Windows 驱动跑在 702 MHz；
- Windows 下的蓝牙。

2026-11-14 未经上板检查就合并的改动清单在[历史记录](../history/2026-11-22-hardware-before-rewrite.md)里。
