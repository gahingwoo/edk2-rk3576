# Radxa ROCK 4D

English | [简体中文](rock4d.zh-CN.md)

The firmware lives on the 16 MB SPI NOR and boots Fedora from NVMe. The board
carries ACPI tables next to the device tree; Windows has not been
started on this board yet.

## What works

Board-tested after release 0.2.1 (version not recorded); 0.2.1 was run by a
user on a v1.12 board (XMC SPI flash). Later display, I2C and menu changes
are shared code tested on the other two boards.

| | In the firmware | Linux (Fedora) |
|---|---|---|
| HDMI | works, 2560x1440 from EDID | works |
| NVMe | works | works, boot disk |
| USB | works; USB-C at USB 2.0 | works |
| Ethernet | works | works |
| SD card | not recorded | not recorded |
| UFS | not tried | not tried |
| Windows | not tried (ACPI tables built, not run) | |

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: USB-A to USB-A cable from the upper USB 3.0 port to the computer,
  then hold the MaskROM button while plugging in the power supply. The USB-C
  port carries power only.

## Install the firmware

Write the `ROCK4D-spi` image to the SPI NOR ([flashing](../flash.md#radxa-rock-4d)).
This resets the UEFI settings, which live on the same flash. Then
[Linux](../linux.md).

## Board facts

[Hardware](../dev/hardware.md#radxa-rock-4d).
