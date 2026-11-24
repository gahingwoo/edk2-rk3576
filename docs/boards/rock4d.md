# Radxa ROCK 4D

[简体中文](../zh-CN/boards/rock4d.md)

The firmware lives on the ROCK 4D's 16 MB SPI NOR and boots Fedora from
NVMe. This board builds without ACPI tables, so Windows does not start on
it.

## What works

| | In the firmware | Linux (Fedora) |
|---|---|---|
| HDMI | works, 2560x1440 from the monitor's EDID | works |
| NVMe | works | works, boot disk |
| USB | works; USB-C at USB 2.0 | works |
| Ethernet | works | works |
| SD card | not recorded | not recorded |
| UFS | not tried | not tried |
| Windows | not available (no ACPI tables) | |

The board was run again after release 0.2.1; which version was not
recorded. 0.2.1 itself was run on a v1.12 board (XMC SPI flash) by a user:
it reached the boot menu and started a Fedora installer from USB. The
display, I2C and menu changes since then are shared with the other two
boards and were run there.

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: connect a USB-A to USB-A cable from the board's upper USB 3.0
  USB-A port to the computer, and hold the MaskROM button while plugging in
  the power supply. The USB-C port carries power only.

## Install the firmware

Write the `ROCK4D-spi` image to the SPI NOR
([flashing](../flash.md#radxa-rock-4d)). The UEFI settings are kept on the
same flash; writing the image resets them.

Then: [Linux](../linux.md).

## Board facts

[Hardware](../dev/hardware.md#radxa-rock-4d).
