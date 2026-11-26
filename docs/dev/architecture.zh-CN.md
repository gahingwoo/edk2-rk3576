# 架构

[English](architecture.md) | 简体中文

这个代码树是怎样组织的，以及为什么这样组织。

## 启动链

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> OS
```

- BootROM 从 SPI NOR（ROCK 4D）或从 eMMC、SD 卡（CM5-IO、泰山派 3M）加载 idblock（DDR 初始化程序和 U-Boot SPL）。
- SPL 加载一个 FIT：BL31、EDK2、SPL 自己用的 DTB，以及在 SD 和 eMMC 上的 UEFI 变量存储（[映像布局](image-layout.zh-CN.md)）。
- BL31 用的是开源的 TF-A v2.14.0（`binaries/bl31.elf`）；只有这个文件不存在时才用 Rockchip 的 BL31。
- EDK2 作为 BL33 在 EL2 运行，起始地址 `0x40800000`。

预编译的输入文件以及每个文件是怎样构建的，见 [binaries/README.md](https://github.com/gahingwoo/edk2-rk3576/blob/main/binaries/README.zh-CN.md)。

## 目录结构

```
Platform/ArmSoM/CM5IO/       board package: DSC, PMIC/GPIO/PHY glue, ACPI, DTS
Platform/Radxa/ROCK4D/
Platform/LCKFB/TaishanPi3M/
Silicon/Rockchip/RK3576/     SoC package: RK3576Base.dsc.inc, FDF, ACPI, drivers
Silicon/Rockchip/            drivers shared across Rockchip SoCs
Silicon/Synopsys/DesignWare/ DWC EQoS and DWC MMC
boards/                      one .conf per board, read by the scripts
scripts/                     setup-host, build, package; serial and KD helpers
patches/                     patches to the TianoCore core, applied at setup
binaries/                    BL31, DDR init blob, SPL
devicetree/                  board DTS (mainline-based), vendor DTBs
docs/                        the site at edk2.gahingwoo.com
```

## 板级与 SoC 的划分

板级 DSC 只放各板子之间不同的内容：有哪些设备、GPIO、PMIC 和 PHY 的适配代码、设置选项的默认值、ACPI DSDT 和设备树。凡是由 SoC 决定的内容都在 `Silicon/Rockchip/RK3576/RK3576Base.dsc.inc` 和 `RK3576.fdf` 里。

RK3576 的包是独立的，不叠加在 RK3588 的配置上，因为继承来的 RK3588 取值（内存映射、I2C 地址、复位 ID）对 RK3576 是错的。以前叠加式的旧代码树在 `legacy/v0.1` 分支上。

## ACPI 与设备树

三块板子都同时安装 ACPI 表和设备树，Windows 用 ACPI，Linux 用设备树。详见 [ACPI](acpi.zh-CN.md)。

交给 Linux 的设备树是固件自己的，由 `devicetree/mainline/` 编译而来，并在启动时做修正（`FdtPlatformDxe`）：例如泰山派 3M 接了屏幕时加上 DSI 面板和背光，"Force UEFI GOP Display" 打开时加上一个 simple-framebuffer 节点，以及把 USB-A 的 VBUS 供电设为常开。

## 核心补丁

`third_party/edk2` 是 tianocore/edk2，固定在 edk2-stable202602。对它的修改以补丁文件的形式放在 `patches/` 里，由 `setup-host.sh` 打上，因为子模块被重置时，直接在里面做的修改会丢失。每个补丁的说明见 [patches/README.md](https://github.com/gahingwoo/edk2-rk3576/blob/main/patches/README.zh-CN.md)。
