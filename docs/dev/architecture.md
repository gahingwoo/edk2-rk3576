# Architecture

English | [简体中文](architecture.zh-CN.md)

How the tree is put together, and why.

## Boot chain

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> OS
```

- The BootROM loads the idblock (DDR init blob and U-Boot SPL) from SPI NOR
  (ROCK 4D) or from the eMMC or SD card (CM5-IO, TaishanPi 3M).
- The SPL loads one FIT: BL31, EDK2, a DTB for itself and, on SD and eMMC,
  the UEFI variable store ([image layout](image-layout.md)).
- BL31 is the open-source TF-A v2.14.0 (`binaries/bl31.elf`); Rockchip's
  BL31 is used only when that file is absent.
- EDK2 runs as BL33 at EL2 from `0x40800000`.

The prebuilt inputs and how each was built are in
[binaries/README.md](https://github.com/gahingwoo/edk2-rk3576/blob/main/binaries/README.md).

## Layout

```
Platform/ArmSoM/CM5IO/       board package: DSC, PMIC/GPIO/PHY glue, ACPI, DTS
Platform/Radxa/ROCK4D/
Platform/LCKFB/TaishanPi3M/
Silicon/Rockchip/RK3576/     SoC package: RK3576Base.dsc.inc, FDF, ACPI, drivers
Silicon/Rockchip/            drivers shared across Rockchip SoCs
Silicon/Synopsys/DesignWare/ DWC EQoS and DWC MMC
boards/                      one .conf per board, read by the scripts
scripts/                     setup-host, build, package; serial and KD helpers
patches/                     patches to the TianoCore core, applied at setup
binaries/                    BL31, DDR init blob, SPL
devicetree/                  board DTS (mainline-based), vendor DTBs
docs/                        the site at edk2.gahingwoo.com
```

## Board and SoC split

A board DSC holds only what differs between boards: which devices exist,
GPIOs, PMIC and PHY glue, defaults of setup options, the ACPI DSDT and the
device tree. Everything the SoC decides is in
`Silicon/Rockchip/RK3576/RK3576Base.dsc.inc` and `RK3576.fdf`.

Before 2026-08-04 this tree was a copy of edk2-rockchip that built RK3576 on
top of RK3588's configuration. Values that were right for RK3588 and wrong
for RK3576 (memory map, I2C addresses, reset IDs) kept turning up, so the
RK3576 package was made standalone. The old tree is on the `legacy/v0.1`
branch.

## ACPI and device tree

The CM5-IO and the TaishanPi 3M install ACPI tables and a device tree side
by side, so Windows uses ACPI and Linux the device tree. The ROCK 4D builds
the device tree only. Details: [ACPI](acpi.md).

The device tree handed to Linux is the firmware's own, built from
`devicetree/mainline/`, with fix-ups at boot (`FdtPlatformDxe`): for
example the DSI panel and backlight on the TaishanPi 3M when the screen is
attached, a simple-framebuffer node when "Force UEFI GOP Display" is on,
and always-on USB-A VBUS supplies.

## Core patches

`third_party/edk2` is tianocore/edk2 pinned to edk2-stable202602. Changes to
it are patch files in `patches/`, applied by `setup-host.sh`, because edits
in the submodule are lost when it is reset. Each patch is described in
[patches/README.md](https://github.com/gahingwoo/edk2-rk3576/blob/main/patches/README.md).
