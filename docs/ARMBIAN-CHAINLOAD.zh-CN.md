# 一次性启动 Armbian 自己的 U-Boot（"Armbian (U-Boot)"）

[English](ARMBIAN-CHAINLOAD.md) | 简体中文

状态：2026-11-24 已合并到 `main`（`2187846`），并为泰山派 3M 构建。
同一天在泰山派 3M 上做了板测（合并提交 `a23adb9`）：插着 SD 卡时，正常启动仍然进入 UEFI 和 Windows；
"Armbian (U-Boot)" 能启动卡上 Armbian 的 U-Boot 和 Armbian；请求扇区读回来是零；
从 Armbian 重启和断电再上电都会回到 UEFI。"只在有 U-Boot 可启动时才列出这个启动项"的改动（`e081771`）
是在那次板测之后合并的，没有它的运行记录。完整的板测步骤在本文末尾。

## 它做什么

启动管理器里多一个启动项 **Armbian (U-Boot)**。选中它后，下一次启动会运行
Armbian 自己的 U-Boot，再由它运行 Armbian 的 `boot.scr`，用的是 Armbian 的内核和设备树。
之后的任何一次启动（从 Armbian 重启、断电再上电、系统崩溃、U-Boot 卡住后断电重启）
都会回到本固件。

| 目标 | 怎样实现 |
|---|---|
| G1 启动管理器里只需选一项 | 启动项 `Armbian (U-Boot)`（应用程序 `ArmbianOnce.efi`） |
| G2 只生效一次 | SPL 在加载任何东西之前先擦除请求，并把该扇区读回确认 |
| G3 用 Armbian 的 DTB、U-Boot 和内核 | SPL 加载 Armbian 的 FIT（其中的 BL31、U-Boot、U-Boot DT）。这次启动 EDK2 完全不运行，所以它的 DT 修正和 GOP 设置都不存在 |
| G4 Armbian 那边什么都不改 | Armbian 的 FIT 在 Armbian 放它的位置读取，或者每次使用这个启动项时从 Armbian 自己的文件复制 |
| G5 改动小 | 一个 U-Boot SPL 补丁（一个约 260 行的新文件）、一个 EDK2 应用程序、一个启动项 |

## 为什么把请求放在 eMMC 上

最初的想法是在 PMU GRF 的某个 OS 寄存器里写一个魔数，由 SPL 读取。
在这些板子上这行不通：**芯片上没有任何东西能在复位后保留下来。**

在泰山派 3M 上的测量结果（2026-11-22，通过串口读取）：

* PMU1 GRF OS_REG15（`0x2602623C`），文档说是"with npor reset"。在 UEFI Shell 里用 `mm`
  把它设成 `0xB007000A`，执行 `reset` 后读出来是 0
  （提交 1266943；日志在 `mm 0x2602623C 0xB007000A` 那一行附近）。
* DRAM：空闲内存里放的 3768 个标记值，复位后一个都没留下（提交 fd2f0c3）。
* TRM 里说 PMU0 GRF OS_REG16 到 23 "cannot be reset"。OS_REG16 是 U-Boot 和 Linux 用的
  BOOT_MODE_REG（`0x26024040`）。这条启动链里没有任何东西写 16 或 20 到 23，
  但每次复位（包括热复位）后它们的值都会变。下面是 Linux `reboot` 前后连续两次启动的值：
  `42157F6F .. A9057566 86545559 4579D675 1940A113`，然后是
  `42157F6F .. A9055467 86545559 4171C655 3900A113`。这是没有复位的触发器上电时的随机值：
  这个电源域掉过电。
* 每次启动 HYM8563 都报告 VL（时钟掉过电），无论之前是 Linux 重启还是 Windows 重启。
  这块板子没有 RTC 电池。

TF-A 解释了原因（v2.14.0，de38734，即 `binaries/` 里的 BL31；rk3576 部分的代码在
2a313ed22 上相同）：

* `soc.c` 的 `system_reset_init()` 写入 `CRU_GLB_RST_CON = 0xffdf`：对*第一种*全局复位
  （bit 2），全局复位也会复位 PMU 域（bit 3）。它还写入 `PMU1SGRF_SOC_CON0 = 0x00070007`，
  即"enable first/tsadc/wdt reset output"。
* `pmu.c` 的 `rockchip_soc_soft_reset()`（PSCI SYSTEM_RESET）写入
  `CRU_GLB_SRST_FST = 0xfdb9`，也就是第一种全局复位。它的复位输出接到 PMIC，
  PMIC 会给 SoC 断电再上电。
* 唯一的例外恰好印证了这一点：当 PMU0 OS_REG16 里是 `BOOT_BROM_DOWNLOAD` 时，
  `rockchip_soc_soft_reset_check_rstout()` 会在复位前关掉这个输出，注释写着
  "os_reg0 ... will be reset by NPOR"。这块板子上的"重启到 MaskROM"就是这样实现的，
  它需要只有安全世界才能写的 SGRF 寄存器，所以 EDK2 没法照做。

考虑过的方案：

| 载体 | 结论 |
|---|---|
| PMU0/PMU1 GRF OS 寄存器 | 断电再上电时被清掉（实测） |
| PMU SRAM 顶部的 `mem_os_reg` | 同一个电源域，下场一样 |
| DRAM | 被清掉（实测） |
| RTC 寄存器 | 泰山派上每次复位 RTC 都会掉电（实测） |
| 第二种全局复位（`CRU_GLB_SRST_SND = 0xeca8`，TRM 2.3.2），由 EDK2 直接写 | 有可能：它没有被选为复位 PMU 的那一种（`GLB_RST_CON` bit 2 = 第一种），也不在 TF-A 的复位输出列表里。但在 RK3576 上没人用过它：不知道之后 BootROM 和 ddrbin 能不能正常启动，PMIC 电源轨和板上外设也不会断电重启。没有采用。以后可以这样测：在 Shell 里先执行 `mm 0x2602623C 0x12345678 -w 4 -n`，再执行 `mm 0x27200C0C 0xECA8 -w 4 -n`；板子应该会重启，启动日志里 `PMU1 GRF OS_REG0-15` 那一行的最后一个值应该是 `12345678` |
| **eMMC 上的一个扇区** | **采用。** 什么情况都能保留下来，EDK2 本来就写 eMMC（nvdata），SPL 本来就读 eMMC。代价是 SPL 要写一个扇区（`CONFIG_SPL_MMC_WRITE`），而且只在有请求时才写 |

## 磁盘上的布局

eMMC 用户区，扇区大小 512 字节：

| 扇区 | 内容 | 由谁写入 |
|---|---|---|
| 64 | idbloader（DDR 初始化 + SPL） | 刷写 |
| **0x3FFF (16383)** | **请求记录** | ArmbianOnce 写入，SPL 擦除 |
| 0x4000 (16384) | 固件 FIT（BL31 + EDK2 + DTB + nvdata） | 刷写 |
| **0x10000..0x17FFF (32..48 MiB)** | **槽位：Armbian FIT 的一份副本** | ArmbianOnce，只在 Armbian 不在 SD 卡上时写 |
| 0x20000 (64 MiB) | `package.sh` 布局里的第一个分区（泰山派上也是 Windows 的 ESP） | |

在所有 Rockchip 启动介质上，扇区 0x3FFF 都在 idbloader 和 FIT 之间那段全零的空隙里
（Armbian 的 `u-boot-rockchip.bin` 也在这里填零）。`package.sh` 生成的映像在这里写零，
所以刷写会清掉请求。槽位在映像覆盖的 32 MiB 之后；如果除了 `firmware` 分区（类型
`8DA63339-0007-60C0-C436-083AC8230908`）之外还有别的分区和它重叠，ArmbianOnce 就拒绝使用它。

请求记录的格式（`Silicon/Rockchip/Include/ArmbianOnce.h`，以及 SPL 里的
`struct ao_request`）：

| 偏移 | 字段 |
|---|---|
| 0 | `"ARMBONCE"` |
| 8 | 版本，1 |
| 12 | 目标：1 SD 卡 0x4000 处的 FIT；2 eMMC 槽位；3 eMMC 0x4000 处的 FIT |
| 16 | 槽位扇区（目标 2 时为 0x10000，否则为 0） |
| 20 | 保留 |
| 508 | 字节 0..507 的 CRC-32（IEEE，同 `crc32(0, ...)` 和 `CalculateCrc32`） |

## SPL 这边

`uboot/v2026.10/0001-rockchip-spl-boot-another-U-Boot-FIT-once-on-request.patch`
新增 `arch/arm/mach-rockchip/spl-armbian-once.c` 和
`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE`。它用了主线 SPL 的两个弱符号钩子
`board_spl_mmc_get_uboot_raw_sector()` 和 `spl_perform_board_fixups()`，
没有改动任何现有代码。

我们给泰山派 3M 用的 SPL 是主线 U-Boot v2026.10，加上 Armbian 的
`lckfb-tspi-3m-rk3576_defconfig` 和 DTS、DDR blob v1.08，以及一处本地改动：先试 SD 卡
（见 `binaries/README.md`）。在这项工作之前它是手工构建的；现在 `scripts/build-spl.sh tspi3m`
从固定的输入（U-Boot 标签和提交、`uboot/v2026.10/tspi3m/` 里的板子文件、补丁、按 SHA-256
校验的 armbian/rkbin DDR blob）构建它，输出到 `out/TaishanPi3M/idbloader.img`，
`package.sh` 本来就优先用这个文件。CI 会运行这个脚本。
不开这个功能时，同一个脚本构建出的 SPL，除了版本行以外，字符串和
`binaries/idblock_tspi3m.bin` 完全相同。

每次启动时，在它尝试的第一个 MMC 设备上，SPL 会：

1. 找到 eMMC（不是 SD 卡的那个 MMC 设备），并记下它 0x4000 处的 FIT 是本固件的
   （`/images/edk2`）还是别的；
2. 读扇区 0x3FFF；如果是有效的请求，就把它清零并读回。如果擦除没有生效，就启动本固件；
3. 按板子的顺序（先 SD 卡，后 eMMC）逐个设备处理：
   * 请求目标是 SD 卡：如果 SD 卡上的 FIT 不是本固件的，就加载它；
     否则继续往下，启动本固件；
   * 请求目标是 eMMC 槽位或 eMMC 上的 FIT：跳过 SD 卡，加载槽位（只一次）或 eMMC 上的 FIT；
     第二次轮到 eMMC 时加载本固件；
   * 没有请求：如果 eMMC 上有本固件，SD 卡上的外来 FIT 会被**跳过**。不这样的话，
     插着一张 Armbian 卡就会每次都从卡启动，而之前的 SD 优先 SPL 正是这样。
     如果 eMMC 上没有本固件，卡照旧启动，所以它仍然可以当救援卡用；
4. 只在走外来 FIT 的路径时，把加载的 U-Boot 的控制 DT 里的 `/config/bootcmd` 设成
   `setenv bootmeths script extlinux; bootflow scan -lb`
   （`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_BOOTCMD`，设为空则关闭）。见风险 R2。

哈希校验失败的 FIT 会被 SPL 报告出来，然后它转到下一个设备，所以 SD 卡上损坏的
Armbian FIT 最终会启动本固件。

## EDK2 这边

`Silicon/Rockchip/Applications/ArmbianOnce` 会编进设置了
`RK_ARMBIAN_ONCE_ENABLE` 的板子（泰山派 3M，以及 `cm5io-parity` 分支上的 CM5-IO）。
`PlatformBootManagerLib` 只在有东西可启动时才把它列为 `Armbian (U-Boot)`。
每次启动时，在磁盘连接好之后、启动管理器读取启动项之前，它会连接 SD 控制器
（以防启动设备发现策略没有连接），并调用 `ArmbianOnceProbe()`（在 `ArmbianOnceLib` 里，
和应用程序共用）。满足以下条件时启动项才会出现：SPL 支持请求，并且在 SD 卡、eMMC
（固件在 SD 卡上时）、USB 或 NVMe 磁盘的 0x4000 扇区有一个 arm64 U-Boot FIT
（一个 `os = "u-boot"`、`arch = "arm64"`、且没有其他架构镜像的映像），或者 eMMC 槽位里已经有一个。
否则就删除对应的 Boot#### 变量，这样它也会从 BootOrder 里消失。没插卡时 SD 磁盘没有介质，
所以检查时不会从它读任何东西，只读 eMMC 的一两个扇区；只有找到了 U-Boot，才会读 1 MiB 的 SPL。
启动后才插入的卡，要到下一次启动才会显示这个启动项。

在文件系统里查找 `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` 不属于这个检查
（那样每次启动都要遍历所有文件系统的目录）。所以如果 Armbian 只在 eMMC 的某个分区里，
没有卡、没有 USB 磁盘、槽位也是空的，就不会出现这个启动项。一旦槽位里有了 U-Boot
（从 USB 磁盘复制进去的），它就会出现。

这个启动项不是 active 状态，所以启动顺序永远不会自动选它，它也没有快捷键。
它被加在 BootOrder 的末尾，如果有什么把它挪到了前面，它会被挪回末尾。
只有发生变化时才会写变量。也可以通过 BootNext 选中它（在经本固件启动的 Linux 里用
`efibootmgr -n`，并且打开"Save OS Variable Changes to eMMC"（把操作系统改的变量保存到 eMMC））。

应用程序会：

1. 找到 eMMC。如果 64 扇区的 SPL（eMMC 上的或 SD 卡上的）不含标记 `ArmbianOnce/1`，
   就拒绝执行，这样旧的 SPL 不会留下一个请求，让后来的 SPL 去执行；
2. 选择目标：
   * SD 卡 0x4000 扇区有外来 FIT：目标 1；
   * eMMC 0x4000 扇区有外来 FIT（这时本固件是从 SD 卡运行的）：目标 3；
   * 否则把 Armbian 的 FIT 复制到 eMMC 槽位（目标 2），来源按以下顺序：另一块整盘
     （USB、NVMe）的 0x4000 扇区，或者在它能读的任何文件系统上（固件里有 Ext4Dxe）
     找到的最新的 `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin`，取该文件偏移
     `(0x4000 - 64) * 512` 处的 FIT。FIT 的长度根据其中各镜像的
     `data-offset`/`data-position` 和 `data-size` 算出。只有槽位内容不同时才写入，
     并且每一块都读回校验；
3. 写入请求记录，读回确认，然后重启（冷重启；对于存在 eMMC 上的记录，复位类型无所谓）。

任何一步失败都会在写记录之前停下，并说明原因。

## SPI NOR 板子（Radxa ROCK 4D）

ROCK 4D 没有 eMMC：它的 SPL 和固件 FIT 都在 SPI NOR 里（idblock 在 0x8000，FIT 在
0x60000），SPL 的启动顺序是 `"same-as-spl"`（SPI NOR），然后是 SD 卡。SPL 写 SPI NOR
代价不小，而请求必须在 SPL 加载任何东西之前由它擦掉，所以请求改放在 SD 卡上，同样是
0x3FFF 扇区，它在 Armbian SD 映像里也处于全零的空隙中。两个开关，默认都关：

* SPL：`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_SD_REQUEST`（`uboot/v2026.10/0002-...patch`）。
  记录在 SD 卡上读取和擦除，只接受目标 1，即这张卡 0x4000 扇区的 FIT。有请求的那次启动，
  `spl_spi_get_uboot_offs()` 返回 0，SPI NOR 在那里放的是分区表，没有 FIT；由于
  `SPL_RAW_IMAGE_SUPPORT` 关闭，SPL 拒绝这个载荷，转向 SD 卡，MMC 钩子在那里让它加载卡上的
  FIT。没有请求时 SPI 偏移是 `CONFIG_SYS_SPI_U_BOOT_OFFS`（0x60000），所以正常启动就是
  SPL 普通的 SPI 启动。如果请求擦除后卡上没有 U-Boot FIT，SPL 会打印出来并启动固件。
* EDK2：`PcdArmbianOnceRequestOnSd`（RockchipPkg.dec）。启动项和应用程序在 SPI NOR 里找
  SPL 标签（通过 NOR flash 驱动读 0x8000..0x5FFFF），只在 SD 卡 0x4000 扇区找 U-Boot FIT，
  并把请求写到 SD 卡的 0x3FFF 扇区。没有槽位，也不从 USB、NVMe 或文件系统复制：SPL
  除了这张卡读不到别处的槽位，而在卡上 0x10000 扇区位于 Armbian 的根分区之内。

对 ROCK 4D，`boards/rock4d.conf` 让 `build-spl.sh` 用 U-Boot v2026.10 自带的
`rock-4d-rk3576_defconfig` 和设备树（不改动），加上两个补丁和
`uboot/v2026.10/rock4d/spl.config`（v1.12 板子的 XMC flash 支持、`SPL_MMC_WRITE`、
上面两个开关），以及 `binaries/idblock_mainline.bin`（U-Boot 2026.04）所带的 DDR blob v1.09。
这会替换每个 ROCK 4D 映像里的 SPL，所以在有板子启动过它之前，它只在分支 `rock4d-parity` 上。

## 用这种方法能启动什么

SPL 是主线 U-Boot 的；凡是 `spl_load_simple_fit()` 能加载的 FIT 它都能加载：
TF-A 作为 `firmware`（`os = "arm-trusted-firmware"`），U-Boot 和可选的 OP-TEE
（`os = "tee"`，由 `spl_atf.c` 作为 BL32 传给 BL31）作为 loadables，一个 U-Boot DT，
哈希可以是 SHA-1、SHA-256 或 CRC-32。外置数据和内嵌数据都可以。运行的是 FIT 自带的 BL31。

里面的 U-Boot 能不能正常工作，取决于它对前面各阶段有什么要求：

* **主线 U-Boot（泰山派 3M 的 Armbian edge 和 vendor，CM5-IO 的 Armbian edge）。**
  Armbian 用同一个标签（v2026.10）、同样的 defconfig 和 DTS、同样的 DDR blob（rkbin v1.08）
  构建它，BL31 用自己的 rkbin BL31 v1.20。它的 SPL 就是我们的 SPL 去掉 SD 优先顺序和这个补丁，
  所以它拿到的交接信息和它预期的完全一样：PMU1 GRF OS_REG2 到 5 里的 DRAM 大小、
  位于 DRAM + 2 MiB - 8 KiB 的 DDR_MEM ATAG（两者都由 DDR blob 写入），以及
  `spl-boot-order.c` 写入 `/chosen` 的 `u-boot,spl-boot-device`。这个组合已经跑过：
  2026-11-16，SD 优先的 SPL 从一张普通的 Armbian 卡启动了 Armbian 的 U-Boot。
  Armbian 的 FIT 里没有 OP-TEE。
* **不同的 DDR blob。** DRAM 由我们的 blob 训练；为其他 blob 版本构建的 U-Boot
  读的是同样格式的 OS_REG 和 ATAG，所以应该没关系，但没有测试过。
* **Rockchip 的厂商 U-Boot（2017.09 next-dev 及其分支）。** 它要求厂商 SPL 提供的 ATAG
  （启动设备、串口、TOS 内存），通常还要求厂商的 `uboot.img`/`trust.img` 布局。不支持；
  可能部分能用。Armbian 在这两块板子上不用它。CM5-IO 的 vendor 分支用的是 Radxa 的
  `next-dev-v2024.10`，它基于主线；未试过。
* **需要它自己的 TPL 或 SPL 做过某些事情的 U-Boot**（Falcon 模式、VBE 状态、
  留下不同 DRAM 或时钟状态的 TPL、SPL DT 里的安全启动密钥）不支持。

## 支持情况

| Armbian 在哪里 | SPL 从哪里加载 Armbian 的 U-Boot | 是否跟着 Armbian 升级 | 状态 |
|---|---|---|---|
| SD 卡，固件在 eMMC 上 | SD 卡 0x4000 扇区，原地加载 | 总是：用的就是 Armbian 自己的那份 | 主要场景，泰山派 3M |
| eMMC，固件在 SD 卡上（SPL 在 eMMC 上） | eMMC 0x4000 扇区，原地加载 | 总是 | 支持 |
| USB 磁盘 | eMMC 槽位，每次使用时从磁盘的 0x4000 扇区复制 | 是，每次使用时 | 支持，前提是 Armbian 的 U-Boot 能在 USB 上找到 `boot.scr`（它的 bootflow 会扫描 USB）。U-Boot 下的 USB-C 没有 FUSB302 驱动；请用 USB-A |
| NVMe | eMMC 槽位，同 USB | 是，每次使用时 | 代码路径相同；泰山派没有 M.2，所以未试过 |
| 和固件在同一块 eMMC 上的某个分区 | eMMC 槽位，从 `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` 复制 | 是，每次使用时：每次 apt 升级 `linux-u-boot-*` 都会替换这个文件 | 支持。Armbian 绝不能把它的 bootloader 写到那里（见 R5） |
| 固件也在 0x4000 处时，Armbian 的 FIT 在同一块盘上 | 不可能：一个扇区位置只能放一个 FIT | | |
| ROCK 4D：SD 卡，固件在 SPI NOR 上 | SD 卡 0x4000 扇区，原地加载 | 总是 | 在分支 `rock4d-parity` 上已构建，未上板运行；见 [SPI NOR 板子](#spi-nor-板子radxa-rock-4d)。那里不支持 USB 或 NVMe 上的 Armbian：没有 eMMC 可放槽位 |
| CM5-IO | 设计上适用（eMMC 上的 SPL） | | 没有编进去：它的 SPL（带底板 overlay 的 2026.07-rc3）还没有用 `build-spl.sh` 重新构建，而且 SD 卡场景需要 SD 优先的启动顺序 |

Armbian 的 U-Boot 软件包只有在 `FORCE_UBOOT_UPDATE=yes`（它的 `postinst`）或通过
`armbian-install` 时才会把 bootloader 写到磁盘上。所以 Armbian SD 卡或 USB 磁盘
0x4000 处的 FIT，是映像或用户最后一次放进去的那个，ArmbianOnce 就用它，不管是不是最新的。
那是 Armbian 自己的状态，符合 G4。

## 风险

* **R1 行为变化**：当 eMMC 上有本固件时，Armbian SD 卡插在卡槽里不再会自动启动。
  这就是 G2。跳过 SD 卡的前提是 eMMC 上有本固件，所以如果板子的 eMMC 上没有固件 FIT，
  卡仍然可以救援。
* **R2 在这条路径上，Armbian 的 U-Boot 启动命令被覆盖了。** 2026-11-16 实测：
  eMMC 上装着 Windows 时，Armbian 的主线 U-Boot 通过它的 EFI 启动管理器
  （U-Boot 会为每个带 ESP 的磁盘生成启动项）启动了 eMMC 上的 Windows Boot Manager，
  然后卡住，根本没走到卡上的 `boot.scr`。`/config/bootcmd` 把它限制在 `script` 和
  `extlinux` 两种 bootmeth。U-Boot 二进制是 Armbian 的，只有这条命令不同。
  通过 U-Boot 的 EFI 启动的 Armbian 映像在这条路径上启动不了（反正它可以通过本固件启动）。
  把 Kconfig 字符串设为空，就能用 Armbian 自己的 `bootcmd`。
* **R3 SPL 会写 eMMC**，只写一个扇区，而且只在有请求时写。
  如果写的过程中断电，留下的扇区 CRC 校验不通过：视为没有请求。
* **R4 槽位**只对照分区表检查，不检查分区之外的数据。
  在本项目发布或文档中提到的所有布局里，32 到 48 MiB 都没有分区，也没有被使用。
* **R5 Armbian 和固件在同一块 eMMC 上**：`armbian-install`、`armbian-config` 的
  bootloader 更新或 `FORCE_UBOOT_UPDATE=yes` 会把 `u-boot-rockchip.bin` 写到 64 扇区，
  覆盖掉固件。我们这边没有办法阻止。
* **R6 Armbian 的 U-Boot 里卡住**时需要断电重启（没有看门狗）；
  之后那次启动就是本固件。
* **R7** 现在 SPL 每次启动都会初始化 eMMC，固件在 SD 卡上时也一样（多几毫秒）。
* **R8** 在泰山派上，从 Windows 设置的 BootNext 在复位后保留不下来
  （板子没有 RTC 电池，每次复位 RTC 都会断电，见[硬件](dev/hardware.zh-CN.md#立创泰山派-3m)），所以不能从 Windows
  "重启进入 Armbian"；从启动管理器可以。

## 构建

```bash
scripts/build.sh tspi3m
scripts/build-spl.sh tspi3m     # clones U-Boot v2026.10 into third_party/u-boot once
scripts/package.sh tspi3m       # picks up out/TaishanPi3M/idbloader.img
```

`UBOOT_DIR` 指向一个已有的 U-Boot 克隆，`RKBIN_DIR` 指向一个放着 DDR blob 的目录，
这样就不用下载。不运行 `build-spl.sh` 时，映像里用的是 `binaries/idblock_tspi3m.bin`，
启动项会提示 SPL 不支持这个功能。

升级 U-Boot 时：把 `uboot/v2026.10` 复制成新版本的目录，从 Armbian 的
`patch/u-boot/<version>` 更新板子文件，rebase 补丁（它只改
`arch/arm/mach-rockchip/{Kconfig,Makefile}` 并新增一个文件），然后修改
`boards/tspi3m.conf` 里的 `SPL_UBOOT_TAG`/`SPL_UBOOT_COMMIT`/`SPL_PATCH_DIR`/`SPL_BOARD_DIR`。

## 板上测试（泰山派 3M）

需要：一张泰山派 3M 用的 Armbian SD 卡（主线 U-Boot 映像；2026-11-16 那张卡就是），
1500000 波特率的串口控制台，能进入 MaskROM。这个构建的 nvdata 偏移没有变
（`0x1550000`，扇区 43648），下面的更新只写 SPL 和固件那一段，所以 UEFI 变量会保留。

1. 在 MaskROM 下刷写（或者在运行中的 Linux 里用 `dd` 写同样的扇区）：

   ```bash
   dd if=TaishanPi3M-emmc.img of=spl.bin bs=512 skip=64 count=400
   dd if=TaishanPi3M-emmc.img of=firmware.bin bs=512 skip=16384 count=27264
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 spl.bin
   rkdeveloptool wl 16384 firmware.bin
   rkdeveloptool rd
   ```

2. 不插卡，冷启动三次：出现 `U-Boot SPL 2026.10-edk2-rk3576`，没有
   `ArmbianOnce/1` 开头的行，固件和 Windows 和以前一样。启动管理器里有 `Armbian (U-Boot)`。
3. 插卡，重启：出现 `ArmbianOnce/1: not booting the U-Boot FIT on the SD card,
   the firmware is on the eMMC`，然后进入固件。（没有这项改动时，这时会从卡启动。）
4. 在启动管理器里选 `Armbian (U-Boot)`：出现 `Found a U-Boot FIT on the SD card.`、
   `Request written. Restarting`。下一次启动：出现 `ArmbianOnce/1: request found and
   erased: booting U-Boot from the SD card, once`、`ArmbianOnce/1: U-Boot will
   run "setenv bootmeths script extlinux; bootflow scan -lb"`，然后是 Armbian 的
   `U-Boot 2026.10` 横幅（带它自己的构建日期）、`boot.scr`、Armbian。
   在 Armbian 里：`/sys/firmware/efi` 不存在，`/proc/device-tree/model` 和
   `/proc/cmdline` 是 Armbian 的，并且
   `dd if=/dev/mmcblk0 bs=512 skip=16383 count=1 status=none | tr -d '\0' | wc -c`
   输出 0（换成 eMMC 的设备名；插着卡时可能不一样）。
5. 只生效一次：在 Armbian 里 `sudo reboot`：进入固件。再选一次这个启动项，
   然后在 Armbian 里 `echo b | sudo tee /proc/sysrq-trigger`：进入固件。再选一次，
   在 Armbian 的 U-Boot 运行时拔电：进入固件。
   第 4 步总共至少重复三次。
6. 拔卡，启动：启动管理器里没有 `Armbian (U-Boot)`（BootOrder 里也没有）。
   插卡，启动：它又出现了，排在列表最后。
7. 发出请求后拔卡：选这个启动项，在屏幕显示 `Restarting` 时拔出卡。SPL 输出
   `request found and erased: booting U-Boot from the SD card, once`，然后是
   `U-Boot was not loaded from the SD card, booting the firmware`，接着从 eMMC 启动固件；
   扇区 16383 为零，下一次启动是正常启动。
8. 可选，槽位路径：拔卡，在 USB-A 口插一个 Armbian USB 磁盘，选这个启动项：出现
   `Found a U-Boot FIT on ...`、`Copying it to the eMMC slot`，SPL 输出
   `booting U-Boot from the eMMC slot, once`；Armbian 的 U-Boot 能不能找到这块盘还是未知的部分。
   第二次使用时输出 `The eMMC slot already holds that U-Boot.`。把 USB 磁盘也拔掉后，
   启动项仍然在（槽位里有 U-Boot），应用程序会显示 `Using the U-Boot copied to the eMMC slot
   earlier`。

把第 3 到 5 步的串口日志发回来；SPL 输出的那些行就是证据。
