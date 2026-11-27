# Radxa ROCK 4D

English | [简体中文](rock4d.zh-CN.md)

The firmware lives on the 16 MB SPI NOR and boots Fedora or Windows 11 23H2
from NVMe. It installs ACPI tables next to the device tree.

## What works

Firmware and Linux: board-tested after release 0.2.1 (version not recorded);
0.2.1 was run by a user on a v1.12 board (XMC SPI flash). Windows: `main` on
2026-11-26, on a 12 GB board.

| | In the firmware | Linux (Fedora) | Windows 11 23H2 |
|---|---|---|---|
| HDMI | works, 2560x1440 from EDID | works | works, GPU driver, 1920x1080 at 60 Hz |
| NVMe | works | works, boot disk | works, boot disk |
| USB | works; USB-C at USB 2.0 | works | not recorded |
| Ethernet | works | works | works, 1 Gbps |
| Audio | | not recorded | headphones, headset microphone, HDMI |
| NPU, video decoder | | not recorded | work |
| Wi-Fi, Bluetooth (USB module) | | not recorded | no driver |
| SD card | not recorded | not recorded | not recorded |
| UFS | not tried | not tried | no driver |

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: USB-A to USB-A cable from the upper USB 3.0 port to the computer,
  then hold the MaskROM button while plugging in the power supply. The USB-C
  port carries power only.

## Install the firmware

Write the `ROCK4D-spi` image to the SPI NOR ([flashing](../flash.md#radxa-rock-4d)).
This resets the UEFI settings, which live on the same flash. Then
[Linux](../linux.md) or Windows (below).

## Windows

- Use firmware [0.5.0](https://github.com/gahingwoo/edk2-rk3576/releases/tag/0.5.0) or later: older releases have no ACPI tables for
  this board.
- Install with [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)
  (Windows to NVMe). It can also put Armbian on an SD card next to Windows.
- Keep "CPU Idle States" Disabled (the default), or Windows stops at boot
  with bugcheck 0x5C.
- Built, not run yet: the LEDs, the EEPROM, the 40-pin header (off by
  default), fan control on J16, the Maskrom key and headphone jack detect
  (SARADC), the 775 mV GPU option. Details: [board features](../BOARD-FEATURES.md),
  [Windows](../windows.md).

## Board facts

[Hardware](../dev/hardware.md#radxa-rock-4d).
