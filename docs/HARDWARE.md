# Hardware

Board facts this firmware depends on. What works on each board is in the
[README](https://github.com/gahingwoo/edk2-rk3576#boards).

## Common to all boards

| | |
|---|---|
| SoC | Rockchip RK3576: 4× Cortex-A72 + 4× Cortex-A53, ARMv8.0 |
| Interrupt controller | GIC-400 (GICv2). No redistributors and no ITS. |
| DRAM base | `0x40000000` |
| Boot chain | BootROM → U-Boot SPL → TF-A BL31 v2.14.0 → EDK2 as BL33 |
| Serial console | UART0 at `0x2AD40000`, 1500000 8N1, no flow control |
| PMIC | RK806 on I²C1, address `0x23` |

Because the SoC is ARMv8.0, Windows 11 24H2 and later cannot run on it.

## Radxa ROCK 4D

| | |
|---|---|
| Firmware lives on | 16 MB SPI NOR |
| Storage | UFS, microSD, NVMe over PCIe. No eMMC. |
| Tested SKU | 12 GB LPDDR5 |

## ArmSoM CM5-IO

| | |
|---|---|
| Firmware lives on | SD card or eMMC. The carrier's SPI NOR is 64 KB, too small. |
| Boot order | The BootROM tries the eMMC before the SD card. |
| Storage | eMMC on the CM5 module, microSD, NVMe over PCIe |
| Ethernet PHY | Motorcomm YT8531C, crystal-less: it takes its 25 MHz reference from the SoC. |
| USB-A | Behind an onboard 4-port hub |
| Tested SKU | 4 GB |

## LCKFB TaishanPi 3M

Early support. First booted 2026-11-12; everything under "seen working" is
from that one boot. The board was provided by LCKFB.

| | |
|---|---|
| BootROM boot order | eMMC, then USB. It does not try the SD card: a valid idblock on SD produced no output at all (one card tried). The order is set by the divider on `SARADC_IN0_BOOT`. |
| Firmware lives on | SPL on the eMMC at sector 64. The FIT (BL31 + EDK2) on an SD card or the eMMC; the SPL tries SD first. |
| MaskROM | No button. Short the two pads marked `MSK`, directly under the `RK3576` silkscreen, while powering on. The download port is the USB-C marked `OTG+DP`. |
| Storage | 58 GiB eMMC, microSD. The mini-PCIe slot carries PCIe, SATA or a USB modem. |
| Ethernet PHY | Maxio MAE0621A-Q3C, ID `7b74.4412`, at MDIO address 1 and answering address 0. Delays are set by straps: RX delay in the PHY, TX delay `0x24` in the MAC. The firmware writes nothing to it. |
| USB-A | Four ports behind an onboard hub on DRD1 |
| USB-C | FUSB302 on I²C2. The firmware does not switch VBUS on. |
| HDMI | GPIO4_C6 enables the level shifters. HPD reaches the SoC through two transistor stages (Q17, Q16), so it is not inverted: measured from Linux, GPIO4_C1 reads 1 with a monitor attached and 0 without, and follows the cable at once. The connector's 5 V comes from the system rail through a diode and is not switched. |
| Fan | 2-pin connector CN2 on PWM2 channel 3 (GPIO3_D0), high = on, switched on its supply by a P-MOSFET. The firmware starts it at 100% (`PcdPwmFanSpeedDefault`) and leaves it running; at 60% the 2 kHz switching was audible, so partial duty now runs at 25 kHz (`PcdPwmFanPeriodNs`, not yet heard on the board). Under Windows the fan stays at 100% unless "Fan Control by ACPI Thermal Zone" is enabled in setup (off by default), which installs AcpiTables/Fan.asl: 40/70/100% above 45/60/75 C on the big core zone. Under Linux, `pwm-fan` takes it over. |
| Tested SKU | 4 GB LPDDR5, 64 GB eMMC |

Seen working: SPL from eMMC loading the FIT from SD, TF-A v2.14, the UEFI
front page, HDMI at 2560x1440 from the sink's EDID, eMMC at 52 MHz, SD card
with the NV variable store on it, the USB hub, PHY identification.

HPD used to read low under the firmware with a monitor attached, and every
boot waited 5 seconds before forcing the display on. The pads' reset pull was
never cleared, because the firmware wrote the wrong register; fixed, and HPD
now reads high at once.

UEFI variables are kept across a reset when booting from SD: the store is
part of the FIT and the SPL loads it back. Seen for a firmware reset; a
power cycle reads the same bytes from the card and has not been tried
separately.

Also seen working, reported from the board: a USB keyboard in the UEFI menus,
booting from a USB stick, keyboard and mouse in Windows Setup, and Ethernet
getting a DHCP address in UEFI.

Not tried: booting an installed operating system, Linux through UEFI, PCIe.

ACPI: the image installs ACPI tables next to the device tree, as the CM5-IO
does (ConfigTableMode ACPI + FDT, switchable in the front-page menu).
`acpiview` reports no errors on the board, and Windows 11 23H2 Setup starts
from a USB stick. Setup does not see the eMMC: Windows has no inbox driver
for it, and the driver in woa-rk3576 has not been tried on this board.
Nothing is installed or booted from the eMMC yet. The SoC tables (MADT, GTDT, PPTT, MCFG, SPCR, DBG2,
FADT) are the CM5-IO's and compile to the same bytes. The DSDT
(`Platform/LCKFB/TaishanPi3M/AcpiTables/Dsdt.asl`) is the CM5-IO's minus
the USB-C controller (XHC0) and the disabled XHC2 and SATA stubs; its
comments give the device tree line for each board value. The mini-PCIe
root complex is hidden from ACPI when its link does not train in UEFI,
which is the usual case with a USB modem in the slot.
