# 一次性启动 Armbian 自己的 U-Boot（"Armbian (U-Boot)"）

[English](ARMBIAN-CHAINLOAD.md) | 简体中文

启动管理器里 **Armbian (U-Boot)** 启动项的开发说明。已在泰山派 3M 上板测（插着卡的正常启动、一次性启动 Armbian、
重启和断电后回到 UEFI）；"只在找到 U-Boot 时才列出启动项"还没有在板子上跑过。ROCK 4D：在 `rock4d-parity`
分支上构建，没有上板。CM5-IO：没有编译进去。

## 它做什么

选中这个启动项后，下一次启动会运行 Armbian 自己的 U-Boot，由它执行 Armbian 的 `boot.scr`，使用 Armbian 的内核和设备树。
之后的每一次启动（重启、断电、崩溃、U-Boot 卡住后断电）都回到本固件。

| 目标 | 做法 |
|---|---|
| 启动管理器里一个选项 | 启动项 `Armbian (U-Boot)`，应用程序 `ArmbianOnce.efi` |
| 只生效一次 | SPL 在加载任何东西之前擦掉请求并读回确认 |
| 用 Armbian 的 DTB、U-Boot 和内核 | SPL 加载 Armbian 的 FIT（它的 BL31、U-Boot、U-Boot DT）；这次启动不运行 EDK2 |
| Armbian 那边什么都不改 | 从 Armbian 放的位置读它的 FIT，或每次使用时从 Armbian 的文件复制 |
| 改动小 | 一个 SPL 补丁（一个新文件，约 260 行）、一个 EDK2 应用、一个启动项 |

## 为什么把请求放在 eMMC 上

在这些板子上，芯片里没有任何东西能在复位后保留：TF-A 的 PSCI SYSTEM_RESET 是第一全局复位，它的复位输出让 PMIC
给 SoC 断电重上电（`soc.c` `system_reset_init()`、`pmu.c` `rockchip_soc_soft_reset()`）。在泰山派 3M 上测得：

| 载体 | 结果 |
|---|---|
| PMU0/PMU1 GRF OS 寄存器 | 被清掉（OS_REG16 到 23 回来的是上电噪声） |
| PMU SRAM | 同一个电源域 |
| DRAM | 3768 个标记一个都没留下 |
| RTC 寄存器 | 每次复位 RTC 都掉电 |
| 第二全局复位（`CRU_GLB_SRST_SND = 0xeca8`） | 也许能保住 PMU 域，但没人在 RK3576 上用过，之后 BootROM/ddrbin 的表现未知；没有采用 |
| **eMMC 上的一个扇区** | **采用**：什么情况都能保留，EDK2 本来就写 eMMC，SPL 本来就读它 |

以后要测第二全局复位，在 UEFI Shell 里：`mm 0x2602623C 0x12345678 -w 4 -n`，再 `mm 0x27200C0C 0xECA8 -w 4 -n`；
启动日志里 `PMU1 GRF OS_REG0-15` 这一行最后应该是 `12345678`。

## 磁盘上的布局

eMMC 用户区，512 字节扇区：

| 扇区 | 内容 | 由谁写 |
|---|---|---|
| 64 | idbloader（DDR 初始化 + SPL） | 刷写 |
| **0x3FFF（16383）** | **请求记录** | ArmbianOnce；由 SPL 擦除 |
| 0x4000（16384） | 固件 FIT（BL31 + EDK2 + DTB + nvdata） | 刷写 |
| **0x10000..0x17FFF（32..48 MiB）** | **槽位：Armbian FIT 的副本** | ArmbianOnce，只在 Armbian 不在 SD 卡上时 |
| 0x20000（64 MiB） | `package.sh` 布局里的第一个分区（泰山派 3M 上是 Windows ESP） | |

0x3FFF 扇区在所有 Rockchip 启动介质上都处于 idbloader 和 FIT 之间的全零空隙里，`package.sh` 生成的映像也把它写成零，
所以刷写会清掉请求。如果有 `firmware`（类型 `8DA63339-0007-60C0-C436-083AC8230908`）以外的分区和槽位重叠，ArmbianOnce 拒绝使用槽位。

记录格式（`Silicon/Rockchip/Include/ArmbianOnce.h`，SPL 里的 `struct ao_request`）：

| 偏移 | 字段 |
|---|---|
| 0 | `"ARMBONCE"` |
| 8 | 版本，1 |
| 12 | 目标：1 SD 卡 0x4000 处的 FIT；2 eMMC 槽位；3 eMMC 0x4000 处的 FIT |
| 16 | 槽位扇区（目标 2 时为 0x10000，否则为 0） |
| 20 | 保留 |
| 508 | 第 0..507 字节的 CRC-32（IEEE，`crc32(0, ...)` / `CalculateCrc32`） |

## SPL 这边

`uboot/v2026.10/0001-rockchip-spl-boot-another-U-Boot-FIT-once-on-request.patch` 新增
`arch/arm/mach-rockchip/spl-armbian-once.c` 和 `CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE`，用的是弱钩子
`board_spl_mmc_get_uboot_raw_sector()` 和 `spl_perform_board_fixups()`。

泰山派 3M 的 SPL 是主线 U-Boot v2026.10，配 Armbian 的 `lckfb-tspi-3m-rk3576_defconfig` 和 DTS、DDR 程序 v1.08，
启动顺序是先 SD 卡。`scripts/build-spl.sh tspi3m` 从固定的输入（U-Boot 标签和提交、`uboot/v2026.10/tspi3m/` 里的板级文件、
补丁、按 SHA-256 校验的 armbian/rkbin DDR 程序）构建出 `out/TaishanPi3M/idbloader.img`，`package.sh` 会优先使用它。CI 会运行这个脚本。

每次启动时，在它尝试的第一个 MMC 设备上，SPL 会：

1. 找到 eMMC，记下它 0x4000 处的 FIT 是不是固件的（`/images/edk2`）；
2. 读 0x3FFF 扇区；如果是有效请求，就清零并读回，清零没成功就启动固件；
3. 按板子的顺序（先 SD，再 eMMC）逐个设备：
   - 请求 SD 卡：如果卡上的 FIT 不是固件，就用它，否则启动固件；
   - 请求 eMMC 槽位或 eMMC FIT：跳过 SD 卡，加载一次槽位或 eMMC FIT；第二次轮到 eMMC 时启动固件；
   - 没有请求：如果 eMMC 上有固件，就跳过 SD 卡上的外来 FIT（否则插着 Armbian 卡时每次都会从卡启动）。
     eMMC 上没有固件时，卡照常启动，可以当救援卡；
4. 只在外来路径上，把加载的 U-Boot 的 DT 里的 `/config/bootcmd` 设为
   `setenv bootmeths script extlinux; bootflow scan -lb`（`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_BOOTCMD`；留空即禁用）。见 R2。

哈希校验失败的 FIT 会被跳过，所以损坏的 Armbian FIT 最后会启动固件。

## EDK2 这边

`Silicon/Rockchip/Applications/ArmbianOnce` 编译进设置了 `RK_ARMBIAN_ONCE_ENABLE` 的板子（泰山派 3M）。
每次启动，磁盘连接之后，`PlatformBootManagerLib` 会连接 SD 控制器并调用 `ArmbianOnceProbe()`（`ArmbianOnceLib`）。
当 SPL 支持请求，并且在 SD 卡、eMMC（固件在 SD 卡上时）、USB 或 NVMe 盘的 0x4000 扇区，或已经在 eMMC 槽位里，
有一个 arm64 U-Boot FIT（`os = "u-boot"`、`arch = "arm64"`，没有其他架构的镜像）时，列出这个启动项。
否则删除对应的 Boot#### 变量。没插卡时只读 eMMC 的一两个扇区。

在文件系统里搜索 `u-boot-rockchip.bin` 不属于这个检查（每次启动都做太慢），所以只装在 eMMC 分区里的 Armbian，
要等槽位填过之后才有这个启动项。

这个启动项不是 active 的（启动顺序不会选到它），没有热键，始终放在 BootOrder 最后。只在有变化时才写变量。
也可以通过 BootNext 选它（从经本固件启动的 Linux 里 `efibootmgr -n`，并打开 "Save OS Variable Changes to eMMC"）。

应用程序：

1. 只有 64 扇区（eMMC 或 SD 卡）上的 SPL 带有 `ArmbianOnce/1` 标记时才继续，这样旧 SPL 不会遇到遗留的请求；
2. 选择目标：SD 卡 0x4000 扇区上的外来 FIT（1）；固件在 SD 卡上时 eMMC 0x4000 扇区上的外来 FIT（3）；
   否则把 Armbian 的 FIT 复制到 eMMC 槽位（2），来源依次是另一块盘（USB、NVMe）的 0x4000 扇区，或任何能读的文件系统上
   最新的 `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin`（固件带有 Ext4Dxe）偏移 `(0x4000 - 64) * 512` 处。
   FIT 长度由各镜像的 `data-offset`/`data-position` 和 `data-size` 算出。槽位内容不同时才写，每一块都读回校验；
3. 写入记录，读回，然后重启。

任何失败都在写记录之前停下，并说明原因。

## SPI NOR 板子（Radxa ROCK 4D）

ROCK 4D 的 SPL 和固件 FIT 在 SPI NOR 上（idblock 0x8000，FIT 0x60000），启动顺序是 `"same-as-spl"`，然后 SD 卡。
SPL 不方便写 SPI NOR，所以请求放在 SD 卡的 0x3FFF 扇区。两个开关，默认都关：

- SPL `CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_SD_REQUEST`（`uboot/v2026.10/0002-...patch`）：在 SD 卡上读取并擦除记录，
  只接受目标 1。有请求时 `spl_spi_get_uboot_offs()` 返回 0（那里没有 FIT；`SPL_RAW_IMAGE_SUPPORT` 关闭时 SPL 会转向 SD 卡）。
  没有请求时偏移为 `CONFIG_SYS_SPI_U_BOOT_OFFS`（0x60000）。擦除后卡上没有 U-Boot FIT：SPL 启动固件。
- EDK2 `PcdArmbianOnceRequestOnSd`（RockchipPkg.dec）：在 SPI NOR（0x8000..0x5FFFF）里找 SPL 标记，只在 SD 卡 0x4000
  扇区找 FIT，把请求写到 SD 卡 0x3FFF 扇区。没有槽位，也不复制：Armbian 卡的 0x10000 扇区在它的根分区里。

`boards/rock4d.conf` 让 `build-spl.sh` 用 U-Boot v2026.10 自带的 `rock-4d-rk3576_defconfig` 和 DT，加上两个补丁、
`uboot/v2026.10/rock4d/spl.config`（v1.12 板子的 XMC 闪存、`SPL_MMC_WRITE`、两个开关）和 DDR 程序 v1.09 构建。
它会替换每个 ROCK 4D 映像带的 SPL，所以在有板子启动过之前只放在 `rock4d-parity` 分支上。

## 用这种方法能启动什么

主线 `spl_load_simple_fit()` 能加载的任何 FIT：TF-A 作为 `firmware`，U-Boot 和可选的 OP-TEE（`os = "tee"`，作为 BL32 传递）
作为 loadables，一个 U-Boot DT，SHA-1/SHA-256/CRC-32 哈希，外部或内嵌数据都可以。运行的是 FIT 自己的 BL31。

| 里面的 U-Boot | 能不能用 |
|---|---|
| 主线（泰山派 3M 的 Armbian edge 和 vendor，CM5-IO 的 edge） | 能：同样的标签、defconfig、DTS 和 DDR 程序，所以拿到的交接信息正是它期望的（PMU1 GRF OS_REG2..5 里的 DRAM 大小、DDR_MEM ATAG、`u-boot,spl-boot-device`） |
| 为别的 DDR 程序构建的 | 应该能（格式相同），没测过 |
| Rockchip vendor U-Boot 2017.09 next-dev 及其分支 | 不支持（需要 vendor SPL 的 ATAG 和布局） |
| Radxa `next-dev-v2024.10`（CM5-IO vendor 分支） | 基于主线，没测过 |
| 需要自己的 TPL/SPL 状态（Falcon、VBE、安全启动密钥） | 不支持 |

## 支持情况

| Armbian 在哪里 | SPL 从哪里加载 Armbian 的 U-Boot | 状态 |
|---|---|---|
| SD 卡，固件在 eMMC | SD 卡 0x4000 扇区，原地 | 主要场景，泰山派 3M，已上板 |
| eMMC，固件在 SD 卡（SPL 在 eMMC） | eMMC 0x4000 扇区，原地 | 支持 |
| USB 盘 | eMMC 槽位，每次使用时复制 | 支持，前提是 Armbian 的 U-Boot 能在 USB 上找到 `boot.scr`；用 USB-A（U-Boot 里没有 FUSB302 驱动） |
| NVMe | eMMC 槽位，同 USB | 同一条代码路径；没测过（泰山派 3M 没有 M.2） |
| 和固件同一块 eMMC 上的分区 | eMMC 槽位，每次使用时从 `u-boot-rockchip.bin` 复制 | 支持；Armbian 绝不能把引导程序写到那里（R5） |
| 和固件 FIT 同一块盘的 0x4000 | 不可能 | |
| ROCK 4D：SD 卡，固件在 SPI NOR | SD 卡 0x4000 扇区 | `rock4d-parity` 分支，没上板；不支持 USB/NVMe（没有 eMMC 槽位） |
| CM5-IO | | 没编译进去：`build-spl.sh` 还不重建它的 SPL，而且需要先 SD 的启动顺序 |

Armbian 只有在 `FORCE_UBOOT_UPDATE=yes` 或 `armbian-install` 时才把引导程序写到盘上，所以 Armbian 卡 0x4000 处的 FIT
是最后一次放上去的那个，ArmbianOnce 原样使用。

## 风险

- **R1**：eMMC 上有固件时，插在卡槽里的 Armbian SD 卡不会再自动启动。eMMC 上没有固件时它仍可用于救援。
- **R2**：这条路径上 Armbian 的启动命令被替换了。不替换时，Armbian 的 U-Boot 会通过它的 EFI 启动管理器启动 eMMC 上的
  Windows Boot Manager 然后卡住。通过 U-Boot 走 EFI 启动的 Armbian 映像用这种方法启动不了。把 Kconfig 字符串留空即可保留 Armbian 自己的 `bootcmd`。
- **R3**：有请求时 SPL 会写一个 eMMC 扇区；写到一半断电会留下 CRC 错误的扇区，即视为没有请求。
- **R4**：槽位只对照分区表检查；本项目提供的所有布局里 32 到 48 MiB 都没有使用。
- **R5**：在同一块 eMMC 上运行 `armbian-install`、`armbian-config` 的引导程序更新或 `FORCE_UBOOT_UPDATE=yes`，会覆盖 64 扇区的固件。
- **R6**：Armbian 的 U-Boot 卡住时需要断电，下一次启动是固件。
- **R7**：SPL 每次启动都会初始化 eMMC，固件在 SD 卡上时也一样（几毫秒）。
- **R8**：在泰山派 3M 上，Windows 设置的 BootNext 在复位后不保留（见[硬件](dev/hardware.zh-CN.md#立创泰山派-3m)），
  所以只能从启动管理器"重启进 Armbian"。

## 构建

```bash
scripts/build.sh tspi3m
scripts/build-spl.sh tspi3m     # 第一次会把 U-Boot v2026.10 克隆到 third_party/u-boot
scripts/package.sh tspi3m       # 会使用 out/TaishanPi3M/idbloader.img
```

`UBOOT_DIR` 和 `RKBIN_DIR` 可以指向已有的 U-Boot 克隆和 DDR 程序目录。不运行 `build-spl.sh` 时映像用
`binaries/idblock_tspi3m.bin`，启动项会提示 SPL 不支持。

升级 U-Boot：把 `uboot/v2026.10` 复制成新版本，从 Armbian 的 `patch/u-boot/<版本>` 更新板级文件，rebase 补丁
（它只改 `arch/arm/mach-rockchip/{Kconfig,Makefile}` 并新增一个文件），再改 `boards/tspi3m.conf` 里的
`SPL_UBOOT_TAG`、`SPL_UBOOT_COMMIT`、`SPL_PATCH_DIR`、`SPL_BOARD_DIR`。

## 板上测试（泰山派 3M）

需要一张 Armbian SD 卡（主线 U-Boot 映像）、1500000 的串口和 MaskROM。更新只写 SPL 和固件，UEFI 变量保留（nvdata 在 43648 扇区）。

1. 刷写：

   ```bash
   dd if=TaishanPi3M-emmc.img of=spl.bin bs=512 skip=64 count=400
   dd if=TaishanPi3M-emmc.img of=firmware.bin bs=512 skip=16384 count=27264
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 spl.bin
   rkdeveloptool wl 16384 firmware.bin
   rkdeveloptool rd
   ```

2. 不插卡启动：固件和 Windows 照旧；没有 `ArmbianOnce/1` 这一行。
3. 插卡启动：`ArmbianOnce/1: not booting the U-Boot FIT on the SD card, the firmware is on the eMMC`，然后进固件。启动项已列出。
4. 选这个启动项：`Request written. Restarting`，然后 SPL 打印 `request found and erased: booting U-Boot from the SD card, once`，
   接着是 Armbian 的 U-Boot、`boot.scr`、Armbian。在 Armbian 里 `/sys/firmware/efi` 不存在，
   `dd if=/dev/mmcblk0 bs=512 skip=16383 count=1 status=none | tr -d '\0' | wc -c` 输出 0（用 eMMC 的设备名）。
5. `sudo reboot`、`echo b | sudo tee /proc/sysrq-trigger`，以及在 Armbian 的 U-Boot 里断电，都回到固件。第 4 步重复三次。
6. 拔掉卡：启动管理器和 BootOrder 里都没有这个启动项；插回卡：又出现，排在最后。
7. 屏幕显示 `Restarting` 时拔卡：SPL 打印 `U-Boot was not loaded from the SD card, booting the firmware`；16383 扇区为零。
8. 可选，槽位：拔卡，在 USB-A 上插 Armbian USB 盘，选启动项：`Copying it to the eMMC slot`，SPL 打印
   `booting U-Boot from the eMMC slot, once`。第二次使用会打印 `The eMMC slot already holds that U-Boot.`

第 3 到 5 步串口日志里的 SPL 行就是证据。
