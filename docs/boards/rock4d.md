# Radxa ROCK 4D

English | [简体中文](rock4d.zh-CN.md)

The firmware lives on the ROCK 4D's 16 MB SPI NOR and boots Fedora from
NVMe. The image now carries ACPI tables next to the device tree, as on the
other two boards; Windows has not been started on this board yet.

What the TaishanPi 3M has and this board has the hardware for is on branch
`rock4d-parity`, built and not run on a ROCK 4D: thermal zones, the fan on
J16 at full speed (and "Fan Control by ACPI Thermal Zone"), power-off
through the RK806, "CPU Maximum Frequency", the NPU option, the GPU rail
report, HDMI audio, the ES8388 headphone jack and "Armbian (U-Boot)" for an
Armbian SD card. The full list, per board: [board features](../BOARD-FEATURES.md).

## What works

| | In the firmware | Linux (Fedora) |
|---|---|---|
| HDMI | works, 2560x1440 from the monitor's EDID | works |
| NVMe | works | works, boot disk |
| USB | works; USB-C at USB 2.0 | works |
| Ethernet | works | works |
| SD card | not recorded | not recorded |
| UFS | not tried | not tried |
| Windows | not tried (ACPI tables built, not run) | |

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
