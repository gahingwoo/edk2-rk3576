# 刷写固件

[English](flash.md) | 简体中文

## 用哪个映像

从 [Releases 页面](https://github.com/gahingwoo/edk2-rk3576/releases/latest)下载，或运行
`scripts/package.sh` 后在 `out/` 里找。偏移见[映像布局](dev/image-layout.zh-CN.md)。

<!-- generated:images -->

| 发布文件 | 板子 | 写到哪里 |
|---|---|---|
| `CM5IO-emmc-edk2-<版本>.img` | ArmSoM CM5-IO | eMMC，带分区表（p1 固件 64 MiB，p2 数据） |
| `CM5IO-sdcard-edk2-<版本>.img` | ArmSoM CM5-IO | SD 卡（也可写到 eMMC，没有分区表） |
| `CM5IO-emmc.sfdisk` | ArmSoM CM5-IO | 单独的 eMMC 分区表 |
| `ROCK4D-spi-edk2-<版本>.img` | Radxa ROCK 4D | 16 MB SPI NOR，从扇区 0 开始 |
| `TaishanPi3M-emmc-edk2-<版本>.img` | 立创泰山派 3M | eMMC，带分区表（p1 固件 64 MiB，p2 数据） |
| `TaishanPi3M-sdcard-edk2-<版本>.img` | 立创泰山派 3M | SD 卡（也可写到 eMMC，没有分区表） |
| `TaishanPi3M-emmc.sfdisk` | 立创泰山派 3M | 单独的 eMMC 分区表 |

<!-- /generated -->

正式版还带 `TaishanPi3M-spl-<版本>.bin`，即单独的泰山派 3M SPL（写到 eMMC 64 扇区）。

### 滚动的 ci 构建

预发布版 [`ci`](https://github.com/gahingwoo/edk2-rk3576/releases/tag/ci) 是 `main`
最近一次成功构建的映像（`<板子>-emmc-edk2-ci-<sha>.img` 等）。`main` 每次构建都会替换它，
它整体没有在板子上跑过。只在需要还没发布的改动时才用，并留着正式版映像以便退回。

## 让板子进入 MaskROM

- **Radxa ROCK 4D：** 用 USB-A 对 USB-A 线把上方的 USB 3.0 口接到电脑（USB-C 口只供电），
  按住 MaskROM 键再插电源。
- **ArmSoM CM5-IO：** 按住 MaskROM 键再上电。MaskROM 口就是 USB-C 口，先拔掉上面接的其他东西。
- **立创泰山派 3M：** 没有按键。断电，短接 `RK3576` 丝印正下方标着 `MSK` 的两个焊盘
  （照片见 0.4.1 正式版附件 `TaishanPi3M-msk-pads.png`），把标着 `OTG+DP` 的 USB-C 口接到电脑，上电。
- 已经在跑本固件的板子，也可以在启动倒计时按 F4 进入 MaskROM。

之后电脑上会出现 USB 设备 `2207:350e`。在 Windows 上，先用 [Zadig](https://zadig.akeo.ie/)
给它装 WinUSB 驱动；Linux 和 macOS 不需要驱动。

## 三种写入方法

1. [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：Windows、macOS、Linux
   桌面程序，也能不用 U 盘直接装 Windows。
2. 用 Chrome 或 Edge 打开 [flash.gahingwoo.com](https://flash.gahingwoo.com/)：选板子和固件，
   点 **Connect and write**，选 Rockchip 设备；引导程序发送后在 **Choose the board** 里再选一次。
3. `rkdeveloptool`：
   - `db` 需要以 `LDR ` 开头的 Rockchip loader，比如 rkbin 里的 `rk3576_spl_loader_*.bin`（本仓库不带）。
     `binaries/rk3576_ddr.bin` 只是 DDR 初始化程序，会报 `Opening loader failed`。
   - `db` 之后等几秒；紧接着的 `wl` 可能报 `Write LBA failed`。
   - `rd` 之前确认 `wl` 到了 100%。写入失败时板子照样重启，跑的还是旧固件。

## 写入完整映像

用于新板子或要重新开始的 eMMC。**这会替换分区表、eMMC 上的所有内容和已保存的 UEFI 设置。**
网页刷机工具里对应 "UEFI firmware, whole image"。

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 <板子>-emmc-edk2-<版本>.img
rkdeveloptool rd
```

映像只有 32 MiB，所以要在 Linux 下运行一次 `sudo sgdisk -e /dev/mmcblk0` 写入备份 GPT（Windows 安装脚本不需要）。

## 只更新固件，保留其他内容

用于已经装了 Windows、Linux 或其他分区的 eMMC（CM5-IO、泰山派 3M）。只写
<!-- generated:spl-sector -->64<!-- /generated --> 到 <!-- generated:keep-last -->43647<!-- /generated -->
扇区（SPL 和固件），在存放 UEFI 设置的 <!-- generated:nvdata-sector -->43648<!-- /generated --> 扇区之前停下。
网页刷机工具和 Maskrom Studio 里对应 "UEFI firmware, keep the partitions"。

<!-- generated:keep-commands -->

```bash
dd if=<板子>-emmc-edk2-<版本>.img of=firmware.bin bs=512 skip=64 count=43584
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 64 firmware.bin
rkdeveloptool rd
```

<!-- /generated -->

在板子上的 Linux 里（先用 `lsblk` 确认 `/dev/mmcblk0` 是 eMMC）：

<!-- generated:keep-dd -->

```bash
sudo dd if=<板子>-emmc-edk2-<版本>.img of=/dev/mmcblk0 bs=512 skip=64 seek=64 count=43584 conv=fsync
```

<!-- /generated -->

不要写整个 `p1` 分区，也不要用 `tail -c +32769`：两者都会覆盖设置，清掉所有 UEFI 设置和启动项。

如果分区表丢了，`sudo sfdisk --wipe always /dev/mmcblk0 < <板子>-emmc.sfdisk` 可以写回去，后面的数据不受影响。

## Radxa ROCK 4D

固件和 UEFI 设置都在 16 MB SPI NOR 上；写入完整映像会重置设置。

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 ROCK4D-spi-edk2-<版本>.img
rkdeveloptool rd
```

Maskrom Studio 也能保留设置写入。从 SD 卡启动的 Linux 里用 `flashcp` 写 `/dev/mtd0` 也可以。

## ArmSoM CM5-IO

固件放在 eMMC 或 SD 卡上（底板的 64 KB SPI NOR 太小）。

- eMMC：按上面两节操作，用 `CM5IO-emmc` 文件。
- SD 卡：`dd if=CM5IO-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync`。
  设置保存在卡上。BootROM 先试 eMMC，所以只要 eMMC 上有引导程序，SD 卡就会被忽略。

## 立创泰山派 3M

BootROM 只读 eMMC，所以 eMMC 上至少要有 SPL。

**全部放在 eMMC 上**（装 Windows 需要）：按上面两节操作，用 `TaishanPi3M-emmc` 文件。完整映像会替换立创的出厂系统。

**SPL 在 eMMC，固件在 SD 卡**：插着卡时 SPL 打印 `Trying to boot from MMC2` 并从卡上加载固件；
拔掉卡时启动 eMMC 上原有的系统。先确认能进 MaskROM，SPL 写坏时要靠它恢复。

1. 把 SPL 写到 eMMC 64 扇区：在板子上的 Linux 里
   （`sudo dd if=TaishanPi3M-spl-<版本>.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync`），
   或在 MaskROM 下 `db` 之后 `rkdeveloptool wl 64 TaishanPi3M-spl-<版本>.bin`。
2. 把固件写到卡上：
   `dd if=TaishanPi3M-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync`。

## 恢复

MaskROM 在 BootROM 里，这里没有任何东西会改写它，所以板子总能重新写入。如果板子能启动但设置不对，
在首页选 "Reset All Settings"，或在启动倒计时按 F9（见[固件菜单](firmware-menus.zh-CN.md#恢复全部设置)）。
