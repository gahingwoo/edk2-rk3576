# Troubleshooting

English | [简体中文](troubleshooting.zh-CN.md)

For a problem to report, save a [debug report](firmware-menus.md#debug-report)
first and attach it to an
[issue](https://github.com/gahingwoo/edk2-rk3576/issues).

## The board does not show up in MaskROM

- On the TaishanPi 3M, short the `MSK` pads before power is applied, and use
  the USB-C port marked `OTG+DP`.
- On the ROCK 4D, use a USB-A to USB-A cable on the upper USB 3.0 port; its
  USB-C port has no data lines.
- On the CM5-IO, the MaskROM port is the USB-C port; unplug anything else
  from it.
- On Windows, install the WinUSB driver for the device with
  [Zadig](https://zadig.akeo.ie/).
- A device `2207:350e` appears when the board is in MaskROM.

## rkdeveloptool errors

- `Opening loader failed`: `db` was given `binaries/rk3576_ddr.bin`. It needs
  an RKLD loader (first bytes `LDR `), such as rkbin's
  `rk3576_spl_loader_*.bin`.
- `Write LBA failed` right after `db`: wait a few seconds and run the write
  again; the device was still re-enumerating.
- The board boots the old firmware after a write: the write did not reach
  100%. Write again before `rd`.

## The CM5-IO ignores the SD card

The BootROM tries the eMMC first. A card is used only when the eMMC holds no
bootloader. On the TaishanPi 3M an SD card never boots on its own; the SPL has
to be on the eMMC ([flashing](flash.md#lckfb-taishanpi-3m)).

## The settings were lost after an update

The update wrote over the settings store at sector 43648. Use "keep the
partitions" in the web flasher or Maskrom Studio, or the `dd` range in
[flashing](flash.md#updating-the-firmware-and-keeping-everything-else).

## Boot entries made in Linux or Windows are gone after a reboot

On eMMC, OS changes to boot entries are kept only with "Save OS Variable
Changes to eMMC" turned on ([firmware menus](firmware-menus.md#boot-entries-written-by-an-os)).
Boot entries made by hand are also gone after Reset All Settings.

## The clock is wrong

The boards do not keep time across a power-off. Connect a network cable
(the firmware sets the clock over NTP at boot), and let the OS sync it.

## The TaishanPi 3M screen shows only the backlight

If touch still works, the DSI ribbon has most likely come loose. Power off,
re-seat it, and power on again.

## A USB stick made with Ventoy does not boot WinPE

Do not use Ventoy for the WinPE image: its UDF driver stopped with a
synchronous exception on this firmware. Write the image directly. Ventoy
itself starts, and has handed over to a Linux disk image once.

## Windows 11 24H2 does not start

RK3576 is ARMv8.0; 24H2 and later need ARMv8.1. Use 23H2.

## Recovery

MaskROM is in the BootROM and nothing here writes it, so a board can always
be put back into MaskROM and written again.
