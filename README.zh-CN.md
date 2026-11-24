# edk2-rk3576

[English](README.md) | 简体中文

Rockchip RK3576 开发板的 UEFI 固件（EDK2 / TianoCore），支持 Radxa ROCK 4D、ArmSoM CM5-IO
和立创泰山派 3M。它能像 PC 一样启动 Linux，在 CM5-IO 和泰山派 3M 上还能启动 Windows 11 23H2。

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> 操作系统
```

下面列出的所有 RK3576 项目都从这个仓库开始。

| 泰山派 3M 上的 Windows 11 | CM5-IO 上的 Fedora |
|---|---|
| ![泰山派 3M 上的 Windows 11 23H2](docs/imgs/tspi3m-win11-desktop.jpg) | ![CM5-IO 上的 Fedora GNOME](docs/imgs/cm5io-desktop.png) |

更多图片：[图片](docs/gallery.md)。

## 从一块新板子到 Windows 或 Linux

1. 先看你的板子：[Radxa ROCK 4D](docs/boards/rock4d.zh-CN.md)、
   [ArmSoM CM5-IO](docs/boards/cm5io.zh-CN.md)、[立创泰山派 3M](docs/boards/tspi3m.zh-CN.md)。
2. 写入固件（见[刷写固件](docs/flash.zh-CN.md)），三种方法任选：
   - [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)，桌面程序；
   - [flash.gahingwoo.com](https://flash.gahingwoo.com/)，用 Chrome 或 Edge 打开；
   - `rkdeveloptool`。
3. 装 Windows 11 23H2（CM5-IO、泰山派 3M）：按
   [woa-rk3576 的安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)操作，
   或者用 Maskrom Studio 在电脑上直接装，不用 U 盘。概览见 [Windows](docs/windows.zh-CN.md)。
4. 在 Windows 下按需加装驱动：GPU 和显示驱动
   [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52)，NPU 驱动
   [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu)。
5. 装 Linux：见 [Linux](docs/linux.zh-CN.md)。
6. 遇到问题：[故障排查](docs/troubleshooting.zh-CN.md)、[已知问题](docs/known-issues.zh-CN.md)。

映像：[最新正式版](https://github.com/gahingwoo/edk2-rk3576/releases/latest)（0.4.1，2026-11-17），
或 `main` 的 [`ci` 构建](docs/flash.zh-CN.md#滚动的-ci-构建)，包含之后合并的全部改动。

## 板子

2026-11-24 的状态。"ci" 表示要用 `main` 的 `ci` 构建，0.4.1 正式版里没有。

| | Radxa ROCK 4D | ArmSoM CM5-IO | 立创泰山派 3M |
|---|---|---|---|
| 固件放在 | SPI NOR | eMMC 或 SD 卡 | eMMC（或 SPL 在 eMMC、其余在 SD 卡） |
| HDMI | 可用 | 可用 | 可用 |
| DSI 屏和触摸 | 未试过 | 未试过 | 可用（立创 800x1280） |
| eMMC | 没有 eMMC（UFS，未试过） | 可用 | 可用 |
| NVMe | 可用 | 可用 | 没有 M.2 插槽 |
| USB-A | 可用 | 可用 | 可用 |
| USB-C | USB 2.0 | USB 2.0 | 上电时插着的设备可用（ci） |
| 以太网 | 可用 | 可用 | 可用 |
| 通过 UEFI 启动 Linux | 从 NVMe 启动 Fedora | 从 NVMe 启动 Fedora | 从 eMMC 启动 Armbian edge，[有限制](docs/linux.zh-CN.md#立创泰山派-3m) |
| Windows 11 23H2 | 不支持（没有 ACPI） | 能进桌面 | 能进桌面 |
| Windows：GPU 绘制桌面 | | 未试过 | 配合 wddm-mali-g52 可用 |
| Windows：音频 | | 未试过 | 耳机、麦克风、HDMI（ci） |
| Windows：蓝牙、WiFi | | 不能用 | 蓝牙可用（ci），WiFi 不能用 |
| Windows：NPU | | 不支持 | 配合 woa-rk3576-npu 可用（ci） |

每块板的结果都来自同一块实物。每块板最后一次测试的时间和版本见[测试](docs/dev/testing.md)（英文）。串口：1500000 8N1。

固件本身的功能：简体中文菜单、截图、调试报告、恢复全部设置、网络校时、屏幕键盘。见[固件菜单](docs/firmware-menus.zh-CN.md)。

## Windows

Windows 11 23H2 可以从 CM5-IO 和泰山派 3M 的 eMMC 启动。Windows 11 24H2 及以后的版本跑不了：RK3576 是
ARMv8.0。Windows 安装程序在这些板子上用不了，所以要用 WinPE U 盘里的 DISM 写入系统映像，或者用 Maskrom Studio。
步骤见 [woa-rk3576 的安装说明](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.zh-CN.md)。
各项功能需要哪个固件：见 [Windows](docs/windows.zh-CN.md)。

### 泰山派 3M 上的 Linux

Armbian edge 和 Windows 一起装在 eMMC 上，经 GRUB 用固件的设备树启动：DSI 屏、触摸、耳机、背光、USB-A、
以太网和 EC20 模块都能用。目前 GPU 是关的。详情和限制见 [Linux](docs/linux.zh-CN.md#立创泰山派-3m)。

## 已知问题

主要的几条，完整列表见[已知问题](docs/known-issues.zh-CN.md)：

- Windows 11 24H2 及以后的版本跑不了（ARMv8.0）。
- 泰山派 3M 的 USB-C 口只给上电时就插着的设备供电。
- ROCK 4D 和 CM5-IO 的 USB-C 在 Linux 以外只有 USB 2.0。
- 泰山派 3M 的 RTC 每次断电都丢时间；时间要靠 NTP 或操作系统。

## 相关项目

| 仓库 | 内容 |
|---|---|
| [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) | Windows 驱动和 WinPE 安装映像 |
| [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) | Mali-G52 的 Windows GPU 和显示驱动 |
| [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | Windows NPU 驱动和 charsiu 大模型运行时 |
| [maskrom-studio](https://github.com/gahingwoo/maskrom-studio) | 桌面程序：写固件、不用 U 盘装 Windows、恢复 Armbian 或出厂系统 |
| [edk2-webflash](https://github.com/gahingwoo/edk2-webflash) | flash.gahingwoo.com 网页刷机工具 |

## 编译

```bash
scripts/setup-host.sh
scripts/build.sh   cm5io      # 或 rock4d、tspi3m
scripts/package.sh cm5io      # 输出到 out/CM5IO/
```

开发文档（英文）：[编译](docs/dev/building.md)、[架构](docs/dev/architecture.md)、[映像布局](docs/dev/image-layout.md)、
[硬件](docs/dev/hardware.md)、[ACPI](docs/dev/acpi.md)、[测试](docs/dev/testing.md)、[发布](docs/dev/releasing.md)、
[核心补丁](patches/README.md)、[历史记录](docs/history/README.md)。

## 致谢

感谢[嘉立创](https://jlc.com/)（JLC）赞助本项目使用的立创泰山派 3M、MIPI DSI 屏、风扇和 EC20 4G 模块，
感谢 [ArmSoM](https://www.armsom.org/) 赞助 CM5 核心板和 CM5-IO 底板。

## 许可证

BSD-2-Clause-Patent，与 TianoCore 和 edk2-rockchip 相同。取自 Linux、U-Boot 和 Rockchip 的代码保留各自的许可证，[LICENSE](LICENSE) 里列出了这些文件。
