# edk2-rk3576

[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![License](https://img.shields.io/badge/license-BSD--2--Clause--Patent-lightgrey)](LICENSE)
[![Flash](https://img.shields.io/badge/flash-WebUSB%20browser%20tool-informational)](https://flash.gahingwoo.com/)
[![Docs](https://img.shields.io/badge/docs-edk2.gahingwoo.com-informational)](https://edk2.gahingwoo.com/)

EDK2 / TianoCore UEFI firmware for Rockchip RK3576 boards.

```
BootROM → U-Boot SPL (idbloader) → TF-A BL31 → EDK2 (BL33) → OS
```

Both boards boot to the UEFI front page over HDMI and run Fedora from an NVMe
disk. CM5-IO also runs Windows 11 23H2 to the desktop from its eMMC.

- **Images:** [Releases](https://github.com/gahingwoo/edk2-rk3576/releases/latest)
- **Flash from the browser:** [flash.gahingwoo.com](https://flash.gahingwoo.com/)
- **Docs:** [edk2.gahingwoo.com](https://edk2.gahingwoo.com/)

## Boards

| Board | Boot medium | HDMI | eMMC | PCIe / NVMe | USB 3.0 | Ethernet |
|---|---|---|---|---|---|---|
| Radxa ROCK 4D | SPI NOR | Working (stable signal; one unlogged bench run, no count) | — (UFS, no eMMC) | Working, Fedora boots from NVMe | Working | Working |
| ArmSoM CM5-IO | SD / eMMC | Working (15 of 15 cold boots) | Working (52 MHz HS, reads and writes; UEFI variables persist) | Working, Fedora boots from NVMe | Working | Working (UEFI received only CRC errors before `4e3f7af`; Windows gets DHCP since then, 2026-10-01) |

The serial console is 1500000 8N1 on both boards.

## Known issues

- **Windows needs this project's drivers for the eMMC and the SD slot.** It
  ships none for these controllers. With the ones in
  [woa-rk3576](https://github.com/gahingwoo/woa-rk3576), Windows 11 23H2 boots
  from the eMMC (2026-10-01, one installation); how it is installed is in that
  repo's `docs/INSTALL.md`.
- **Boot variables written from an OS do not reach the firmware.** On
  2026-10-01 a `BootNext` and a Fedora boot entry set with `efibootmgr` under
  Linux were both absent from the firmware's own list on the next boot, and
  `bcdboot` left no Windows Boot Manager entry either. Linux also logs
  `Unable to handle paging request in EFI runtime service` at reboot. Windows
  boots anyway, through `\EFI\Boot\bootaa64.efi` on the eMMC.
- **An SD card in the slot used to make Windows crawl or bugcheck.** The
  firmware left the SD controller's interrupts armed at ExitBootServices.
  `75c0f32` quiesces it; four card-present boots have been clean since, which
  is not yet many.
- **The USB-C port is USB 2.0 only, in UEFI and in Windows.** No driver here
  brings up its USBDP PHY. Until `5edeab0` Windows could not start that
  controller at all (code 10); it now enables the controller's USB3 port at
  ExitBootServices so the controller describes itself consistently. Measured on
  CM5-IO only; the ROCK 4D has the same controller and gets the same write.
  Linux has its own PHY driver and runs the port at 5 Gb/s.
- **Windows 11 24H2 and later cannot boot.** RK3576 is ARMv8.0. Use Windows 10
  or 11 23H2.
- **The ComboPHY and PCIe device-tree fixups do nothing.** They look for
  `/pcie@…` at the root, where mainline puts those nodes under `/soc/`. NVMe
  works without them.
- **The CRU library is still RK3588's clock tree** with RK3576 base
  addresses. Nothing on RK3576 is known to call into it.

## Screenshots

| Radxa ROCK 4D | ArmSoM CM5-IO |
|---|---|
| ![UEFI front page — ROCK 4D](docs/imgs/monitor-4d.png) | ![UEFI front page — CM5-IO](docs/imgs/monitor-cm5io.jpeg) |
| TianoCore front page, 2560×1440@60 over HDMI | TianoCore on CM5-IO, 2560×1440@60 over HDMI |

| Radxa ROCK 4D | ArmSoM CM5-IO |
|---|---|
| ![GNOME on ROCK 4D](docs/imgs/rock4d-desktop.png) | ![GNOME on CM5-IO](docs/imgs/cm5io-desktop.png) |
| GNOME *About* — Fedora 44, 11.5 GiB RAM | GNOME *About* — Fedora 45 Workstation, kernel 7.2, 3.7 GiB RAM, Mali-G52 via Panfrost |

| | |
|---|---|
| ![GRUB on USB](docs/imgs/grub.png) | ![Fedora live console](docs/imgs/fedora.png) |
| GRUB from a Fedora USB stick | Fedora live console |

| ArmSoM CM5-IO |
|---|
| ![Windows 11 on CM5-IO](docs/imgs/cm5io-win11-desktop.jpg) |
| Windows 11 23H2 booted from the eMMC: eight cores, 3.7 GB, the eMMC as C:, the NVMe, the SD card and Ethernet. Drivers from [woa-rk3576](https://github.com/gahingwoo/woa-rk3576). |

## Build

```bash
scripts/setup-host.sh
scripts/build.sh   cm5io
scripts/package.sh cm5io      # -> out/CM5IO/CM5IO-sdcard.img
```

Details in [docs/BUILDING.md](docs/BUILDING.md), flashing in
[docs/FLASHING.md](docs/FLASHING.md), image layout in
[docs/SPI_LAYOUT.md](docs/SPI_LAYOUT.md), board facts in
[docs/HARDWARE.md](docs/HARDWARE.md).

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
binaries/                    BL31, DDR init blob, SPL: build inputs, about 1 MB
devicetree/mainline/         board DTS
docs/                        the site at edk2.gahingwoo.com
```

A board DSC contains only what differs between boards. Everything the SoC
decides is in
[`Silicon/Rockchip/RK3576/RK3576Base.dsc.inc`](Silicon/Rockchip/RK3576/RK3576Base.dsc.inc).

## History

This tree was restructured on 2026-08-04. Before that it was a copy of the
whole `edk2-rockchip` vendor tree with RK3576 support grown inside it, and
both RK3576 boards pulled in `RK3588Platform.dsc.inc`: the RK3576
configuration was defined by subtracting from RK3588's, and five library
classes were never overridden, so RK3576 boards linked RK3588's
`ArmPlatformLib`, `PlatformCruLib`, `MemoryInitPeiLib`, `SaradcLib` and
`Pcie30PhyLib`. That put RK3588's memory map and I2C controller addresses
into RK3576 images: the map declared about 700 MB of non-DRAM as System RAM,
and the I2C addresses are ordinary DRAM on RK3576.

The state immediately before the restructure is preserved on the
`legacy/v0.1` branch and the `legacy-v0.1` tag.

## Licence

BSD-2-Clause-Patent, matching TianoCore and edk2-rockchip. See
[LICENSE](LICENSE).
