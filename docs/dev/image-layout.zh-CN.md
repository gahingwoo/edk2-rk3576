# 映像布局

[English](image-layout.md) | 简体中文

本页的表格由 `scripts/gen-docs` 根据 `scripts/package.sh`、`Silicon/Rockchip/RK3576/RK3576.fdf`
以及 `scripts/lib/gen_fit_its.py` 和 mkimage 生成的 FIT 自动生成；和代码不一致时 CI 会失败。

## ROCK 4D：16 MB SPI NOR

<!-- generated:layout-spi -->

| 偏移 | 内容 |
|---|---|
| `0x000000` | GPT（`binaries/rk3576_spi_nor_gpt.img`） |
| `0x008000` | idblock：DDR 初始化 + U-Boot SPL |
| `0x060000` | FIT：BL31、EDK2、DTB，最多到 `0xFC0000` |
| `0xFC0000` | UEFI 变量存储，64 KB |
| `0xFD0000` | FTW 工作块，64 KB |
| `0xFE0000` | FTW 备用块，64 KB |

<!-- /generated -->

这三个 NV 区域出厂时是擦除状态（全为 `0xFF`）。如果那里是全零，会被当成损坏的存储而不是空存储，变量永远不会初始化。
在 SPI NOR 上，变量驱动直接读写闪存。

## CM5-IO 和泰山派 3M：SD 卡或 eMMC，32 MB 映像

<!-- generated:layout-mmc -->

| 扇区 | 字节偏移 | 内容 |
|---|---|---|
| 64 | `0x008000` | idblock：DDR 初始化 + U-Boot SPL |
| 16384 | `0x800000` | FIT：BL31、EDK2、DTB 和 UEFI 变量存储 |
| 43648 | `0x1550000` | FIT 内部的最后一个映像 `nvdata`：UEFI 变量存储，192 KB（存储区、FTW 工作块、FTW 备用块），擦除状态（全 `0xFF`） |

<!-- /generated -->

在 SD 和 eMMC 上，变量存储作为 FIT 的最后一个映像 `nvdata` 放在 FIT 里。每次启动时，SPL 把它加载到
`PcdFlashNvStorageVariableBase64`（<!-- generated:nvdata-load -->0x415C0000<!-- /generated -->），RkFvbDxe 再把改动写回卡上同样的字节。
在这些介质上，保存的变量只能通过这条路回到内存。`package.sh` 从 `RK3576.fdf` 取得地址和大小，找到 mkimage
把 `nvdata` 放在哪里，并检查映像里这些字节全是 `0xFF`。

所以，想保留设置的写入要停在 `nvdata` 扇区之前（[刷写固件](../flash.zh-CN.md#只更新固件保留其他内容)）。
网页刷写工具会读取映像里的 FIT，如果 `nvdata` 不在它预期的位置，就拒绝这种写入。

`<板子>-emmc-edk2-<版本>.img` 是同样的字节，前面加了保护性 MBR 和主 GPT：

<!-- generated:layout-emmc-gpt -->

| 扇区 | 内容 |
|---|---|
| 0 到 33 | 保护性 MBR 和主 GPT |
| 64 到 131071 | p1 `firmware`（64 MiB，没有文件系统，类型 `8DA63339-0007-60C0-C436-083AC8230908`） |
| 131072 到末尾 | p2 `data`，未格式化，eMMC 的其余部分 |

<!-- /generated -->

映像只到 32 MiB，所以在板子上运行 `sgdisk -e` 之前，备份 GPT 是缺失的。`<板子>-emmc.sfdisk` 是单独的同一张分区表。

## FIT 内容

<!-- generated:fit-contents -->

| 映像 | 加载地址 | 大小 | 内容 |
|---|---|---|---|
| `atf-1` | `0x3fe70000` | 8 KB | BL31 段：PMUSRAM |
| `atf-2` | `0x40040000` | 116 KB | BL31 段：main code + entry |
| `atf-3` | `0x4005d000` | 7 KB | BL31 段：coherent data |
| `fdt` | 无 | 182 KB | SPL 用的 DTB |
| `edk2` | `0x40800000` | 13312 KB | EDK2，作为 BL33；必须和 TF-A 的 `BL33_BASE` 一致 |
| `nvdata` | `0x415c0000` | 192 KB | UEFI 变量存储（只在 SD 和 eMMC 映像里） |

<!-- /generated -->

RK3576 的 DRAM 从 `0x40000000` 开始。FIT 用外部数据方式构建（`mkimage -E -B 0x1000`）：一个 4 KB 的头，后面跟着各个载荷。

## 启动流程

```
BootROM
  -> idblock (DDR init, then SPL)
       -> SPL loads the FIT
            -> BL31 at 0x40040000, EL3
            -> EDK2 at 0x40800000, EL2 (BL33)
                 -> UEFI front page and boot manager
```
