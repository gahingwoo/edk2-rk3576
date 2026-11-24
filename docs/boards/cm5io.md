# ArmSoM CM5-IO

[简体中文](../zh-CN/boards/cm5io.md)

The ArmSoM CM5 module (RK3576) on the CM5-IO carrier. Boots Fedora from NVMe
and Windows 11 23H2 from the eMMC. Everything below is from one board, a
4 GB CM5. The module and the carrier were sponsored by ArmSoM.

## What works

| | In the firmware | Windows 11 23H2 | Linux (Fedora) |
|---|---|---|---|
| HDMI | works, 2560x1440 from the monitor's EDID | works (the firmware's framebuffer) | works |
| eMMC | works, settings are saved | works, boot disk | works |
| SD card | works; the firmware can live on it | works | works |
| NVMe | works | works | works, boot disk |
| USB-A (behind a hub) | works | works | works |
| USB-C | works at USB 2.0 | works at USB 2.0 | works |
| Ethernet | works | works | works |
| GPU | powered for the OS | wddm-mali-g52's first milestones ran here (2026-10-01 to 2026-10-03); its desktop driver has run on the TaishanPi 3M only | works (Panfrost) |
| HDMI audio | declared in ACPI | not tried | not recorded |
| WiFi, Bluetooth | not used | not working | not recorded |

Most of the Windows column was seen on 2026-10-01 with firmware 0.2.0 and
woa-rk3576 0.1.0. The board has booted an operating system with firmware
0.4.x; the 0.4.0 release notes recorded its menus over HDMI in both
languages.

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: hold the MaskROM button while powering on. The MaskROM port is
  the USB-C port; unplug anything else from it.
- The BootROM tries the eMMC before the SD card, so a card is ignored while
  the eMMC holds a bootloader.

## Install the firmware

On the eMMC (needed for Windows):

1. Write the `CM5IO-emmc` image as a whole image
   ([flashing](../flashing.md#writing-the-whole-image)). This replaces
   everything on the eMMC.
2. Later updates: "keep the partitions"
   ([flashing](../flashing.md#updating-the-firmware-and-keeping-everything-else)).

Or on an SD card, with an eMMC that holds no bootloader:
[flashing](../flashing.md#armsom-cm5-io).

Then:

- Windows: [woa-rk3576's install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md).
  See [Windows](../windows.md).
- Linux: [Linux](../linux.md).

## Board facts

[Hardware](../dev/hardware.md#armsom-cm5-io).
