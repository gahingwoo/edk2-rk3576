# edk2-rk3576

[English](index.md) | 简体中文

Rockchip RK3576 开发板的 UEFI 固件（EDK2 / TianoCore）：Radxa ROCK 4D、ArmSoM CM5-IO 和立创泰山派 3M。

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> 操作系统
```

## 从一块新板子到 Windows 或 Linux

1. 先看你的板子：[Radxa ROCK 4D](boards/rock4d.zh-CN.md)、[ArmSoM CM5-IO](boards/cm5io.zh-CN.md)、[立创泰山派 3M](boards/tspi3m.zh-CN.md)。
2. 写入固件：见[刷写固件](flash.zh-CN.md)。最简单的是 [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio) 和 [flash.gahingwoo.com](https://flash.gahingwoo.com/)。
3. Windows 11 23H2（CM5-IO、泰山派 3M）：见 [Windows](windows.zh-CN.md)。
4. Linux：见 [Linux](linux.zh-CN.md)。
5. 遇到问题：[故障排查](troubleshooting.zh-CN.md)、[已知问题](known-issues.zh-CN.md)。

## 下载

- 映像：[最新正式版](https://github.com/gahingwoo/edk2-rk3576/releases/latest)，或 `main` 的 [`ci` 构建](flash.zh-CN.md#滚动的-ci-构建)
- 浏览器刷机：[flash.gahingwoo.com](https://flash.gahingwoo.com/)
- 源码：[github.com/gahingwoo/edk2-rk3576](https://github.com/gahingwoo/edk2-rk3576)

## 其他页面

- [固件菜单和功能](firmware-menus.zh-CN.md)：语言、截图、调试报告、恢复全部设置、时钟、显示、触摸、给操作系统用的选项
- [图片](gallery.md)（英文）
- 开发文档（英文）：[编译](dev/building.md)、[架构](dev/architecture.md)、[映像布局](dev/image-layout.md)、[硬件](dev/hardware.md)、[ACPI](dev/acpi.md)、[测试](dev/testing.md)、[发布](dev/releasing.md)

## 相关项目

| 仓库 | 内容 |
|---|---|
| [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) | Windows 驱动和 WinPE 安装映像 |
| [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) | Mali-G52 的 Windows GPU 和显示驱动 |
| [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | Windows NPU 驱动和 charsiu 大模型运行时 |
| [maskrom-studio](https://github.com/gahingwoo/maskrom-studio) | 桌面程序：写固件、不用 U 盘装 Windows、恢复 Armbian 或出厂系统 |
| [edk2-webflash](https://github.com/gahingwoo/edk2-webflash) | flash.gahingwoo.com 网页刷机工具 |
