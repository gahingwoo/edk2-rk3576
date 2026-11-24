# 预编译二进制文件

[English](README.md) | 简体中文

这里放的是构建的输入，不是构建的产物。`scripts/package.sh` 会读取这些文件；
这里没有任何文件是 `scripts/build.sh` 生成的。

| 文件 | 是什么 |
|---|---|
| `bl31.elf` | TF-A BL31 v2.14.0，开源。优先使用。 |
| `rk3576_bl31_vendor.elf` | Rockchip 的 BL31，没有 `bl31.elf` 时使用。 |
| `rk3576_ddr.bin` | LPDDR5 初始化 blob v1.09，由 SPL 使用。它**不是** `rkdeveloptool db` 要的文件：那个命令要的是 RKLD 格式的 loader（前四个字节是 `LDR `），本仓库里没有。 |
| `idblock_mainline.bin` | U-Boot SPL（idbloader），用 U-Boot e3405917a180 的 `rock-4d-rk3576_defconfig` 加上 `CONFIG_SPI_FLASH_XMC=y` 构建，DDR blob v1.09。如果有新构建的 `out/<BOARD>/idbloader.img`，`package.sh` 会优先用它，而不是这个文件。 |
| `idblock_cm5io.bin` | 带 CM5-IO 底板 overlay 的 U-Boot SPL 2026.07-rc3。`boards/cm5io.conf` 通过 `IDBLOCK_OVERRIDE` 固定使用它；`idblock_mainline.bin` 是 2026.04 版，比这个 overlay 更早。 |
| `idblock_tspi3m.bin` | 泰山派 3M 用的 U-Boot SPL 2026.10，用 `lckfb-tspi-3m-rk3576_defconfig` 和 DDR 初始化 blob v1.08 构建，只改了一处：在 `rk3576-lckfb-tspi-3m-u-boot.dtsi` 里设置 `u-boot,spl-boot-order = &sdmmc, "same-as-spl", &sdhci;`，让 SPL 先试 SD 卡，再试它自己所在的设备。构建时不能开 `CONFIG_SPL_GZIP`。插卡和不插卡都能让板子从 eMMC 启动。`scripts/build-spl.sh tspi3m` 用同样的输入（`uboot/v2026.10/tspi3m`）加上 `uboot/v2026.10` 里的补丁重新构建它，输出到 `out/TaishanPi3M/idbloader.img`，之后 `package.sh` 会改用这个文件；见 `docs/ARMBIAN-CHAINLOAD.md`。 |
| `rk3576_spi_nor_gpt.img` | 写在 16 MB SPI NOR 映像偏移 0 处的 GPT（ROCK 4D）。 |

`checksums.sha256` 覆盖所有 ELF 和 BIN 文件。

## 关于 GPT 映像

它是从一个 Rockchip parameter 文件生成的，那个文件里还写着
`MACHINE_MODEL: rk3588_s`。实际只用到它生成的分区表，而且分区表描述的布局和
RK3576 SPL 期望的一致，所以能用。但它没有为 RK3576 重新生成过，修改它之前最好先了解这个来历。
