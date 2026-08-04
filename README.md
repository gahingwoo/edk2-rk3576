# edk2-rk3576

EDK2 UEFI firmware for Rockchip RK3576 boards.

```
BootROM → U-Boot SPL (idbloader) → TF-A BL31 → EDK2 (BL33) → OS
```

| Board | Boot medium | State |
|---|---|---|
| ArmSoM CM5-IO (CM5 module + CM5-IO carrier) | SD / eMMC | Boots to UEFI Shell; eMMC, USB and GbE work |
| Radxa ROCK 4D | SPI NOR | Boots to UEFI Shell |

Read [docs/STATUS.md](docs/STATUS.md) before anything else — it is the honest
account of what works, what does not, and with what sample size. HDMI output
is intermittent (2 of 8 cold boots) and PCIe does not train in UEFI.

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
