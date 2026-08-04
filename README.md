# edk2-rk3576

[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![License](https://img.shields.io/badge/license-BSD--2--Clause--Patent-lightgrey)](LICENSE)
[![Flash](https://img.shields.io/badge/flash-WebUSB%20browser%20tool-informational)](https://gahingwoo.github.io/edk2-webflash/)

EDK2 / TianoCore UEFI firmware for Rockchip RK3576 boards.

```
BootROM → U-Boot SPL (idbloader) → TF-A BL31 → EDK2 (BL33) → OS
```

Both boards boot to the UEFI front page and load an OS. The ROCK 4D has run
Fedora 44 aarch64 through to a GNOME desktop.

## Screenshots

These are real captures from these boards, not mock-ups. **HDMI output is not
reliable** — see the caveat below the images and
[docs/STATUS.md](docs/STATUS.md).

| Radxa ROCK 4D | ArmSoM CM5-IO |
|---|---|
| ![UEFI front page — ROCK 4D](docs/imgs/monitor-4d.png) | ![UEFI front page — CM5-IO](docs/imgs/monitor-cm5io.jpeg) |
| TianoCore front page, 2560×1440@60 over HDMI | TianoCore on CM5-IO over HDMI (slight horizontal offset) |

| | |
|---|---|
| ![GRUB on USB](docs/imgs/grub.png) | ![Fedora 44 GNOME](docs/imgs/desktop.png) |
| GRUB from a Fedora 44 USB stick | GNOME *About* — ROCK 4D, 11.5 GiB RAM |

> **These pictures are the good case, not the usual one.** HDMI comes up
> intermittently: on CM5-IO, 2 of 8 cold boots produced an image. Everything
> the firmware can report about the display path succeeds on the boots that
> produce nothing, so a serial log that looks clean is not a guarantee of a
> picture. Do not take these screenshots as "HDMI works".

## Boards

| Board | Boot medium | Serial console | HDMI | eMMC | USB 3.0 | Ethernet |
|---|---|---|---|---|---|---|
| Radxa ROCK 4D | SPI NOR | Reliable | Intermittent | — (no onboard eMMC) | Working | Working |
| ArmSoM CM5-IO | SD / eMMC | Reliable | Intermittent | Working (26 MHz, capped) | Working | Working |

Read [docs/STATUS.md](docs/STATUS.md) before anything else. It is the account
of what works, what does not, and with what sample size — every claim in it
carries its evidence. The short version: the serial console and the boot chain
are solid, HDMI is the open problem, and PCIe trains but its endpoint's config
space does not answer.

## Build

```bash
scripts/setup-host.sh
scripts/build.sh   cm5io
scripts/package.sh cm5io      # -> out/CM5IO/CM5IO-sdcard.img
```

Details in [docs/BUILDING.md](docs/BUILDING.md), flashing in
[docs/FLASHING.md](docs/FLASHING.md).

## Layout

```
Platform/                    board packages
  ArmSoM/CM5IO/              DSC, PMIC/GPIO/PHY glue, ACPI tables, DTS wiring
  Radxa/ROCK4D/
Silicon/
  Rockchip/                  shared drivers and libraries
    RK3576/                  the SoC package: DSC chain, FDF, Soc.h, ACPI, drivers
    Include/RockchipIpRegs.h IP-block register layouts shared across RK3568/76/88
  Synopsys/DesignWare/       DWC EQoS and DWC MMC
boards/                      one .conf per board
scripts/                     setup-host / build / package
patches/                     our patches to the TianoCore core, applied at setup
binaries/                    BL31, DDR init blob, SPL — build inputs, ~780 KB
devicetree/mainline/         board DTS
docs/
```

A board DSC contains only what differs between boards. Everything the SoC
decides is in
[`Silicon/Rockchip/RK3576/RK3576Base.dsc.inc`](Silicon/Rockchip/RK3576/RK3576Base.dsc.inc).

## History

This tree was restructured on 2026-08-04. Before that it was a copy of the
whole `edk2-rockchip` vendor tree with RK3576 support grown inside it, and
both RK3576 boards pulled in `RK3588Platform.dsc.inc` — the RK3576
configuration was defined by subtracting from RK3588's, and five library
classes were never overridden at all, so RK3576 boards linked RK3588's
`ArmPlatformLib`, `PlatformCruLib`, `MemoryInitPeiLib`, `SaradcLib` and
`Pcie30PhyLib`.

The state immediately before the restructure is preserved on the
`legacy/v0.1` branch and the `legacy-v0.1` tag.

What that arrangement was hiding is written up in
[docs/STATUS.md](docs/STATUS.md); the short version is that `I2cDxe` had been
programming RK3588's I2C controller addresses, which on RK3576 are ordinary
DRAM.

## Licence

BSD-2-Clause-Patent, matching TianoCore and edk2-rockchip. See
[LICENSE](LICENSE).
