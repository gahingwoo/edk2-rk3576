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

Skeleton: the firmware builds for this board and has not been booted on it.
The facts below are from the board's mainline device tree (Linux 7.3), not
from a firmware run. The board was provided by LCKFB.

| | |
|---|---|
| Firmware lives on | SD card or eMMC. No SPI NOR. |
| Storage | eMMC, microSD. The mini-PCIe slot carries PCIe, SATA or a USB modem. |
| Ethernet PHY | ID `7b74.4412`, rgmii-rxid with a MAC-side TX delay of `0x24`. The firmware has no init code for this PHY yet. |
| USB-A | Four ports behind an onboard hub on DRD1 |
| USB-C | FUSB302 on I²C2. The firmware does not switch VBUS on. |
| HDMI | GPIO4_C6 enables the level shifters |
