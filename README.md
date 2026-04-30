# edk2-rk3576 — UEFI for Radxa ROCK 4D (Rockchip RK3576)

[![Status](https://img.shields.io/badge/status-experimental-orange)]()
[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![Board](https://img.shields.io/badge/board-Radxa%20ROCK%204D-green)]()
[![License](https://img.shields.io/badge/license-MIT%20%2B%20BSD--2--Clause--Patent-lightgrey)]()

A working **EDK2 / TianoCore UEFI** port for the **Radxa ROCK 4D**
(Rockchip RK3576), with a matching **TF-A BL31 + U-Boot** boot stack.
Verified on real hardware (12 GB LPDDR5 SKU).

> **Status:** boots to UEFI Shell over UART, mass-storage / USB / Ethernet
> functional. HDMI is **not yet implemented** for RK3576.

---

## Documentation → [`docs/`](docs/)

| Document                                       | What it covers                            |
|------------------------------------------------|-------------------------------------------|
| [docs/REPO_LAYOUT.md](docs/REPO_LAYOUT.md)     | What lives where in this repository       |
| [docs/HARDWARE.md](docs/HARDWARE.md)           | Hardware verification matrix, UART setup  |
| [docs/FLASHING.md](docs/FLASHING.md)           | Flashing the prebuilt firmware            |
| [docs/BUILDING.md](docs/BUILDING.md)           | Building the UEFI image from source       |
| [docs/SPI_LAYOUT.md](docs/SPI_LAYOUT.md)       | 16 MB SPI NOR layout & FIT contents       |
| [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)   | Limitations, gotchas and workarounds      |

---

## TL;DR

### Flash the prebuilt UEFI image (16 MB SPI NOR, MaskROM mode)

```bash
rkdeveloptool db   binaries/rk3576_ddr.bin
rkdeveloptool wl 0 rock4d-spi-edk2.img
rkdeveloptool rd
```

Serial console: **1,500,000 8N1** on the 3-pin debug header.
See [docs/FLASHING.md](docs/FLASHING.md) for details and recovery.

### Build from source

```bash
cd edk2_port
# Clone third-party trees once (see docs/BUILDING.md for the exact commands)
bash build_rock4d_uefi.sh
# Output: rock4d-spi-edk2.img
```

Full instructions in [docs/BUILDING.md](docs/BUILDING.md).

---

## What's in the box

* `binaries/` — pre-built, hardware-verified BL31 / U-Boot / DDR-init blobs
* `rock4d-spi-edk2.img` — ready-to-flash 16 MB SPI NOR UEFI image
* `edk2_port/` — the EDK2 source overlay:
  * `Platform/Radxa/ROCK4D/` — board package
  * `Silicon/Rockchip/RK3576/` — SoC silicon package
  * `build_rock4d_uefi.sh` — one-shot build script with all the GCC 10–13
    workarounds baked in

The upstream third-party trees (`edk2`, `edk2-non-osi`, `edk2-platforms`,
`arm-trusted-firmware`, `rkbin`) are **not** vendored — they are cloned at
build time. See [docs/BUILDING.md](docs/BUILDING.md).

---

## Credits

* [TianoCore EDK2](https://github.com/tianocore/edk2) — UEFI reference implementation
* [edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588) — structural template for the RK3576 silicon package
* [Trusted Firmware-A](https://www.trustedfirmware.org/projects/tf-a/) — BL31
* [Radxa](https://radxa.com/products/rock4/4d/) — hardware
* [Rockchip](https://www.rock-chips.com/) — SoC and DDR init blobs

---

## License

* Repository scaffolding, build scripts, README and docs → **MIT**
  (see [LICENSE](LICENSE))
* EDK2 platform / silicon overlay code → **BSD-2-Clause-Patent** (TianoCore)
* TF-A binaries (`bl31.elf`) → **BSD-3-Clause**
* `rk3576_ddr.bin` → **Rockchip proprietary**, redistributable

See individual file headers.
