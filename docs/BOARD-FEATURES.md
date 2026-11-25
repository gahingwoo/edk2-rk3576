# Board features: TaishanPi 3M, CM5-IO, ROCK 4D

English | [简体中文](BOARD-FEATURES.zh-CN.md)

Inventory of 2026-11-25, written for the feature freeze: every setup option,
ACPI device, firmware feature and Windows driver match, per board, against
the LCKFB TaishanPi 3M, the board everything else is measured against.

* **TaishanPi 3M** and **CM5-IO**: what `main` (0b03605) builds. The CM5-IO
  column does not include branch `cm5io-parity`, which is being worked on in
  parallel and will change it.
* **ROCK 4D**: what branch `rock4d-parity` builds. Where `main` differs, the
  cell says so ("main: ...").
  On `main` the ROCK 4D builds no ACPI tables at all (device tree only), so
  every ACPI device and every Windows driver in the ROCK 4D column exists on
  `rock4d-parity` only.
* Branches that are not on `main` for any board are listed at the end.

Marks: **yes** = present; **missing** = the hardware exists and the firmware
or ACPI does not have it; **n/a** = no such hardware on that board. "Not run"
means built and never run on that board. Nothing in the ROCK 4D column of
`rock4d-parity` has run on a ROCK 4D.

Hardware facts come from each board's mainline device tree
(`devicetree/mainline/`), its schematic (ROCK 4D V1.12, CM5-IO 1V1, LCKFB
TaishanPi 3M v1.4) and the vendor kernel tree; the ROCK 4D's are cited in
`Platform/Radxa/ROCK4D/`.

## Setup options

| Option (page) | Gate | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| Config Table Mode (ACPI / Device Tree) | every board | yes, ACPI + DT | yes, ACPI + DT | yes, ACPI + DT (main: device tree only) |
| ACPI PCIe ECAM compatibility | ACPI on | yes | yes | yes |
| Fan Control by ACPI Thermal Zone | `PcdAcpiFanControlSupported` | yes, off | yes, off | yes, off (main: missing) |
| CPU Idle States (`_LPI`) | every board | yes | yes | yes |
| NPU (experimental) | `PcdRk806I2cBase` != 0 | yes | missing (RK806 on I2C1 0x23 exists) | yes (main: missing) |
| Video decoder (VDPU383) | every board | yes | yes | yes |
| SDIO Wi-Fi | `PcdAcpiSdioWifiSupported` | yes (AP6256) | missing on main (SYN43752 on SDIO; no Windows driver either) | n/a: Wi-Fi is a USB module (FCU760K, AIC8800D80) |
| Device tree: compat mode, Force UEFI GOP, DTB override, overlays, fixups | DT on | yes | yes | yes |
| CPU Maximum Frequency | `PcdCpuMaxFreqSupported` | yes, 1608 MHz | missing (same RK806 rails) | yes, 1608 MHz (main: missing) |
| Fan (on/off, CPU page) | `PcdPwmFanSpeedDefault` != 0 | yes, on | yes, on | yes, on (main: hidden, no fan) |
| CPU frequency asked once at first boot (with a screen) | `PcdCpuMaxFreqSupported` | yes | no (option not offered) | yes (main: no) |
| Screenshot target | every board with a display | yes | yes | yes |
| Network Time Sync | every board with IPv4 | yes | yes | yes |
| Save OS Variable Changes to eMMC | every board | yes | yes | shown, not needed: the store is on SPI NOR and OS writes reach it at once |
| Display: mode, priority, force, duplicate, rotation, HDMI signaling | display | yes | yes | yes |
| Display: screen brightness | a MIPI DSI connector in `PcdDisplayConnectors` | yes | n/a (no DSI panel supported) | n/a: the board has a DSI connector ("MIPI CSI/DSI" page), no panel is supported |
| ComboPHY modes | every board | yes | yes | yes |
| Language (English / Simplified Chinese) | every board | yes | yes | yes |

## Firmware features

| Feature | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|
| Debug report (Ctrl+Alt+F11, boot log to the ESP) | yes | yes | yes |
| Screenshots | yes | yes | yes |
| On-screen keyboard and USB mouse | yes (also by touch) | yes | yes |
| Touch in the menus, touch test | yes | n/a | n/a |
| Reset to MaskROM (F4) | yes | yes | yes |
| Reset All Settings | yes | yes | yes |
| NTP clock at boot | yes | yes | yes |
| Battery-backed RTC (HYM8563 family on I2C2) | yes (no battery fitted) | yes | yes (TCS8563, battery J6) |
| NV variable store | on the eMMC, in the firmware region | on the boot medium (eMMC or SD) | SPI NOR at 0xFC0000 |
| Power off through the RK806 (Shut Down, `reset -s`, Windows) | yes | missing (RK806 on I2C1 0x23) | yes (main: missing) |
| GPU rail report (`GpuRail` variable, reads only) | yes | missing | yes (main: missing) |
| GPU rail raised to 775 mV for 702 MHz | yes, approved for this board only | n/a until approved | not ported: approved for the TaishanPi 3M only; the Windows driver then defaults to 594 MHz |
| TSADC read-out and TSHUT at 120 C | yes | yes | yes |
| Fan started at 100% | yes (CN2, PWM2 ch3) | yes (CN32, PWM2 ch6, polarity disputed) | yes, J16, PWM2 ch5, GPIO4_C5, normal polarity (main: no fan) |
| HDMI audio preset (SAI6, 48 kHz) | yes | yes | yes (main: off) |
| Analog audio preset (ES8388 on SAI1 + I2C3) | yes, with amplifier and jack detect | not on main | yes: headphone jack and its microphone, no amplifier (none fitted), no jack detect (it is a SARADC input) (main: missing) |
| "Armbian (U-Boot)" one-shot | yes (request on the eMMC) | missing (its SPL is not rebuilt by `build-spl.sh`) | yes: request on the SD card, Armbian on the SD card only; needs the new SPL this branch builds (main: missing) |
| SPL built from pinned sources (`build-spl.sh`) | yes | no (prebuilt `idblock_cm5io.bin`) | yes, U-Boot v2026.10 (main: prebuilt 2026.04) |
| MIPI DSI screen | yes | n/a | missing: connector exists, no panel |
| USB-C VBUS from the FUSB302 | yes | yes | n/a: USB-C is power input only |
| Bluetooth power-up for the OS | yes (UART6) | missing | n/a in ACPI: Bluetooth is on the USB Wi-Fi module |

## ACPI devices

| Device | `_HID` | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| CPUs, `_LPI` (RKLPI / RKWFI SSDT) | ACPI0007 | yes | yes | yes |
| PCIe root complexes (PCI0, PCI1) | PNP0A08 | yes (hidden without a link) | yes | yes (hidden without a link) |
| eMMC | RKCP0D40 | yes | yes | n/a: no eMMC, its pins are the SPI NOR's |
| SD card | RKCPFE2C | yes | yes | yes |
| SDIO Wi-Fi host (RKWIFI SSDT) | RKCPFE2D | yes, setup option | missing | n/a (USB Wi-Fi) |
| GPIO banks, pin controller | RKCP3002, RKCP3005 | yes | yes | yes |
| DMA (PL330) | ARMH0330 | yes | yes | yes |
| UART2 | HISI0031 | no (no usable pins) | yes | yes |
| I2C0..I2C9 | RKCP3001 | yes | yes | yes |
| SPI0..SPI4 | RKCP3003 | yes | yes | yes (SFC0 `_STA` 0) |
| RK806 PMIC | RKCP0806 | yes | yes | yes |
| HYM8563 RTC | HYMB0001 | yes | yes | yes |
| Touch (GT9271) | RKCP9271 | yes | n/a | n/a |
| Bluetooth (BCM4345C5) | RKCP4345 | yes | missing | n/a (USB) |
| USB XHC0 / XHC1 | PNP0D10 | yes | yes (+ XHC2 stub) | yes |
| GMAC0 | RKCP6543 | yes | yes | yes |
| Display (VOP2) | RKCP7403 | yes | yes | yes |
| GPU (Mali-G52) | RKCP7402 | yes | yes | yes |
| Thermal zones TZ00..TZ02, `_CRT` 115 C | (ThermalZone) | yes | yes | yes |
| Fan SSDT (RKFAN, 3 x PNP0C0B) | PNP0C0B | yes | yes | yes, all levels 100% |
| HDMI audio SAI6 | RKCP3004 | yes | yes | yes |
| SAI1 + ES8388 | RKCP3004, RKCP8388 | yes | not on main | yes, no jack detect |
| NPU (RKNPU SSDT) | RKCP3576 | yes | built, never installed (option hidden) | yes |
| Video decoder (RKVDEC SSDT) | RKCP0383 | yes | yes | yes |
| SATA | RKCP0161 | n/a | stub only | n/a |
| UFS | | n/a | n/a | missing: no Windows driver |
| I2C EEPROM (BL24C16F, I2C6 0x50) | | n/a | n/a | not declared: no Windows driver |

## Windows drivers

Every driver binds an ACPI `_HID` the three DSDTs share, so no INF needs a
board-specific hardware ID for the ROCK 4D. The drivers that read board
wiring take it from ACPI (`_CRS`, `_DSD`).

| Driver (repo) | Binds | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| rk3xi2c, rk3xgpio, rk3xspi, pl330dma (woa-rk3576) | RKCP3001, RKCP3002, RKCP3003, ARMH0330 | yes | yes | yes |
| dwcsdhc (eMMC), dwcmshc (SD) | RKCP0D40, RKCPFE2C | yes | yes | SD only |
| dwc_eqos (Ethernet) | RKCP6543 | yes | yes | yes |
| rk3xi2sbus, csaudiork3x (HDMI, headphones) | RKCP3004 | yes | HDMI | HDMI and headphones (no jack detect) |
| es8323 (ES8388) | RKCP8388 | yes | n/a on main | yes; untested without a jack GpioInt (see the test list) |
| rk3576-null (PMIC, RTC, pin controller, UART2) | RKCP0806, HYMB0001, RKCP3005, HISI0031 | yes | yes | yes, now that the PMIC is RKCP0806 |
| bcm4345bt | RKCP4345 | yes | n/a | n/a |
| gt9271 | RKCP9271 | yes | n/a | n/a |
| vdpu383 | RKCP0383 | yes | yes | yes |
| bcmwlsdio (branch `tspi3m-wifi-driver`) | SDIO card under RKCPFE2D | branch | could reuse it for the SYN43752 | n/a |
| Wi-Fi + Bluetooth on USB (AIC8800D80) | USB | n/a | n/a | missing: Mario Bălănică's aicwlan needs Windows 24H2, which this SoC cannot run; not ported |
| malidod, maliwddm, malikm (wddm-mali-g52) | RKCP7403, RKCP7402 | yes | yes | yes. HPD on GPIO4_C1 is the same on all three |
| rknpu (woa-rk3576-npu) | RKCP3576 | yes | option hidden | yes, with the NPU option on |
| Inbox: usbxhci, pci, ACPI thermal and fan | PNP0D10, PNP0A08, PNP0C0B | yes | yes | yes |

## What is left for the ROCK 4D, and why

* **GPU at 702 MHz.** Needs vdd_gpu_s0 at 775 mV. Raising it was approved
  for the TaishanPi 3M only; the rail report is on, so the Windows driver
  runs the ROCK 4D at 594 MHz (wddm branch `gpu702-default`).
* **Wi-Fi and Bluetooth under Windows.** USB AIC8800D80; no driver that runs
  on 23H2. Recorded, not ported.
* **Headphone jack detect.** A SARADC input; Windows has no ADC driver
  here. The headphone endpoint stays "plugged".
* **UFS, the DSI connector, the EEPROM, the LEDs.** No Windows driver, no
  panel, nothing to drive.
* **Armbian on USB or NVMe through "Armbian (U-Boot)".** The SPL boots only
  the SD card's FIT; the eMMC slot of the other boards does not exist here.

## Not on main for any board

`tspi3m-gpu-volt-read`. `fan-onoff` and `cpu-ask-once` were merged on
2026-11-25; the ROCK 4D gets both from its PCDs (`PcdPwmFanSpeedDefault`,
`PcdCpuMaxFreqSupported`), with no board code of their own.
