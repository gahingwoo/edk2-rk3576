# 映像布局

[English](image-layout.md) | 简体中文

这里的偏移就是 [`scripts/package.sh`](https://github.com/gahingwoo/edk2-rk3576/blob/main/scripts/package.sh) 写入的偏移。

## ROCK 4D：16 MB SPI NOR

| 偏移 | 内容 |
|---|---|
| `0x000000` | GPT（来自 `binaries/rk3576_spi_nor_gpt.img`） |
| `0x008000` | idblock：DDR 初始化 + U-Boot SPL，约 190 KB |
| `0x060000` | FIT：BL31、EDK2、DTB。最多可以增长到 `0xFC0000`。 |
| `0xFC0000` | UEFI 变量存储，64 KB |
| `0xFD0000` | FTW 工作块，64 KB |
| `0xFE0000` | FTW 备用块，64 KB |

这三个 NV 区域出厂时是擦除状态（全为 `0xFF`）。如果那里是全零，会被当成损坏的存储而不是空存储，变量永远不会初始化。在 SPI NOR 上，变量驱动直接读写闪存。

## CM5-IO 和泰山派 3M：SD 卡或 eMMC，32 MB 映像

| 扇区 | 字节偏移 | 内容 |
|---|---|---|
| 64 | `0x008000` | idblock：DDR 初始化 + U-Boot SPL |
| 16384 | `0x800000` | FIT：BL31、EDK2、DTB 和 UEFI 变量存储 |
| 43648 | `0x1550000` | 在 FIT 内部：映像 `nvdata`，即 UEFI 变量存储，192 KB（存储、FTW 工作块、FTW 备用块），擦除状态 |

在 SD 和 eMMC 上，变量存储作为 FIT 的最后一个映像 `nvdata` 放在 FIT 里。每次启动时，SPL 把它加载到 `PcdFlashNvStorageVariableBase64`（`0x415C0000`），RkFvbDxe 再把改动写回卡上同样的字节。在这些介质上，保存的变量只能通过这条路回到内存。`package.sh` 从 `RK3576.fdf` 取得地址和大小，找到 mkimage 把 `nvdata` 放在哪里，并检查映像里这些字节全是 `0xFF`。

所以，想保留设置的写入要停在 43648 扇区之前："保留分区"的写法是把 eMMC 映像的 64 到 43647 扇区写到 64 扇区（[刷写固件](../flash.zh-CN.md#只更新固件保留其他内容)）。网页刷写工具会读取映像里的 FIT，如果 `nvdata` 不是从 43648 扇区开始，就拒绝这种写入。

`<Board>-emmc-edk2-<ver>.img` 是同样的字节，前面加了保护性 MBR 和主 GPT：

| 扇区 | 内容 |
|---|---|
| 0 到 33 | 保护性 MBR 和主 GPT |
| 64 到 131071 | p1 `firmware`（64 MiB，没有文件系统） |
| 131072 到末尾 | p2 `data`，未格式化，占 eMMC 剩下的空间 |

映像只到 32 MiB，所以在板子上运行 `sgdisk -e` 之前，备份 GPT 是缺失的。`<Board>-emmc.sfdisk` 是单独的同一张分区表。

## FIT 内容

| 映像 | 加载地址 | 内容 |
|---|---|---|
| `atf-1` | `0x3fe70000` | BL31，PMU SRAM 段 |
| `atf-2` | `0x40040000` | BL31 主代码；配置的入口点 |
| `atf-3` | `0x4005d000` | BL31 一致性数据 |
| `fdt` | 无 | SPL 使用的 DTB |
| `edk2` | `0x40800000` | 作为 BL33 的 EDK2。必须和 TF-A 的 `BL33_BASE` 一致。 |
| `nvdata` | `0x415c0000` | UEFI 变量存储（仅 SD 和 eMMC 映像） |

RK3576 的 DRAM 从 `0x40000000` 开始，所以除了 PMU SRAM 段，每个加载地址都紧挨在这个基址之上。FIT 用外部数据方式构建（`mkimage -E -B 0x1000`）：一个 4 KB 的头，后面跟着各个载荷。ITS 由 [`scripts/lib/gen_fit_its.py`](https://github.com/gahingwoo/edk2-rk3576/blob/main/scripts/lib/gen_fit_its.py) 生成。

## 启动流程

```
BootROM
  -> idblock (DDR init, then SPL)
       -> SPL loads the FIT
            -> BL31 at 0x40040000, EL3
            -> EDK2 at 0x40800000, EL2 (BL33)
                 -> UEFI front page and boot manager
```
