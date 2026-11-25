# ACPI

English | [简体中文](acpi.zh-CN.md)

What the firmware describes to an ACPI OS (Windows) on each board, and which
build setting or setup option decides it.

## Table mode per board

| Board | Default (`PcdConfigTableModeDefault`) | ACPI tables |
|---|---|---|
| ROCK 4D | device tree only | not built, so Windows cannot start |
| CM5-IO | ACPI + device tree | `Platform/ArmSoM/CM5IO/AcpiTables/` |
| TaishanPi 3M | ACPI + device tree | `Platform/LCKFB/TaishanPi3M/AcpiTables/` |

"Config Table Mode" in setup switches between ACPI + device tree, ACPI only
and device tree only where ACPI is built. The SoC tables (MADT, GTDT, PPTT,
MCFG, SPCR, DBG2, FADT) are shared and identical on both ACPI boards. Each
board's DSDT includes the SoC device files from
`Silicon/Rockchip/RK3576/AcpiTables/` and notes the device tree line each
board value comes from.

## Devices

C = CM5-IO, T = TaishanPi 3M. "Option" means a setup option, off by default
unless noted.

| Device | `_HID` | On | Decided by |
|---|---|---|---|
| eMMC | `RKCP0D40` | C T | always |
| SD card | `RKCPFE2C` | C T | always |
| GPIO, 5 banks | `RKCP3002` | C T | always |
| I2C | `RKCP3001` | C T | always |
| SPI | `RKCP3003` | C T | always |
| DMA (PL330), 3 | `ARMH0330` | C T | always |
| Ethernet (GMAC0) | `RKCP6543` | C T | always |
| USB-C (XHC0, DRD0) | `PNP0D10` | C T | always; on T, VBUS only for a sink attached at boot |
| USB-A (XHC1, DRD1) | `PNP0D10` | C T | always |
| XHC2, SATA | | C | CM5-IO only |
| PCIe | | C T | on T, hidden when the mini-PCIe link does not train in UEFI (usual with a USB modem) |
| GPU (Mali-G52) | `RKCP7402` | C T | always; firmware powers `PD_GPU` |
| Display (VOP2) | `RKCP7403` | C T | always; monitor EDID in `\_SB.GPU0.EDID` and on the GOP handle |
| HDMI audio (SAI6) | `RKCP3004` | C T | `PcdHdmiAudioPreset` (TRUE on both) |
| ES8388 codec, SAI1 | `RKCP8388`, `RKCP3004` | T | `PcdAnalogAudioPreset` |
| Touch GT9271 (`\_SB.I2C0.TCH0`) | `RKCP9271` | T | when the screen answers at boot |
| Bluetooth BCM4345C5 (`\_SB.BTH0`) | `RKCP4345` | T | board DSDT |
| RTC HYM8563 | `HYMB0001` | C T | board DSDT |
| PMIC RK806 (`\_SB.I2C1.PMC0`) | `RKCP0806`, `_CID` `PRP0001` | C T | board DSDT; no Windows driver (woa-rk3576 `drivers/null` names it) |
| Pin controller (`\_SB.PINC`) | `RKCP3005`, `_CID` `PRP0001` | C T | always; no Windows driver |
| UART2 (`\_SB.UAR2`) | `HISI0031` | C | CM5-IO DSDT (no T pin option reaches a connector) |
| Thermal zones (TSADC) | | C T | always; an untrusted reading shows as 100.0 C |
| Fan (`PNP0C0B`, `Fan.asl`) | | C T | option "Fan Control by ACPI Thermal Zone" |
| CPU idle states (SSDT `RKLPI`) | | C T | option "CPU Idle States" |
| NPU (`Npu.asl`) | `RKCP3576` | T | option "NPU (experimental)"; needs `PcdRk806I2cBase` |
| 40-pin UARTs (SSDT `RKUART40`) | `RKCP3006`, `_CID` `PRP0001` | T | option "40-pin UART (experimental)" with `PcdPin40Supported`; `SerCx-FriendlyName` gives COM ports for rk3xuart |
| RhProxy (SSDT `RKRHPX`, `\_SB.RHPX`) | `MSFT8000` | T | option "40-pin GPIO/I2C/SPI for apps (experimental)", same boards |
| Hardware watchdog (WDAT) | | C T | option "Hardware watchdog (experimental)", see below |
| Power button (SSDT `RKPWRKEY`, `\_SB.I2C1.PWRK`) | `RKCP806B` | none yet | option "Power button (RK806, experimental)"; needs `PcdAcpiPwrKeySupported` (ROCK 4D SW1, CM5-IO SW4) and `PcdRk806I2cBase`, which no board sets both of on `main`. For rk806pwrkey |
| LEDs (SSDT `RKLED`, `\_SB.LEDS`) | `RKCP1ED0` | T | option "Board LEDs for the OS (experimental)" with `PcdAcpiLedsSupported`; for rkled |
| SARADC and keys (SSDT `RKSARADC`, `\_SB.ADC0`) | `RKCP0ADC` | T | option "ADC keys for the OS (experimental)" with `PcdAcpiSaradcSupported` (on by default on T). Off, the SARADC clock is not set either. T's Recovery key reports HID System Power Down (`0x00010081`), Windows' power button. For rk3xsaradc |

Rule for optional SSDTs: refer (`External`) only to objects in the DSDT,
never to a device in another optional SSDT. SSDTs are installed in FV order,
so such an object may not exist yet when Windows loads the table (Windows did
not boot with an RhProxy SSDT that did this). When one table must know
whether another is installed, RK3576AcpiPlatformDxe patches a `Name` in it at
install time, as with `\_SB.RHPX.UAON`.

## Hardware watchdog

The WDAT describes WDT_NS (`0x2ACE0000`), the DesignWare watchdog TF-A leaves
to the normal world. Windows asks for 5 minutes, longer than the block can
count, so the table declares 1 s counts up to 600 and the firmware always
programs the longest period: 2^31 cycles of 24 MHz, twice (interrupt, then
reset), about 179 s. Register details: `Wdat.aslc` and
`RK3576AcpiPlatformDxe/Watchdog.c`. The serial log says at each boot whether
the last reset came from the watchdog.

Board test:

1. Option on, Windows idle and then loaded for an hour: no reset.
2. Hang Windows with woa-rk3576 `tools/wdthang`: reset within about
   3 minutes, next boot logs "last reset WAS the watchdog".
3. Option off, same hang: the board stays hung.

## Variables for OS drivers

| Variable | Set by | Read by |
|---|---|---|
| `GpuRail` (volatile) | `GpuRail.c`: BUCK5 voltage, GPU OTP bytes | wddm-mali-g52, to allow 702 MHz |
| `AcpiNpu` | the setup option, or `setvar AcpiNpu -guid 8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c -bs -rt -nv =01` in the UEFI Shell | the firmware at the next boot |

## Where to look

- DSDTs: `Platform/<vendor>/<board>/AcpiTables/Dsdt.asl`.
- SoC devices: `Silicon/Rockchip/RK3576/AcpiTables/*.asl`.
- Setup options and defaults: `Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/`
  (`RK3576DxeHii.vfr`, `ConfigTable.c`) and `Silicon/Rockchip/RK3576/RK3576.dec`.
- `acpiview` in the UEFI Shell dumps and checks the installed tables.
