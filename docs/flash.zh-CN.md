# 刷写固件

[English](flash.md) | 简体中文

## 用哪个映像

从 [Releases 页面](https://github.com/gahingwoo/edk2-rk3576/releases/latest)下载，或运行
`scripts/package.sh` 后在 `out/` 里找。偏移见[映像布局](dev/image-layout.zh-CN.md)。

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

预发布版 [`ci`](https://github.com/gahingwoo/edk2-rk3576/releases/tag/ci) 是 `main`
最近一次成功构建的映像（`<板子>-emmc-edk2-ci-<sha>.img` 等，附带 `.sfdisk` 分区表和
`SHA256SUMS.txt`）。`main` 每次构建都会替换它，它整体没有在板子上跑过。只在需要还没发布的改动时才用，
并留着正式版映像以便退回。

## 三种写入方法

三种方法都要让板子进入 MaskROM 模式（方法见下面各板子），用 USB 连接电脑。

1. [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)：Windows、macOS、Linux
   桌面程序，也能不用 U 盘直接装 Windows。在 ROCK 4D 上跑过（把 Windows 装到 NVMe）。
2. 用 Chrome 或 Edge 打开 [flash.gahingwoo.com](https://flash.gahingwoo.com/)。
3. `rkdeveloptool`。

在 Windows 上，先用 [Zadig](https://zadig.akeo.ie/) 给 Rockchip 设备装 WinUSB 驱动。Linux 和 macOS 不需要驱动。

### 用浏览器

1. 让板子进入 MaskROM 模式，连上电脑。
2. 打开 [flash.gahingwoo.com](https://flash.gahingwoo.com/)，选板子和要写的固件。
3. 点 **Connect and write**，选 Rockchip 设备。
4. 引导程序发送后，板子会作为新 USB 设备出现：点 **Choose the board**，再选一次。
5. 等写入完成，板子会自动重启。

### rkdeveloptool

- `db` 需要以 `LDR ` 开头的 Rockchip loader，比如 rkbin 里的 `rk3576_spl_loader_*.bin`（本仓库不带）。
  `binaries/rk3576_ddr.bin` 只是 DDR 初始化程序，会报 `Opening loader failed`。
- `db` 之后等几秒；紧接着的 `wl` 可能报 `Write LBA failed`。
- `rd` 之前确认 `wl` 到了 100%。写入失败时板子照样重启，跑的还是旧固件。
- 有些版本把这几条命令叫作 `boot`、`write` 和 `reset`。

## 写入完整映像

用于新板子或要重新开始的 eMMC。**这会替换分区表、eMMC 上的所有内容和已保存的 UEFI 设置。**
网页刷机工具里对应 "UEFI firmware, whole image"。

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 <板子>-emmc-edk2-<版本>.img
rkdeveloptool rd
```

GPT 里 p1 是固件（64 扇区起，64 MiB），p2 是 eMMC 的其余部分，未格式化。映像只有 32 MiB，
所以要在 Linux 下运行一次 `sudo sgdisk -e /dev/mmcblk0` 写入备份 GPT（Windows 安装脚本不需要）。

## 只更新固件，保留其他内容

用于已经装了 Windows、Linux 或其他分区的 eMMC。只写 64 到 43647 扇区（SPL 和固件），
在存放 UEFI 设置的 43648 扇区之前停下。CM5-IO 和泰山派 3M 用同一个范围。
网页刷机工具和 Maskrom Studio 里对应 "UEFI firmware, keep the partitions"。

```bash
dd if=<板子>-emmc-edk2-<版本>.img of=firmware.bin bs=512 skip=64 count=43584
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 64 firmware.bin
rkdeveloptool rd
```

在板子上的 Linux 里（先用 `lsblk` 确认 `/dev/mmcblk0` 是 eMMC）：

```bash
sudo dd if=<板子>-emmc-edk2-<版本>.img of=/dev/mmcblk0 bs=512 skip=64 seek=64 count=43584 conv=fsync
```

不要写整个 `p1` 分区，也不要用 `tail -c +32769`：两者都会覆盖 43648 扇区，清掉所有 UEFI 设置和启动项。

如果分区表丢了，`sudo sfdisk --wipe always /dev/mmcblk0 < <板子>-emmc.sfdisk` 可以写回去，后面的数据不受影响。

## Radxa ROCK 4D

固件和 UEFI 设置（在 `0xFC0000`）都在 16 MB SPI NOR 上；写入映像会重置设置。

1. 用 USB-A 对 USB-A 线把上方的 USB 3.0 口接到电脑。USB-C 口只供电。
2. 按住 MaskROM 键再插电源。
3. 写入：

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 0 ROCK4D-spi-edk2-<版本>.img
   rkdeveloptool rd
   ```

从 SD 卡启动的 Linux 里用 `flashcp` 写 `/dev/mtd0` 也可以。

## ArmSoM CM5-IO

固件放在 eMMC 或 SD 卡上（底板的 64 KB SPI NOR 太小）。MaskROM：按住 MaskROM 键再上电；
MaskROM 口就是 USB-C 口，先拔掉上面接的其他东西。

- eMMC：按上面两节操作，用 `CM5IO-emmc` 文件。
- SD 卡：`dd if=CM5IO-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync`。
  设置保存在卡上。BootROM 先试 eMMC，所以只要 eMMC 上有引导程序，SD 卡就会被忽略。

## 立创泰山派 3M

BootROM 只读 eMMC，所以 eMMC 上至少要有 SPL。

MaskROM（没有按键）：

1. 断电。
2. 短接 `RK3576` 丝印正下方标着 `MSK` 的两个焊盘（照片见 0.4.1 正式版附件 `TaishanPi3M-msk-pads.png`）。
3. 把标着 `OTG+DP` 的 USB-C 口接到电脑，上电。会出现 `2207:350e` 设备。

已经在跑本固件的板子，也可以在启动倒计时按 F4 进入 MaskROM。

**全部放在 eMMC 上**（装 Windows 需要）：按上面两节操作，用 `TaishanPi3M-emmc` 文件。完整映像会替换立创的出厂系统。

**SPL 在 eMMC，固件在 SD 卡**：插着卡时 SPL 打印 `Trying to boot from MMC2` 并从卡上加载固件；
拔掉卡时启动 eMMC 上原有的系统。先确认能进 MaskROM，SPL 写坏时要靠它恢复。

1. 把 SPL 写到 eMMC 64 扇区（约 200 KB；U-Boot FIT 在 16384 扇区的系统不插卡时照常启动）。在板子上的 Linux 里：

   ```bash
   sudo dd if=TaishanPi3M-spl-<版本>.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync
   ```

   或在 MaskROM 下：

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 TaishanPi3M-spl-<版本>.bin
   rkdeveloptool rd
   ```

2. 把固件写到卡上：

   ```bash
   dd if=TaishanPi3M-sdcard-edk2-<版本>.img of=/dev/sdX bs=1M status=progress conv=fsync
   ```

## 恢复

MaskROM 在 BootROM 里，这里没有任何东西会改写它，所以板子总能重新写入。如果板子能启动但设置不对，
在首页选 "Reset All Settings"，或在启动倒计时按 F9（见[固件菜单](firmware-menus.zh-CN.md#恢复全部设置)）。
