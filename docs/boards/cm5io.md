# ArmSoM CM5-IO

English | [简体中文](cm5io.zh-CN.md)

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
   ([flashing](../flash.md#writing-the-whole-image)). This replaces
   everything on the eMMC.
2. Later updates: "keep the partitions"
   ([flashing](../flash.md#updating-the-firmware-and-keeping-everything-else)).

Or on an SD card, with an eMMC that holds no bootloader:
[flashing](../flash.md#armsom-cm5-io).

Then:

- Windows: [woa-rk3576's install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md).
  See [Windows](../windows.md).
- Linux: [Linux](../linux.md).

## Parity with the TaishanPi 3M (branch `cm5io-parity`)

Everything the TaishanPi 3M firmware has, checked against this board's
hardware (mainline device tree, ArmSoM's vendor tree, the CM5 and CM5-IO
V1.1 schematics). Built, and nothing in this section has run on the board.

| Feature | On this board | Notes |
|---|---|---|
| Debug Report (Ctrl+Alt+F11), screenshots, on-screen keyboard, network time, OS variables on eMMC | already there | shared code, every board |
| Thermal zones, 115 C `_CRT`, 120 C hardware TSHUT | already there | `PcdTsadcEnable` |
| Video decoder, CPU idle states setup options | already there | shared |
| HDMI audio (SAI6) | already there | `PcdHdmiAudioPreset` |
| RK806 power-off at shutdown | **added** | same PMIC wiring as the TaishanPi 3M: RK806 at 0x23 on I2C1, GPIO0_A3 = PMIC_PWR_CTRL1 |
| NPU setup option | **added** | appears once the firmware can reach the RK806 (vdd_npu_s0 = BUCK2) |
| CPU Maximum Frequency | **added**, default 1608 MHz | BUCK1/BUCK3 as on the TaishanPi 3M; the OTP SoC-variant check still applies |
| GPU rail report (`GpuRail` variable) | **added**, reads only | no 775 mV raise: the GPU stays at 594 MHz on this board |
| Analog audio (ES8388) | **added** | I2C0 0x10, SAI1 M0 (SDO0), output on the 3-pin header CN3 and the 40-pin header; no jack detect (CN3 has no detect contact), no amplifier |
| "Armbian (U-Boot)" one-shot | **added** | SPL rebuilt from mainline U-Boot v2026.10 with Armbian's CM5-IO files and the one-shot patch; it now tries the SD card first, like the TaishanPi 3M's |
| Fan on/off switch ("Fan", default on) | already there (main, `fan-onoff`) | polarity still not settled, see `PwmFanSetSpeed` in this board's RockchipPlatformLib.c: "on" holds the pin low, which is full speed only if the inverted reading is right |
| CPU frequency asked once at first boot | **added** with CPU Maximum Frequency | `cpu-ask-once` (main) asks on every board that offers Max |
| Wi-Fi, Bluetooth (SYN43752) | not ported | no Windows driver for this chip; the TaishanPi 3M drivers are for the BCM43456/BCM4345C5 |
| DSI screen, touch, touch keys | not applicable | the carrier has a DSI connector but no screen was tested; the TaishanPi 3M code is for its own panel |
| USB-C device wait / USB-A VBUS FDT fixup | not needed | the CM5-IO powers its USB-C port; its USB-A regulator is a PHY supply in the mainline tree |

What to check on the board, firmware first:

- Serial log: `Rk806I2cPowerOff` finds the PMIC (`CHIP_NAME 0x80`), `GpuRail`
  prints vdd_gpu_s0, `Cm5ioAudio: mclk_sai1 = ...` and `SAI1 and I2C0 pins
  set`.
- "Fan" on and off: note which one actually spins the fan on CN32 (this
  settles the polarity).
- "Shut Down" on the front page and `reset -s` in the Shell switch the board
  off; a restart still restarts.
- First boot after flashing: the one-time CPU frequency prompt appears on
  HDMI. CPU page: "CPU Maximum Frequency" is offered; Max boots and the log shows
  the raised rails. ACPI page: "NPU" is offered.
- With an Armbian SD card in and the firmware on the eMMC: a normal boot
  reaches UEFI; "Armbian (U-Boot)" boots Armbian once; the next reboot is
  UEFI again. Also check that the picture is unchanged with the new SPL; if
  it is not, remove `SPL_DEFCONFIG` from `boards/cm5io.conf` to go back to
  `binaries/idblock_cm5io.bin`.
- Windows: shutdown powers off; `\_SB.SAI1` and `\_SB.I2C0.ES83` bind
  csaudiork3x/rk3xi2sbus and es8323 with no yellow mark; sound out of CN3 and
  the 40-pin line out; NPU (with the option on) binds woa-rk3576-npu; maliwddm
  reads the GpuRail variable and runs at 594 MHz.
- Linux: boots as before (audio, NPU and the PMIC are the device tree's
  business there); `poweroff` still powers off.

## Board facts

[Hardware](../dev/hardware.md#armsom-cm5-io).
