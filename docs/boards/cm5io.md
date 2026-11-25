# ArmSoM CM5-IO

English | [简体中文](cm5io.zh-CN.md)

The ArmSoM CM5 module (RK3576) on the CM5-IO carrier. Boots Fedora from NVMe
and Windows 11 23H2 from the eMMC. Results are from one board (4 GB CM5),
sponsored by ArmSoM.

## What works

Board-tested: an OS has booted with firmware 0.4.x; most of the Windows
column was seen with firmware 0.2.0 and woa-rk3576 0.1.0.

| | In the firmware | Windows 11 23H2 | Linux (Fedora) |
|---|---|---|---|
| HDMI | works, 2560x1440 from EDID | works (firmware framebuffer) | works |
| eMMC | works, settings saved | works, boot disk | works |
| SD card | works; can hold the firmware | works | works |
| NVMe | works | works | works, boot disk |
| USB-A (behind a hub) | works | works | works |
| USB-C | USB 2.0 | USB 2.0 | works |
| Ethernet | works | works | works |
| GPU | powered for the OS | wddm-mali-g52 desktop driver run on the TaishanPi 3M only | works (Panfrost) |
| HDMI audio | declared in ACPI | not tried | not recorded |
| WiFi, Bluetooth | not used | not working | not recorded |

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: hold the MaskROM button while powering on. The MaskROM port is
  the USB-C port; unplug anything else from it.
- The BootROM tries the eMMC before the SD card, so a card is ignored while
  the eMMC holds a bootloader.

## Install the firmware

- eMMC (needed for Windows): write the `CM5IO-emmc` image as a
  [whole image](../flash.md#writing-the-whole-image); later updates with
  ["keep the partitions"](../flash.md#updating-the-firmware-and-keeping-everything-else).
- SD card, with no bootloader on the eMMC: [flashing](../flash.md#armsom-cm5-io).

Then [Windows](../windows.md) or [Linux](../linux.md).

## Board facts

[Hardware](../dev/hardware.md#armsom-cm5-io).
