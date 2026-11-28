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

## Parity with the TaishanPi 3M

Everything the TaishanPi 3M firmware has, checked against this board's
hardware (mainline device tree, ArmSoM's vendor tree, the CM5 and CM5-IO
V1.1 schematics). In release 0.5.0. Only "Armbian (U-Boot)" has run on the
board so far (2026-11-27).

| Feature | On this board | Notes |
|---|---|---|
| Debug Report (Ctrl+Alt+F11), screenshots, on-screen keyboard, network time, OS variables on eMMC | already there | shared code, every board |
| Thermal zones, 115 C `_CRT`, 120 C hardware TSHUT | already there | `PcdTsadcEnable` |
| Video decoder, CPU idle states setup options | already there | shared |
| HDMI audio (SAI6) | already there | `PcdHdmiAudioPreset` |
| RK806 power-off at shutdown | **added** | same PMIC wiring as the TaishanPi 3M: RK806 at 0x23 on I2C1, GPIO0_A3 = PMIC_PWR_CTRL1 |
| NPU setup option | **added** | appears once the firmware can reach the RK806 (vdd_npu_s0 = BUCK2) |
| CPU Maximum Frequency | **added**, default 1608 MHz | BUCK1/BUCK3 as on the TaishanPi 3M; the OTP SoC-variant check still applies |
| GPU rail report (`GpuRail` variable) | **added** | read on every boot and handed to the Windows GPU driver |
| GPU voltage for 702 MHz (775 mV) | **added**, default on | vdd_gpu_s0 is RK806S-5 BUCK5 on I2C1 0x23 (CM5 schematic p2, p13; `rk3576-armsom-cm5.dtsi` dcdc-reg5, 550000 to 900000 uV), as on the TaishanPi 3M. On: raised from 750 to 775 mV in 6.25 mV steps, each read back, only with TSHUT armed, and maliwddm runs 702 MHz; if the PMIC or BUCK5 does not read as expected the rail is left alone and maliwddm runs 594 MHz; off: 750 mV and 594 MHz |
| Analog audio (ES8388) | **added** | I2C0 0x10, SAI1 M0 (SDO0), output on the 3-pin header CN3 (OUT1; OUT2 only reaches test pads T36/T37), microphone pads T34/T35 into LIN2/RIN2 differential; `_DSD` `rockchip,output-route` 3, `rockchip,input-select` 4; no jack detect (CN3 has no detect contact), no amplifier |
| "Armbian (U-Boot)" one-shot | **added** | SPL rebuilt from mainline U-Boot v2026.10 with Armbian's CM5-IO files and the one-shot patch; it now tries the SD card first, like the TaishanPi 3M's |
| Fan on/off switch ("Fan", default on) | already there (main, `fan-onoff`) | polarity still not settled, see `PwmFanSetSpeed` in this board's RockchipPlatformLib.c: "on" holds the pin low, which is full speed only if the inverted reading is right |
| CPU frequency asked once at first boot | **added** with CPU Maximum Frequency | `cpu-ask-once` (main) asks on every board that offers Max |
| Wi-Fi, Bluetooth (BCM43752 on the BW3752-50B1 module) | **added** | Bluetooth: `\_SB.BTH0` (`RKCP4362`) on UART4 M1, BT_REG_ON GPIO1_C7, BT_WAKE GPIO1_D4, for woa-rk3576 bcm4345bt with BCM4362A2.hcd. Wi-Fi: setup option "SDIO Wi-Fi" (off by default) sets up SDIO and probes the card, then installs the `RKCPFE2D` SSDT for bcmwlsdio (branch `cm5io-wifi-bt` in woa-rk3576, never run). Pins from the ArmSoM-CM5-1V1 schematic pages 11 and 17; they are the TaishanPi 3M's, only the UART differs |
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
  csaudiork3x/rk3xi2sbus and es8323 with no yellow mark (needs es8323 from
  woa-rk3576 `es83-optional-irq` or later: older ones create an interrupt
  this board's `_CRS` does not have); sound out of CN3 and the T36/T37 pads;
  a microphone on T34/T35 records (differential LIN2/RIN2); the codec's
  `Device Parameters` show `ActiveOutputRoute` 3, `ActiveInputSelect` 4,
  `JackInterrupt` 0; NPU (with the option on) binds woa-rk3576-npu; maliwddm
  reads the GpuRail variable.
- GPU voltage, with the fan on ("GPU voltage for 702 MHz (775 mV)" is On by
  default). Serial log: `GpuRail: raised BUCK5 0x28 -> 0x2C, read back
  775000 uV` (if it says `not raising BUCK5: ...` instead, note the reason:
  this board's RK806 has not been read yet). Windows: `m5test clk` (or the
  GPU clock perf counter) shows 702 MHz, with no `GpuClk` stored and
  `GpuClkHigh` not set; FurMark 60 s runs without a TDR; note the score and
  the peak GPU and CPU temperatures. Then set the option to Off and restart:
  the log shows `lowered BUCK5 0x2C -> 0x28, read back 750000 uV` and
  Windows runs 594 MHz.
- Bluetooth (Windows): `ACPI\RKCP4362` gets bcm4345bt; its Device
  Parameters show `ResetStatus` 0, `RomVersion`, `FirmwareLoaded` 1 with
  BCM4362A2.hcd, `Baud` 1500000. A `ResetStatus` timeout means the module
  is off, has no 32 kHz clock, or the UART pins are wrong.
- "SDIO Wi-Fi" on: the serial log has `CM5-IO Wi-Fi: CIS
  vendor 0x02d0 device 0xaae8`, `chip ID ... chip 0xaae8 (43752) rev N` and
  `probe PASS`; note the revision (Armbian's firmware is for rev 2). In
  Windows `\_SB.SDIO` binds dwcmshc and a `SD\VID_02D0&PID_AAE8` card
  appears.
- Linux: boots as before (audio, NPU and the PMIC are the device tree's
  business there); `poweroff` still powers off.

## Board facts

[Hardware](../dev/hardware.md#armsom-cm5-io).
