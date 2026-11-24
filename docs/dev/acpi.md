# ACPI

What the firmware tells an ACPI operating system (Windows) on each board,
and which build setting or setup option decides it. As of `main` on
2026-11-24.

## Table mode per board

| Board | Default (`PcdConfigTableModeDefault`) | ACPI tables built |
|---|---|---|
| ROCK 4D | device tree only | no; the board builds FDT-only, so Windows cannot start on it |
| CM5-IO | ACPI + device tree | yes, `Platform/ArmSoM/CM5IO/AcpiTables/` |
| TaishanPi 3M | ACPI + device tree | yes, `Platform/LCKFB/TaishanPi3M/AcpiTables/` |

"Config Table Mode" on the ACPI / Device Tree setup page switches between
ACPI + device tree, ACPI only and device tree only where ACPI is built.

The SoC tables (MADT, GTDT, PPTT, MCFG, SPCR, DBG2, FADT) are shared and
compile to the same bytes on both ACPI boards. Each board has its own DSDT,
which includes the SoC device files from `Silicon/Rockchip/RK3576/AcpiTables/`
and puts the board values next to the device tree line they come from.

## Devices

| Device | `_HID` | CM5-IO | TaishanPi 3M | Decided by |
|---|---|---|---|---|
| eMMC | `RKCP0D40` | yes | yes | always |
| SD card | `RKCPFE2C` | yes | yes | always |
| GPIO, 5 banks | `RKCP3002` | yes | yes | always |
| I2C | `RKCP3001` | yes | yes | always |
| SPI | `RKCP3003` | yes | yes | always |
| DMA (PL330), 3 | `ARMH0330` | yes | yes | always |
| Ethernet (GMAC0) | `RKCP6543` | yes | yes | always |
| USB-C (XHC0, DRD0) | `PNP0D10` | yes | yes | always; on the TaishanPi 3M with VBUS only for a sink attached at boot ([hardware](hardware.md)) |
| USB-A (XHC1, DRD1) | `PNP0D10` | yes | yes | always |
| XHC2, SATA | | yes | no | CM5-IO only |
| PCIe | | yes | yes | the TaishanPi 3M's mini-PCIe root complex is hidden when its link does not train in UEFI (the usual case with a USB modem in the slot) |
| GPU (Mali-G52) | `RKCP7402` | yes | yes | always; the firmware powers `PD_GPU` |
| Display (VOP2) | `RKCP7403` | yes | yes | always |
| HDMI audio (SAI6) | `RKCP3004` | yes | yes | `PcdHdmiAudioPreset`, TRUE on both boards |
| Headphone codec ES8388 | `RKCP8388` | no | yes | `PcdAnalogAudioPreset`, TRUE on the TaishanPi 3M |
| SAI1 (analog audio) | `RKCP3004` | no | yes | the same |
| Touch GT9271 (`\_SB.I2C0.TCH0`) | `RKCP9271` | no | when the screen answers at boot | screen detection |
| Bluetooth BCM4345C5 (`\_SB.BTH0`) | `RKCP4345` | no | yes | TaishanPi 3M DSDT |
| RTC HYM8563 | `HYMB0001` | yes | yes | board DSDT |
| Thermal zones (TSADC read-out) | | yes | yes | always; a reading that cannot be trusted shows as 100.0 C |
| Fan (`PNP0C0B`, `Fan.asl`) | | yes | yes | setup "Fan Control by ACPI Thermal Zone", off by default |
| CPU idle states (SSDT `RKLPI`) | | yes | yes | setup "CPU Idle States", off by default |
| NPU (`Npu.asl`) | `RKCP3576` | no | yes | setup "NPU (experimental)", off by default; offered only on a board that names its RK806's I2C bus (`PcdRk806I2cBase`), which on 2026-11-24 is the TaishanPi 3M |

The EDID of the monitor is published as `\_SB.GPU0.EDID` for the Windows
display driver, and on the GOP handle.

## Variables for OS drivers

| Variable | Set by | Read by |
|---|---|---|
| `GpuRail` (volatile) | `GpuRail.c`: BUCK5 voltage, GPU OTP bytes | wddm-mali-g52, to decide whether 702 MHz is allowed |
| `AcpiNpu` | the setup option; can also be set from the UEFI Shell: `setvar AcpiNpu -guid 8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c -bs -rt -nv =01` | the firmware at the next boot |

## Where to look

- DSDTs: `Platform/<vendor>/<board>/AcpiTables/Dsdt.asl`.
- SoC devices: `Silicon/Rockchip/RK3576/AcpiTables/*.asl`.
- Setup options and their defaults: `Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/`
  (`RK3576DxeHii.vfr`, `ConfigTable.c`) and `Silicon/Rockchip/RK3576/RK3576.dec`.
- `acpiview` in the UEFI Shell dumps and checks the installed tables.
