# Repository layout

```
.
├── binaries/                        ← Pre-built, hardware-verified firmware
│   ├── bl31.elf                     ← TF-A BL31 v2.14.0
│   ├── rk3576_bl31_vendor.elf       ← Rockchip vendor BL31 (fallback)
│   ├── rk3576_ddr.bin               ← LPDDR5 init blob v1.09
│   ├── u-boot-spl.bin               ← U-Boot SPL (idbloader)
│   ├── u-boot.itb                   ← U-Boot FIT (BL31 + U-Boot + DTB)
│   ├── u-boot-rockchip-spi.bin      ← U-Boot SPI image
│   ├── rock4d-sd-uboot.img          ← SD/eMMC image (U-Boot only)
│   ├── rock4d-spi-uboot.img         ← SPI image (U-Boot only)
│   └── checksums.sha256
├── rock4d-spi-edk2.img              ← 16 MB SPI NOR image with UEFI
├── docs/                            ← Documentation (this folder)
├── edk2_port/                       ← Source overlay
│   ├── build_rock4d_uefi.sh         ← One-shot build script
│   ├── patch_tools_def.py           ← BaseTools tools_def.txt patcher
│   ├── rock4d.dts                   ← Working device-tree source
│   ├── configs/                     ← Board / SoC build configs
│   │   ├── RK3576.conf
│   │   └── rock-4d.conf
│   ├── devicetree/vendor/           ← Compiled board DTB
│   ├── misc/                        ← Build helpers
│   │   ├── extractbl31.py           ← BL31 ELF segment extractor
│   │   ├── gen_fit_its.py           ← FIT descriptor generator
│   │   ├── uefi_rk3576.its          ← FIT image template
│   │   ├── rk3576_spl.dtb           ← SPL DTB
│   │   ├── rk3588_spi_nor_gpt.txt   ← GPT layout (SPI NOR)
│   │   └── tools/{aarch64,x86_64}/mkimage
│   ├── RK3576/                      ← (legacy mirror — see note)
│   ├── ROCK4D/                      ← (legacy mirror — see note)
│   └── edk2-rockchip/edk2-rockchip/ ← ACTIVE overlay used by the build
│       ├── Platform/Radxa/ROCK4D/   ← Board package
│       │   ├── ROCK4D.dsc           ← Platform DSC
│       │   ├── ROCK4D.Modules.fdf.inc
│       │   ├── Library/RockchipPlatformLib/
│       │   │       └── RockchipPlatformLib.c   ← PMIC / GPIO / PCIe / HdmiTxIomux
│       │   ├── DeviceTree/Vendor.inf
│       │   └── AcpiTables/
│       └── Silicon/Rockchip/RK3576/ ← SoC silicon package
│           ├── RK3576.dec           ← Package declaration + PCDs
│           ├── RK3576.fdf           ← Flash layout
│           ├── Include/Soc.h        ← All MMIO addresses
│           ├── Drivers/RK3576Dxe/   ← SoC DXE driver
│           └── Library/SdramLib/    ← DRAM detection (PMU1_GRF)
├── README.md
└── .gitignore
```

## Note on the duplicate overlays

`edk2_port/RK3576/` and `edk2_port/ROCK4D/` at the top level are early
scaffolding mirrors. **The build does not consume them.** The active overlay
is the one inside `edk2_port/edk2-rockchip/edk2-rockchip/`. See
[KNOWN_ISSUES.md](KNOWN_ISSUES.md#duplicate-overlay-copies) for details.

## What is NOT in this repo

The upstream third-party trees (cloned at build time, see
[BUILDING.md](BUILDING.md)):

* `edk2/`                          — TianoCore EDK2 (pinned to commit `46548b1`)
* `edk2-non-osi/`                  — TianoCore non-OSI assets
* `edk2-platforms/`                — TianoCore platform repo
* `edk2-rockchip-non-osi/`         — Rockchip non-OSI binaries
* `arm-trusted-firmware/`          — TF-A source
* `misc/rkbin/`                    — Rockchip prebuilt blobs
* upstream `devicetree/` mirror    — 98 MB of Rockchip board DTBs
