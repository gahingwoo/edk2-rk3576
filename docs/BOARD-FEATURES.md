# Board features: TaishanPi 3M, CM5-IO, ROCK 4D

English | [简体中文](BOARD-FEATURES.zh-CN.md)

Inventory of 2026-11-25 (ROCK 4D column updated 2026-11-26), written for the
feature freeze: every setup option, ACPI device, firmware feature and Windows
driver match, per board, against the LCKFB TaishanPi 3M, the board everything
else is measured against.

* **TaishanPi 3M** and **CM5-IO**: what `main` (0b03605) builds. The CM5-IO
  column does not include branch `cm5io-parity`, which is being worked on in
  parallel and will change it.
* **ROCK 4D**: what `main` builds. It has all of branch `rock4d-parity`
  except its SPL, which the "Armbian (U-Boot)" one-shot needs.
* Branches that are not on `main` for any board are listed at the end.

Marks: **yes** = present; **missing** = the hardware exists and the firmware
or ACPI does not have it; **n/a** = no such hardware on that board. "Not run"
means built and never run on that board. In the ROCK 4D column, **verified**
means it ran under Windows 11 23H2 on a 12 GB ROCK 4D on 2026-11-26 (from
NVMe); a ROCK 4D cell without it has not run on the board.

Hardware facts come from each board's mainline device tree
(`devicetree/mainline/`), its schematic (ROCK 4D V1.12, CM5-IO 1V1, LCKFB
TaishanPi 3M v1.4) and the vendor kernel tree; the ROCK 4D's are cited in
`Platform/Radxa/ROCK4D/`.

## Setup options

| Option (page) | Gate | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| Config Table Mode (ACPI / Device Tree) | every board | yes, ACPI + DT | yes, ACPI + DT | yes, ACPI + DT, verified |
| ACPI PCIe ECAM compatibility | ACPI on | yes | yes | yes |
| Fan Mode | `PcdAcpiFanControlSupported` | yes, Always on | yes, Always on | yes, Always on, not run |
| CPU Idle States (`_LPI`), off by default; on, Windows bugchecks 0x5C at boot | every board | yes | yes | yes |
| NPU (experimental) | `PcdRk806I2cBase` != 0 | yes | missing (RK806 on I2C1 0x23 exists) | yes, verified |
| Video decoder (VDPU383) | every board | yes | yes | yes, verified |
| SDIO Wi-Fi | `PcdAcpiSdioWifiSupported` | yes (AP6256) | missing on main (SYN43752 on SDIO; no Windows driver either) | n/a: Wi-Fi is a USB module (FCU760K, AIC8800D80) |
| Device tree: compat mode, Force UEFI GOP, DTB override, overlays, fixups | DT on | yes | yes | yes |
| CPU Maximum Frequency | `PcdCpuMaxFreqSupported` | yes, 1608 MHz | missing (same RK806 rails) | yes, 1608 MHz |
| GPU voltage for 702 MHz (775 mV) | `PcdGpuRail775Supported` | yes, on | missing on main (branch `cm5io-parity`: yes, off) | yes, off, not run |
| CPU frequency asked once at first boot (with a screen) | `PcdCpuMaxFreqSupported` | yes | no (option not offered) | yes |
| 40-pin header: UART, GPIO/I2C/SPI for apps (off by default) | `PcdPin40Supported` | yes | missing on main (branch `cm5io-parity`: yes) | yes, not run |
| Board LEDs for the OS (off by default) | `PcdAcpiLedsSupported` | yes | missing on main (branch `cm5io-parity`: yes) | yes, not run |
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
| Power off through the RK806 (Shut Down, `reset -s`, Windows) | yes | missing (RK806 on I2C1 0x23) | yes |
| GPU rail report (`GpuRail` variable, reads only) | yes | missing | yes |
| GPU rail raised to 775 mV for 702 MHz | yes, setup option on by default | setup option off by default on branch `cm5io-parity` | setup option, off by default, not run; on, the Windows driver runs 702 MHz, off 594 MHz |
| TSADC read-out and TSHUT at 120 C | yes | yes | yes |
| Fan started at 100% | yes (CN2, PWM2 ch3) | yes (CN32, PWM2 ch6, polarity disputed) | yes, J16, PWM2 ch5, GPIO4_C5, normal polarity; not run |
| HDMI audio preset (SAI6, 48 kHz) | yes | yes | yes, verified |
| Analog audio preset (ES8388 on SAI1 + I2C3) | yes, with amplifier and jack detect | not on main | yes, verified: headphones and the headset microphone; no amplifier (none fitted); jack detect (SARADC channel 3, rk3xsaradc) not run |
| "Armbian (U-Boot)" one-shot | yes (request on the eMMC) | missing (its SPL is not rebuilt by `build-spl.sh`) | missing on main: needs the SPL of branch `rock4d-parity`, left out until a board has booted it |
| SPL built from pinned sources (`build-spl.sh`) | yes | no (prebuilt `idblock_cm5io.bin`) | no on main (prebuilt 2026.04); U-Boot v2026.10 on branch `rock4d-parity` |
| MIPI DSI screen | yes | n/a | missing: connector exists, no panel |
| USB-C VBUS from the FUSB302 | yes | yes | n/a: USB-C is power input only |
| Bluetooth power-up for the OS | yes (UART6) | missing | n/a in ACPI: Bluetooth is on the USB Wi-Fi module |

## ACPI devices

| Device | `_HID` | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| CPUs, `_LPI` (RKLPI / RKWFI SSDT) | ACPI0007 | yes | yes | yes |
| PCIe root complexes (PCI0, PCI1) | PNP0A08 | yes (hidden without a link) | yes | yes (hidden without a link), verified (NVMe) |
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
| GMAC0 | RKCP6543 | yes | yes | yes, verified |
| Display (VOP2) | RKCP7403 | yes | yes | yes, verified |
| GPU (Mali-G52) | RKCP7402 | yes | yes | yes, verified |
| Thermal zones TZ00..TZ02, `_CRT` 115 C | (ThermalZone) | yes | yes | yes |
| Fan SSDT (RKFAN, 3 x PNP0C0B) | PNP0C0B | yes | yes | yes, all levels 100%, not run |
| HDMI audio SAI6 | RKCP3004 | yes | yes | yes, verified |
| SAI1 + ES8388 | RKCP3004, RKCP8388 | yes | not on main | yes, verified; jack state from the SARADC (`rockchip,hp-det-saradc`), not run |
| SARADC (Saradc.asl, ADC keys and ADC jack) | RKCP0ADC | branch `saradc-acpi`: Recovery key, channel 1 | Maskrom key, channel 0 | Maskrom key, channel 0; jack, channel 3; not run |
| NPU (RKNPU SSDT) | RKCP3576 | yes | built, never installed (option hidden) | yes, verified |
| Video decoder (RKVDEC SSDT) | RKCP0383 | yes | yes | yes, verified |
| SATA | RKCP0161 | n/a | stub only | n/a |
| UFS | | n/a | n/a | missing: no Windows driver |
| LEDs (RKLED SSDT) | RKCP1ED0 | yes | missing on main (branch `cm5io-parity`: yes) | yes, setup option, not run |
| I2C EEPROM (BL24C16F, `\_SB.I2C6.EEP0`) | RKCP2416 | n/a | n/a | yes, read only, for woa-rk3576 rkeeprom; not run |

## Windows drivers

Every driver binds an ACPI `_HID` the three DSDTs share, so no INF needs a
board-specific hardware ID for the ROCK 4D. The drivers that read board
wiring take it from ACPI (`_CRS`, `_DSD`).

| Driver (repo) | Binds | TaishanPi 3M | CM5-IO | ROCK 4D |
|---|---|---|---|---|
| rk3xi2c, rk3xgpio, rk3xspi, pl330dma (woa-rk3576) | RKCP3001, RKCP3002, RKCP3003, ARMH0330 | yes | yes | yes |
| dwcsdhc (eMMC), dwcmshc (SD) | RKCP0D40, RKCPFE2C | yes | yes | SD only |
| dwc_eqos (Ethernet) | RKCP6543 | yes | yes | verified, 1 Gbps (firmware with the RTL8211F delay fix 1fa5e6d) |
| rk3xi2sbus, csaudiork3x (HDMI, headphones) | RKCP3004 | yes | HDMI | verified: HDMI, headphones, headset microphone (jack detect through rk3xsaradc not run) |
| rk3xsaradc, tools/adctest (woa-rk3576 branch `rk3xsaradc`) | RKCP0ADC | Recovery key as a vendor HID button | Maskrom key as a vendor HID button | Maskrom key; headphone jack state to es8323. Not run |
| es8323 (ES8388) | RKCP8388 | yes | n/a on main | verified; needs woa-rk3576 `es83-optional-irq` or later (an older es8323 creates an interrupt this `_CRS` does not have); `_DSD` `rockchip,output-route` 1, `rockchip,input-select` 1 (OUT1 to J18, LIN1 headset microphone) |
| rk3576-null (PMIC, RTC, pin controller, UART2) | RKCP0806, HYMB0001, RKCP3005, HISI0031 | yes | yes | yes, now that the PMIC is RKCP0806 |
| bcm4345bt | RKCP4345 | yes | n/a | n/a |
| gt9271 | RKCP9271 | yes | n/a | n/a |
| vdpu383 | RKCP0383 | yes | yes | verified |
| bcmwlsdio (branch `tspi3m-wifi-driver`) | SDIO card under RKCPFE2D | branch | could reuse it for the SYN43752 | n/a |
| Wi-Fi + Bluetooth on USB (AIC8800D80) | USB | n/a | n/a | missing: no driver on main (Mario Bălănică's aicwlan needs a newer WiFiCx than 23H2 has) |
| malidod, maliwddm, malikm (wddm-mali-g52) | RKCP7403, RKCP7402 | yes | yes | verified, 1920x1080 at 60 Hz. HPD on GPIO4_C1 is the same on all three |
| rknpu (woa-rk3576-npu) | RKCP3576 | yes | option hidden | verified, with the NPU option on |
| Inbox: usbxhci, pci, ACPI thermal and fan | PNP0D10, PNP0A08, PNP0C0B | yes | yes | yes |

## What is left for the ROCK 4D, and why

Run on 2026-11-26: Windows 11 23H2 from NVMe with Ethernet, GPU, audio, NPU
and the video decoder (marked verified above). Not run yet:

* **GPU at 702 MHz: opt-in, not run.** vdd_gpu_s0 is BUCK5 of the same
  RK806S-5 as on the TaishanPi 3M (schematic V1.12 p25 VOUT5/FB5 to
  VDD_GPU_S0, I2C1_M0 on GPIO0_B2/B3 p5; `rk3576-rock-4d.dts` dcdc-reg5,
  550000 to 900000 uV, mali-supply of `&gpu`). "GPU voltage for 702 MHz
  (775 mV)" is off by default: the Windows driver runs 594 MHz until the
  option is turned on and the board test below has passed.
* **Wi-Fi and Bluetooth under Windows.** USB AIC8800D80; no driver on
  `main`.
* **Headphone jack detect.** A SARADC input (channel 3, the J18
  microphone contact). Built, not run: \_SB.ADC0 with thresholds computed
  from the schematic (plugged below 1.45 V, unplugged above 1.52 V), the
  woa-rk3576 rk3xsaradc driver, and es8323 following it. Without the
  driver the headphone endpoint stays "plugged".
* **The LEDs, the EEPROM, the 40-pin header, fan control on J16.** In the
  ACPI tables (LEDs and header off by default), not run.
* **UFS, the DSI connector.** No Windows driver, no panel.
* **"Armbian (U-Boot)".** Needs the SPL of branch `rock4d-parity`, not on
  `main`. That SPL boots only the SD card's FIT; there is no eMMC slot.

## Board test: GPU voltage for 702 MHz

With the fan on:

1. Set "GPU voltage for 702 MHz (775 mV)" to On and restart. The serial log
   shows `GpuRail: raised BUCK5 0x28 -> 0x2C, read back 775000 uV`. If it
   shows `not raising BUCK5: ...` instead, note the reason (this board's
   RK806 has not been read yet); nothing was written.
2. Windows, with no `GpuClk` stored and `GpuClkHigh` not set: `m5test clk`
   (or the GPU clock perf counter) shows 702 MHz.
3. FurMark 60 s: no TDR; note the score and the peak GPU and CPU
   temperatures.
4. Set the option to Off and restart: the log shows `lowered BUCK5 0x2C ->
   0x28, read back 750000 uV`, and Windows runs 594 MHz.

## Not on main for any board

`tspi3m-gpu-volt-read`; the ROCK 4D SPL of `rock4d-parity`. `fan-onoff` and
`cpu-ask-once` were merged on 2026-11-25; the ROCK 4D gets both from its PCDs
(`PcdPwmFanSpeedDefault`, `PcdCpuMaxFreqSupported`), with no board code of
their own.
