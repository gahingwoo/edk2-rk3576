# edk2-rk3576 — UEFI for Radxa ROCK 4D (Rockchip RK3576)

[![Status](https://img.shields.io/badge/status-experimental-orange)]()
[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![Board](https://img.shields.io/badge/board-Radxa%20ROCK%204D-green)]()

A working **EDK2 / TianoCore UEFI** port for the **Radxa ROCK 4D** (Rockchip RK3576),
plus the matching mainline-style **U-Boot** SPL/TPL stack used as the BL33
loader. Verified on real hardware (12 GB LPDDR5 SKU).

> **Status:** boots to UEFI Shell over UART, mass-storage / USB / Ethernet
> functional. Display (HDMI / DSI) is not yet implemented for RK3576 — see
> [Known limitations](#known-limitations).

---

## Repository layout

```
.
├── binaries/                ← Pre-built, hardware-verified firmware
│   ├── u-boot.itb           ← U-Boot FIT (BL31 + U-Boot proper + DTB)
│   └── checksums.sha256
├── rock4d-spi-edk2.img      ← Ready-to-flash 16 MB SPI NOR image (UEFI build)
├── edk2_port/               ← Source overlay
│   ├── build_rock4d_uefi.sh ← One-shot build script
│   ├── patch_tools_def.py   ← BaseTools tools_def.txt patcher
│   ├── rock4d.dts           ← Working device-tree source
│   ├── configs/             ← Board / SoC build configs
│   ├── devicetree/vendor/   ← Compiled board DTB
│   ├── misc/                ← extractbl31.py, mkimage, FIT *.its templates
│   ├── RK3576/              ← (legacy top-level mirror; see note below)
│   ├── ROCK4D/              ← (legacy top-level mirror; see note below)
│   └── edk2-rockchip/edk2-rockchip/   ← ACTIVE overlay used by the build
│       ├── Platform/Radxa/ROCK4D/     ← Board package
│       └── Silicon/Rockchip/RK3576/   ← SoC silicon package
└── .gitignore
```

> **Note on duplicates.** `edk2_port/RK3576/` and `edk2_port/ROCK4D/` at the top
> level are an early scaffolding mirror; the build actually consumes
> `edk2_port/edk2-rockchip/edk2-rockchip/Platform/Radxa/ROCK4D/` and
> `…/Silicon/Rockchip/RK3576/`. Edit those for changes that affect the build.

The upstream third-party trees (`edk2`, `edk2-non-osi`, `edk2-platforms`,
`edk2-rockchip-non-osi`, `arm-trusted-firmware`, `rkbin`) are **not** vendored
in this repo — they are fetched at build time. See
[Building from source](#building-from-source).

---

## Hardware verified

| Item                | Result                                       |
|---------------------|----------------------------------------------|
| LPDDR5              | 2736 MHz, dual-channel, 12 GB OK             |
| TF-A BL31           | v2.14.0 loaded at 0x00040000 (EL3)           |
| U-Boot              | 2026.04, EL2 (BL33)                          |
| RK806 PMIC          | I²C0 enumerated                              |
| eMMC / SD           | Boot + read/write OK                         |
| SPI NOR (16 MB)     | Boot + read/write OK                         |
| USB host / Ethernet | OK                                           |
| EDK2 UEFI Shell     | OK over UART (1.5 Mbaud 8N1)                 |

Serial console: **`1500000 8N1`** on the 3-pin debug header.

---

## Quick start — flash the prebuilt firmware

### Option A — SD card / eMMC (U-Boot only, no UEFI)

```bash
# Use the FIT directly with idbloader, OR use a packaged image
# (see Radxa upstream for the SD packaging recipe).
```

### Option B — SPI NOR (UEFI, persistent) via MaskROM

```bash
# Put the board in MaskROM (hold the MaskROM button while powering on)
rkdeveloptool db   binaries/rk3576_ddr.bin    # download DDR init blob
rkdeveloptool wl 0 rock4d-spi-edk2.img        # write the 16 MB SPI image
rkdeveloptool rd                              # reboot
```

After flashing you should see TF-A → U-Boot → EDK2 banner on UART.

---

## Building from source

### 1. Toolchain

* GCC AArch64 cross-compiler (`gcc-aarch64-linux-gnu`) — or build natively on an AArch64 host
* Python ≥ 3.10
* `mkimage` (a vendored copy ships under `edk2_port/misc/tools/`)
* Standard EDK2 build deps: `build-essential nasm uuid-dev acpica-tools`

### 2. Clone third-party sources

```bash
cd edk2_port

# EDK2 (pin to the version we tested against)
git clone https://github.com/tianocore/edk2.git edk2
( cd edk2 && \
  git checkout 46548b1adac82211d8d11da12dd914f41e7aa775 && \
  git submodule update --init --depth=1 \
      MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
      MdePkg/Library/MipiSysTLib/mipisyst \
      MdePkg/Library/BaseFdtLib/libfdt \
      CryptoPkg/Library/OpensslLib/openssl \
      CryptoPkg/Library/MbedTlsLib/mbedtls )

git clone --depth=1 https://github.com/tianocore/edk2-non-osi.git
git clone --depth=1 https://github.com/tianocore/edk2-platforms.git
```

### 3. Build

```bash
cd edk2_port
bash build_rock4d_uefi.sh
```

The script:

* Detects host architecture (AArch64 / x86_64) and sets `GCC_AARCH64_PREFIX`
* Rebuilds **BaseTools** locally (mandatory on AArch64 hosts)
* Applies GCC 10–13 compatibility patches (`-Wimplicit-function-declaration`,
  LTO removal, stack-protector workaround)
* Symlinks `/Scripts/GccBase.lds` (required by the EDK2 GCC linker script)
* Builds the `Platform/Radxa/ROCK4D/ROCK4D.dsc` package (~15–30 min)
* Packs **BL31 + EDK2 + DTB** into a FIT image
* Assembles the final 16 MB SPI NOR image as `rock4d-spi-edk2.img`

### 4. Flash

```bash
# MaskROM mode
rkdeveloptool db   ../binaries/rk3576_ddr.bin
rkdeveloptool wl 0 rock4d-spi-edk2.img
rkdeveloptool rd
```

---

## SPI NOR layout (16 MB)

| Offset      | Content                                                          |
|-------------|------------------------------------------------------------------|
| `0x000000`  | GPT partition table                                              |
| `0x008000`  | idblock (DDR init + SPL)                                         |
| `0x100000`  | FIT image — TF-A BL31 (→ `0x00040000`, EL3), EDK2 (→ `0x00200000`, BL33), Board DTB |

---

## Known limitations

* **No display output yet.** RK3576 HDMI / VOP2 has its own GRF map and is
  not covered by the existing RK3588 display drivers. There is also no
  generic simple-framebuffer / `GenericGop` fallback in upstream
  edk2-rockchip. Patches welcome.
* **ACPI tables are stubs**, the firmware boots in **FDT** mode by default.
* **EDK2 must be pinned** to commit `46548b1`; newer cores have API changes
  that break the rockchip overlay.
* On **AArch64 hosts**, BaseTools must be rebuilt with the system
  `gcc` / `g++` — the `bin/` checked into upstream EDK2 is x86_64.

---

## Credits

* [TianoCore EDK2](https://github.com/tianocore/edk2) — UEFI reference implementation
* [edk2-rockchip](https://github.com/edk2-porting/edk2-rk3588) — RK3588 port we
  used as the structural template for the RK3576 silicon package
* [Trusted Firmware-A](https://www.trustedfirmware.org/projects/tf-a/) — BL31
* [Radxa](https://radxa.com/products/rock4/4d/) for the hardware
* [Rockchip](https://www.rock-chips.com/) for the SoC and DDR init blobs

---

## License

Source code in this repository follows the licenses of its upstream origins:

* EDK2 platform / silicon code → **BSD-2-Clause-Patent** (TianoCore)
* TF-A binaries (`bl31.elf`) → **BSD-3-Clause**
* Rockchip DDR init blob (`rk3576_ddr.bin`) → **Rockchip proprietary**, redistributable
* Build scripts and original integration glue in this repo → **BSD-2-Clause-Patent**

See individual files for headers.
