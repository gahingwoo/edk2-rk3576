# 刷写固件

[English](flash.md) | 简体中文

怎样把固件写到板子上、以后怎样更新、出问题时怎样恢复。

## 用哪个映像

正式版在 [Releases 页面](https://github.com/gahingwoo/edk2-rk3576/releases/latest)。
`scripts/package.sh` 会在 `out/` 下生成同样的文件。

| 文件 | 板子 | 写到哪里 |
|---|---|---|
| `ROCK4D-spi-edk2-<版本>.img` | Radxa ROCK 4D | 16 MB SPI NOR，从 0 扇区开始 |
| `CM5IO-emmc-edk2-<版本>.img` | ArmSoM CM5-IO | eMMC，带分区表 |
| `CM5IO-sdcard-edk2-<版本>.img` | ArmSoM CM5-IO | SD 卡 |
| `TaishanPi3M-emmc-edk2-<版本>.img` | 立创泰山派 3M | eMMC，带分区表。装 Windows 必须用这个。 |
| `TaishanPi3M-sdcard-edk2-<版本>.img` | 立创泰山派 3M | SD 卡，同时 eMMC 上要有 SPL |
| `TaishanPi3M-spl-<版本>.bin` | 立创泰山派 3M | 单独的 SPL，写到 eMMC 64 扇区 |
| `<板子>-emmc.sfdisk` | CM5-IO、泰山派 3M | 单独的 eMMC 分区表 |

### 滚动的 ci 构建

标签为 [`ci`](https://github.com/gahingwoo/edk2-rk3576/releases/tag/ci) 的预发布版是
`main` 最近一次成功构建的映像，文件名形如 `<板子>-emmc-edk2-ci-<sha>.img`。
`main` 每次构建成功都会替换它。它不会被标为最新版，整体也没有在板子上跑过。

平时用正式版。只有需要 `main` 上已合并、但还没发布的改动时才用 `ci` 构建，并且留着正式版映像，方便退回。
截至 2026-11-24，泰山派 3M 在 Windows 下的触摸、蓝牙、耳机孔、HDMI 音频和 USB-C 都只在 `ci` 构建里，0.4.1 正式版没有。

## 三种写入方法

三种方法都要让板子进入 MaskROM 模式，用 USB 连接电脑。

1. [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：Windows、macOS、Linux
   桌面程序，可以写固件，也可以不用 U 盘直接装 Windows。截至 2026-11-24 还没有在板子上跑过。
2. [flash.gahingwoo.com](https://flash.gahingwoo.com/)：用 Chrome 或 Edge 打开，不用安装任何东西。
3. 命令行的 `rkdeveloptool`，见下文。

在 Windows 上，MaskROM 设备需要 WinUSB 驱动：先用 [Zadig](https://zadig.akeo.ie/)
给 Rockchip 设备装上。Linux 和 macOS 不需要驱动。

### 用浏览器

1. 让板子进入 MaskROM 模式（方法见下面各板子），连上电脑。
2. 打开 [flash.gahingwoo.com](https://flash.gahingwoo.com/)，选板子和要写的固件。
3. 点 **Connect and write**，在浏览器弹窗里选 Rockchip 设备。
4. 引导程序发送后，板子会作为新 USB 设备重新出现：点 **Choose the board**，再选一次。
5. 等写入完成，板子会自动重启。

### rkdeveloptool

`db` 需要 Rockchip 的 loader 文件（文件开头是 `LDR `），比如 rkbin 里的
`rk3576_spl_loader_*.bin`。本仓库不带这个文件。`binaries/rk3576_ddr.bin`
只是 DDR 初始化程序，`db` 会报 `Opening loader failed`。

`db` 之后等几秒再执行下一条命令：设备要重新枚举，紧接着的第一次 `wl`
可能报 `Write LBA failed`。`rd` 之前确认 `wl` 到了 100%：写入失败时板子仍会重启，跑的还是原来的固件。
有些版本的 rkdeveloptool 把这几条命令叫作 `boot`、`write` 和 `reset`。

## 写入完整映像

用于新板子，或者想把 eMMC 完全重来。**这会替换分区表和 eMMC 上已安装的全部内容**，UEFI 设置也会清空。

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 <板子>-emmc-edk2-<版本>.img
rkdeveloptool rd
```

eMMC 映像带 GPT：固件在 p1（从 64 扇区开始，64 MiB），eMMC 剩下的部分是 p2，未格式化。
映像只有 32 MiB，所以末尾的备份 GPT 要在 Linux 下执行一次 `sudo sgdisk -e /dev/mmcblk0`
才会补上（Windows 安装脚本不需要它）。

网页刷机工具里对应的选项是 "UEFI firmware, whole image"。

## 只更新固件，保留其他内容

用于之前已经用完整映像写过、现在装着 Windows、Linux 或其他系统的 eMMC。
它把 eMMC 映像的 64 到 43647 扇区（SPL 和固件）写回 64 扇区，在保存 UEFI 设置的 43648
扇区之前停下。分区表、其他分区和设置都保留。CM5-IO 和泰山派 3M 用同一个范围。

网页刷机工具和 Maskrom Studio 里对应的选项是 "UEFI firmware, keep the partitions"。用 rkdeveloptool：

```bash
dd if=<板子>-emmc-edk2-<版本>.img of=firmware.bin bs=512 skip=64 count=43584
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 64 firmware.bin
rkdeveloptool rd
```

在板子本身运行的 Linux 里，等价的命令是：

```bash
sudo dd if=<板子>-emmc-edk2-<版本>.img of=/dev/mmcblk0 bs=512 skip=64 seek=64 count=43584 conv=fsync
```

执行前先用 `lsblk` 确认 `/dev/mmcblk0` 是 eMMC。

不要写整个 `p1` 分区，也不要对映像用 `tail -c +32769`：这两种做法都会用映像里空的设置区覆盖
43648 扇区，清掉所有 UEFI 设置和启动项。

如果分区表还是丢了，`sudo sfdisk --wipe always /dev/mmcblk0 < <板子>-emmc.sfdisk`
可以把映像的分区表写回去，后面的数据不受影响。

## Radxa ROCK 4D

固件在 16 MB 的 SPI NOR 上。

ROCK 4D 的 USB-C 口只供电（数据线没接）。进入 MaskROM：

1. 用 USB-A 对 USB-A 线，把板子上方那个 USB 3.0 USB-A 口接到电脑。
2. 按住 MaskROM 键，同时插上电源。
3. 写入：

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 0 ROCK4D-spi-edk2-<版本>.img
   rkdeveloptool rd
   ```

从 SD 卡启动的 Linux 里，也可以用 `flashcp` 写到 `/dev/mtd0`。UEFI 设置保存在同一块闪存的
`0xFC0000`，写完整映像会清空设置。

## ArmSoM CM5-IO

固件可以放在 eMMC 或 SD 卡上。底板上的 SPI NOR 只有 64 KB，放不下。

MaskROM：按住 MaskROM 键再上电。MaskROM 口就是 USB-C 口，先拔掉上面接的其他东西。

写到 eMMC：见上面"写入完整映像"或"只更新固件"，用 `CM5IO-emmc` 文件。

写到 SD 卡：

```bash
dd if=CM5IO-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync
```

BootROM 先试 eMMC 再试 SD 卡，所以只要 eMMC 上有引导程序，SD 卡就会被忽略。
UEFI 设置保存在卡上，位于固件映像内的 43648 扇区。

## 立创泰山派 3M

这块板子的 BootROM 只读 eMMC，单独一张 SD 卡是启动不了的。eMMC 上至少要有 SPL。

MaskROM：板子上没有 MaskROM 键。

1. 断电。
2. 短接 `RK3576` 丝印正下方标着 `MSK` 的两个焊盘（0.4.1 正式版附有照片 `TaishanPi3M-msk-pads.png`）。
3. 用标着 `OTG+DP` 的 USB-C 口连接电脑，然后上电。电脑上会出现设备 `2207:350e`。

已经在跑本固件的板子，也可以在启动倒计时按 F4 进入 MaskROM。

全部放在 eMMC 上（装 Windows 需要这样）：见上面"写入完整映像"或"只更新固件"，用
`TaishanPi3M-emmc` 文件。写完整映像会替换立创的出厂系统，要恢复出厂系统得重新写立创自己的固件。

或者 SPL 放在 eMMC 上、固件放在 SD 卡上。插着卡时，SPL 打印 `Trying to boot from MMC2`
并从卡上加载固件；拔掉卡时，板子启动 eMMC 上原有的系统。

1. 把 SPL 写到 eMMC 64 扇区。在板子上运行的 Linux 里（只替换约 200 KB 的 SPL；
   自带 U-Boot FIT 在 16384 扇区的系统，在不插卡时照样能启动）：

   ```bash
   sudo dd if=TaishanPi3M-spl-<版本>.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync
   ```

   或者在 MaskROM 下：

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 TaishanPi3M-spl-<版本>.bin
   rkdeveloptool rd
   ```

2. 在电脑上把固件写到 SD 卡：

   ```bash
   dd if=TaishanPi3M-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync
   ```

替换 SPL 之前，先确认能进入 MaskROM。SPL 写坏了，要靠 MaskROM 恢复。

自己编译映像：`scripts/build.sh tspi3m` 和 `scripts/package.sh tspi3m`
（[编译说明](dev/building.md)，英文）。

## 恢复

MaskROM 在 BootROM 里，这里的任何操作都不会改写它，所以板子总能进入 MaskROM 重新写入。

如果板子能启动但设置有问题，在首页选 "Reset All Settings"，或者在启动倒计时按 F9，
会清空 UEFI 设置，像刚写好固件的板子一样启动。见[固件菜单](firmware-menus.zh-CN.md)。
