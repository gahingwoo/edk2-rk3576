# Hardware

English | [简体中文](hardware.zh-CN.md)

Board facts the firmware depends on, as of `main` on 2026-11-24. What works
on each board is on the board pages: [ROCK 4D](../boards/rock4d.md),
[CM5-IO](../boards/cm5io.md), [TaishanPi 3M](../boards/tspi3m.md). Which
ACPI devices and settings the firmware turns on is in [ACPI](acpi.md).

The bring-up notes that used to be on this page are in
[history](../history/2026-11-22-hardware-before-rewrite.md).

## Common to all boards

| | |
|---|---|
| SoC | Rockchip RK3576: 4x Cortex-A72 + 4x Cortex-A53, ARMv8.0 |
| Interrupt controller | GIC-400 (GICv2). No redistributors and no ITS. |
| DRAM base | `0x40000000` |
| Boot chain | BootROM, U-Boot SPL, TF-A BL31 v2.14.0, EDK2 as BL33 |
| Serial console | UART0 at `0x2AD40000`, 1500000 8N1, no flow control |
| PMIC | RK806 on I2C1, address `0x23` |
| Thermal shutdown | TSADC hardware shutdown armed by the firmware at 120 C (`PcdTsadcTshutEnable`, `PcdTsadcTshutTemp`), plus each channel's OTP trim |

Because the SoC is ARMv8.0, Windows 11 24H2 and later cannot run on it.

## Radxa ROCK 4D

| | |
|---|---|
| Firmware lives on | 16 MB SPI NOR. v1.12 boards use an XMC XM25QU128C, earlier ones a Macronix part; the SPL supports both from 0.2.1 on. |
| MaskROM port | The upper USB 3.0 USB-A port (the USB-C port carries power only) |
| Storage | UFS, microSD, NVMe over PCIe. No eMMC. |
| PCIe | pcie0 on combphy0, to a 16-pin FPC connector; an NVMe drive needs an M.2 adapter on it. PERST# GPIO2_B4, 3.3 V switch GPIO2_D3. |
| USB | The USB-C connector is power input only. DRD0 is the upper USB 3.0 port (USBDP PHY lanes 0 and 1), DRD1 an FE1.1s hub for the other three USB-A ports and the USB WiFi, plus the lower port's SuperSpeed through combphy1. |
| Ethernet PHY | Realtek RTL8211F-CG at MDIO address 1, 25 MHz from the SoC, `rgmii-id`. The firmware writes only its LED register. |
| Tested SKU | 12 GB LPDDR5 |

ACPI: the image installs ACPI tables next to the device tree, as the CM5-IO
does (ConfigTableMode ACPI + FDT, switchable in the front-page menu). Before
that the ROCK 4D was device tree only. The SoC tables are the shared ones the
other two boards build. The DSDT (`Platform/Radxa/ROCK4D/AcpiTables/Dsdt.asl`)
is the CM5-IO's minus the eMMC (this board has none; its pins are the SPI
NOR's) and the disabled XHC2 and SATA stubs; UFS is not described, having no
Windows driver. Its comments give the device tree line or schematic page for
each board value. The PCIe root complex is hidden from ACPI when its link does
not train in UEFI. **None of it has been run on a ROCK 4D**, with or without
an operating system; it compiles, which is all that is known.

## ArmSoM CM5-IO

| | |
|---|---|
| Firmware lives on | SD card or eMMC. The carrier's SPI NOR is 64 KB, too small. |
| Boot order | The BootROM tries the eMMC before the SD card. |
| MaskROM port | USB-C |
| Storage | eMMC on the CM5 module, microSD, NVMe over PCIe |
| Ethernet PHY | Motorcomm YT8531C, crystal-less: it takes its 25 MHz reference from the SoC. |
| USB-A | Behind an onboard 4-port hub |
| USB-C | FUSB302; the firmware reads the plug orientation from it and brings up the USBDP PHY's USB3 half at ExitBootServices. SuperSpeed has not trained there. |
| Audio | ES8388 on I2C0 and SAI1, to a 3-pin header (CN3), no jack; not described by the firmware. HDMI audio on SAI6 is (`PcdHdmiAudioPreset` TRUE). |
| Tested SKU | 4 GB |

## LCKFB TaishanPi 3M

Everything here is from one board, first booted 2026-11-12.

| | |
|---|---|
| BootROM boot order | eMMC, then USB. It does not try the SD card: a valid idblock on SD produced no output at all (one card tried). The order is set by the divider on `SARADC_IN0_BOOT`. |
| Firmware lives on | SPL on the eMMC at sector 64. The FIT (BL31 + EDK2) on an SD card or the eMMC; the SPL tries SD first. |
| MaskROM | No button. Short the two pads marked `MSK`, directly under the `RK3576` silkscreen, while powering on. The download port is the USB-C marked `OTG+DP`. F4 in the boot countdown also resets into MaskROM. |
| Storage | 58 GiB eMMC (62008590336 bytes), microSD. The mini-PCIe slot carries PCIe, SATA or a USB modem. |
| Ethernet PHY | Maxio MAE0621A-Q3C, ID `7b74.4412`, at MDIO address 1 and answering address 0. Delays are set by straps: RX delay in the PHY, TX delay `0x24` in the MAC. The firmware writes nothing to it. |
| USB-A | Four ports behind an onboard hub on DRD1. The firmware's device tree marks their VBUS supplies always-on (the mainline tree leaves them to nobody, and Linux switched them off). |
| USB-C | FUSB302 on I2C2; VBUS switch SY6280 enabled by GPIO0_B6. At boot and again at ExitBootServices the firmware reads CC and turns VBUS on only when a sink (Rd) is attached; with a charger or a PC (Rp) it stays off. No OS driver watches the port afterwards, so a device plugged in after boot gets no VBUS. No VCONN. |
| DSI screen | LCKFB's 800x1280 screen, part C41409463: ILI9881C panel on DSI, reset on GPIO2_B4, backlight on PWM2 channel 2 (GPIO3_C5), mounted so that landscape is the panel's left edge up. Lit by the firmware; an OS gets it in portrait. When the screen answers, the firmware's device tree enables its pwm-backlight at the setup brightness and, with force GOP off, the DSI host and panel (`rocknix,generic-dsi`, so an Armbian kernel); see `FdtFixupDsiPanel`. Plug or unplug the ribbon only with the power off. |
| Touch | Goodix GT9271 at 0x5d on I2C0 (GPIO0_C1, GPIO0_C2). The firmware reads it for the menus and uses its presence to tell whether the screen is attached. It never writes the controller's configuration. |
| HDMI | GPIO4_C6 enables the level shifters. HPD reaches the SoC through two transistor stages (Q17, Q16), so it is not inverted: GPIO4_C1 reads 1 with a monitor attached. The connector's 5 V comes from the system rail through a diode and is not switched. |
| Audio | ES8388 on I2C3 0x11, SAI1 (DMA0 requests 2 and 3, lane 0 out on SDO2), headphone amplifier enable GPIO4 PB2, jack detect GPIO4 PB0, mclk_sai1 12.288 MHz from clk_audio_frac_1. Set up by `Tspi3mAudio.c` when `PcdAnalogAudioPreset` is TRUE (it is for this board). |
| Bluetooth | AP6256 (BCM4345C5) on UART6 (uart6m1, GPIO2 PD2 to PD5), sclk_uart6 24 MHz from xin24m; BT_REG_ON GPIO1 PC7, BT_WAKE GPIO1 PD4. The 32 kHz sleep clock comes from the HYM8563's CLKOUT, on as the chip leaves the factory; the firmware does not write it. |
| RTC | HYM8563 on I2C2 0x51. On this board it loses its time at every power-off and reset (no battery), so the clock is set from the network (NTP) or by the OS. |
| GPU supply | `vdd_gpu_s0` = RK806 BUCK5, 750 mV at power-on. The firmware reads it, raises it to 775 mV for the 702 MHz operating point when the setup option "GPU voltage for 702 MHz (775 mV)" is on (`PcdGpuRail775`, on by default on this board), and publishes the reading as the `GpuRail` variable for the Windows GPU driver. The driver runs 702 MHz when that reading is 775 mV or more, else 594 MHz. |
| Fan | 2-pin connector CN2 on PWM2 channel 3 (GPIO3_D0), high = on, switched on its supply by a P-MOSFET. The firmware starts it at 100% (`PcdPwmFanSpeedDefault`) with a 2 kHz period (`PcdPwmFanPeriodNs` 500000); at 60% the 2 kHz switching was audible. Under Windows the fan stays at 100% unless "Fan Control by ACPI Thermal Zone" is enabled in setup (off by default). Under Linux the firmware's device tree describes the PWM channel but no `pwm-fan`, so the fan stays where the firmware left it. |
| Tested SKU | 4 GB LPDDR5, 64 GB eMMC |
