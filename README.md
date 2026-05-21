# edk2-rk3576 — UEFI for Rockchip RK3576 Boards

[![Status](https://img.shields.io/badge/status-working-brightgreen)]()
[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![Board](https://img.shields.io/badge/board-Radxa%20ROCK%204D-green)]()
[![Board](https://img.shields.io/badge/board-FriendlyElec%20NanoPi%20M5-blue)]()
[![Board](https://img.shields.io/badge/board-ArmSoM%20CM5--IO-orange)]()
[![Board](https://img.shields.io/badge/board-ArmSoM%20CM5%20RPI--CM4--IO-purple)]()
[![Board](https://img.shields.io/badge/board-Waveshare%20CM4--IO--BASE--B-red)]()
[![License](https://img.shields.io/badge/license-MIT%20%2B%20BSD--2--Clause--Patent-lightgrey)]()
[![Flash](https://img.shields.io/badge/flash-WebUSB%20browser%20tool-informational)](https://gahingwoo.github.io/edk2-webflash/)

A working **EDK2 / TianoCore UEFI** port for **Rockchip RK3576** single-board
computers. Primary target is the **Radxa ROCK 4D**, with initial support for
the **FriendlyElec NanoPi M5**, **ArmSoM CM5-IO**, **ArmSoM CM5 RPI-CM4-IO**,
and **Waveshare CM4-IO-BASE-B**. The ROCK 4D port includes a matching
**TF-A BL31 + U-Boot SPL** boot stack, verified on real hardware
(12 GB LPDDR5 SKU) booting **Fedora 44 aarch64** to GNOME desktop.

> **Status:** UEFI front page and menus render natively over **HDMI at
> 2560×1440@60 (QHD)** from EDID. Chain-loads GRUB → Linux from USB.
> Full RAM, USB 2.0, USB 3.0 SuperSpeed, eMMC, SD, Ethernet and SMBIOS
> are functional. **PCIe link training does not complete** under UEFI
> (controller is probed, but LTSSM never reaches L0).

---

## Screenshots

| | |
|---|---|
| ![UEFI front page on monitor](docs/imgs/monitor.png) | ![GRUB on USB](docs/imgs/grub.png) |
| TianoCore UEFI front page — 2560×1440@60 QHD via HDMI, native on-board VOP2 + HDPTX PHY | GRUB on the Fedora 44 USB stick |

| | |
|---|---|
| ![Fedora live + lsusb](docs/imgs/fedora.png) | ![Fedora 44 GNOME — System Details](docs/imgs/desktop.png) |
| Fedora live shell — `xhci-hcd 5000M` USB 3.0 root hub | GNOME *About* — **Radxa ROCK 4D**, **11.5 GiB** RAM from SMBIOS |

---

## Documentation → [`docs/`](docs/)

| Document                                       | What it covers                            |
|------------------------------------------------|-------------------------------------------|
| [docs/REPO_LAYOUT.md](docs/REPO_LAYOUT.md)     | What lives where in this repository       |
| [docs/HARDWARE.md](docs/HARDWARE.md)           | Hardware verification matrix, UART setup  |
| [docs/FLASHING.md](docs/FLASHING.md)           | Flashing the prebuilt firmware            |
| [docs/BUILDING.md](docs/BUILDING.md)           | Building the UEFI image from source       |
| [docs/SPI_LAYOUT.md](docs/SPI_LAYOUT.md)       | 16 MB SPI NOR layout & FIT contents       |
| [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)   | Limitations, gotchas and workarounds      |

---

## TL;DR

### Flash the prebuilt UEFI image

#### Option 1 — Browser (no tools required)

Open **[gahingwoo.github.io/edk2-webflash](https://gahingwoo.github.io/edk2-webflash/)** in Chrome or Edge.
Hold the MaskROM button, plug in USB-C, follow the on-screen steps.
No drivers or command-line tools needed on Linux / macOS.
(Windows: install WinUSB for the device via [Zadig](https://zadig.akeo.ie/).)

#### Option 2 — rkdeveloptool (MaskROM mode)

```bash
rkdeveloptool db   binaries/rk3576_ddr.bin
rkdeveloptool wl 0 rock4d-spi-edk2.img
rkdeveloptool rd
```

Serial console: **1,500,000 8N1** on the 3-pin debug header.
See [docs/FLASHING.md](docs/FLASHING.md) for details and recovery.

### Build from source

```bash
cd edk2_port
# Clone third-party trees once (see docs/BUILDING.md for the exact commands)

# Build for ROCK 4D (default)
bash build_rock4d_uefi.sh
# Output: output/ROCK4D/ROCK4D-spi-edk2.img

# Build for a specific platform
bash build_rock4d_uefi.sh --config configs/armsom-cm5-io.conf
# Output: output/CM5IO/CM5IO-spi-edk2.img
```

Full instructions in [docs/BUILDING.md](docs/BUILDING.md).

---

## What works

* **CPU / RAM**: 8× Cortex-A72/A53, 12 GB LPDDR5 @ 2736 MHz dual-channel
* **Boot chain**: BootROM → U-Boot SPL → FIT → TF-A BL31 (EL3) → EDK2 (EL2, BL33)
* **EDK2 services**: GIC, generic timer, runtime services, SMBIOS,
  variable services (SPI NOR-backed)
* **Storage**: eMMC, SPI NOR, SD card
* **USB 2.0 host (EHCI + OHCI)** — HID, mass-storage
* **USB 3.0 host (xHCI / DWC3 SuperSpeed)**
  * USB-A (DRD1, combphy1): SS+HS in UEFI and Linux — verified at 5 Gbps
  * USB-C (DRD0, Samsung USBDP combo PHY): HS in UEFI; SS+HS in Linux
    kernel via mainline `phy-rockchip-usbdp.c` (`rockchip,rk3576-usbdp-phy`)
* **Network**: 1 GbE (in Linux; UEFI driver pending)
* **HDMI display**: VOP2 + DW HDMI QP TX PHY fully initialised in EDK2.
  EDID read via DDC. GOP installed at the monitor's native resolution
  (2560×1440@60 QHD on the test monitor). UEFI menus and GRUB render
  directly on the monitor without requiring Linux to bring up the display.
* **Boot path**: GRUB on USB stick → Fedora 44 aarch64 → GNOME 50

## What doesn't work yet

* **PCIe** — RC enumerated and DBI is reachable
  (`VID:DID = 0x1D87:0x3576`), but LTSSM stays in `Polling.*` and never
  reaches L0 (`Link up timeout!`); endpoints work fine under Linux on the
  same physical slot
* **ACPI** — only stub tables, FDT mode is the supported configuration

Details and workarounds in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

---

## What's in the box

* `binaries/` — pre-built, hardware-verified BL31 / U-Boot / DDR-init blobs
* `rock4d-spi-edk2.img` — ready-to-flash 16 MB SPI NOR UEFI image
* `edk2_port/` — the EDK2 source overlay:
  * `Platform/Radxa/ROCK4D/` — board package
  * `Silicon/Rockchip/RK3576/` — SoC silicon package, including
    `RK3576Dxe`, `Vop2Dxe`, `DwHdmiQpLib`, `PhyRockchipSamsungHdptxHdmi`,
    `FdtPlatformDxe`, `Rk3576PciHostBridgeLib`, `Rk3576PciSegmentLib`
  * `build_rock4d_uefi.sh` — one-shot build script with all the GCC 10–13
    workarounds baked in

The upstream third-party trees (`edk2`, `edk2-non-osi`, `edk2-platforms`,
`arm-trusted-firmware`, `rkbin`) are **not** vendored — they are cloned at
build time. See [docs/BUILDING.md](docs/BUILDING.md).

---

## Board Support

### Radxa ROCK 4D — Hardware Verified

| Feature | Status |
|---|---|
| CPU / RAM | Working — 8-core A72+A53, 12 GB LPDDR5 |
| eMMC / SD / SPI NOR | Working — all three storage paths functional |
| USB 2.0 (EHCI/OHCI) | Working — HID and mass-storage |
| USB 3.0 xHCI @ 5 Gbps (USB-A) | Working — verified |
| USB-C (USB3 SS+HS in kernel) | Working — kernel via `phy-rockchip-usbdp.c` |
| 1 GbE (GMAC0) | Working under Linux; UEFI SNP driver pending |
| HDMI | **Working** — VOP2 + HDPTX PHY init, EDID read, GOP at native res |
| PCIe | Partial — DBI reachable, LTSSM never reaches L0 |
| SMBIOS | Working — populated by `PlatformSmbiosDxe` |

See full details above.

### FriendlyElec NanoPi M5 — Initial Support

> **Mainline DTS status:** `rk3576-nanopi-m5.dts` is **not yet in mainline
> Linux** (as of May 2026). The vendor DTS lives in FriendlyElec's
> `nanopi6-v6.1.y` kernel branch. A placeholder DTB is used in CI;
> replace it with a real DTB compiled from the vendor tree before flashing.

**Same RK3576 SoC as ROCK 4D.** Key board-level differences:

| Feature | NanoPi M5 | ROCK 4D |
|---|---|---|
| Ethernet | **2× 1 GbE** (GMAC0 + GMAC1, both RTL8211F RGMII-ID) | 1× 1 GbE (GMAC0) |
| PMIC | RK806 @ I2C1 0x23 | RK806 @ I2C0 0x23 |
| Storage | 16 MB SPI NOR + optional UFS 2.0 | 16 MB SPI NOR + eMMC |
| PCIe slot | M.2 M-Key (PCIe 2.1 ×1) | M.2 M-Key (PCIe 2.1 ×1) |
| LEDs | 3× (SYS / LED1 / LED2) | 1× power LED |

**What is implemented (`Platform/FriendlyElec/NanoPi-M5/`):**

- Full `NanoPi-M5.dsc` platform descriptor, `NanoPi-M5.Modules.fdf.inc`
- `RockchipPlatformLib.c`: IOMUX for GMAC0 (GPIO bank 3) **and GMAC1**
  (GPIO bank 4, eth1m0 pins, function 3), 3-LED init, UFS (no eMMC mux)
- `AcpiTables/Dsdt.asl` includes both `Gmac0.asl` and `Gmac1.asl`
- `DeviceTree/Vendor.inf` packs the vendor DTB
- `PcdGmac1Supported|TRUE`, `PcdGmac0Supported|TRUE` (dual Ethernet)
- PCDs: `PcdComboPhy0ModeDefault|PCIe`, `PcdComboPhy1ModeDefault|USB3`
- SMBIOS strings: `NanoPi M5` / `FriendlyElec` / family `NanoPi`
- Build verified: `BL33_AP_UEFI.Fv` produced cleanly in ~65 s

**Known gaps / TODOs:**

- GMAC1 PHY reset GPIO (GPIO4 PB0 assumed — verify from board schematic)
- No hardware sample available for boot testing
- Vendor DTB must be built manually from FriendlyElec kernel

**Building for NanoPi M5:**

```bash
cd edk2_port
bash build_rock4d_uefi.sh --config configs/nanopi-m5.conf
# Output: Build/NanoPi-M5/RELEASE_GCC/FV/BL33_AP_UEFI.Fv
```

Or using the dedicated GitHub Actions workflow (`.github/workflows/build-nanopi-m5.yml`).

### ArmSoM CM5-IO — Initial Support

> **Mainline DTS status:** `rk3576-armsom-cm5-io.dts` is **not yet in mainline
> Linux** (as of May 2026). This port ships a hand-written mainline-style DTS
> based on GPIO data from the ArmSoM BSP kernel (`linux-6.1-stan-rkr6.1`).
> No vendor DTB is bundled — the UEFI image defaults to the compiled mainline DTS.

**Same RK3576 SoC as ROCK 4D.** The CM5 is an RPi CM4-form-factor compute
module; the CM5-IO is the official carrier board.

Key hardware differences vs ROCK 4D:

| Feature | CM5-IO | ROCK 4D |
|---|---|---|
| eMMC | Onboard on CM5 module (`RK_EMMC_ENABLE=TRUE`) | None |
| USB HOST 5V | GPIO4 PB0 | GPIO0 PD3 |
| USB OTG 5V | GPIO2 PB6 | GPIO2 PD2 |
| PCIe reset | GPIO2 PB1 | GPIO2 PB4 |
| PCIe power | GPIO0 PC3 | GPIO2 PD3 |
| GMAC0 PHY reset | GPIO2 PB3 (active-low) | GPIO2 PB5 (active-low) |
| GMAC0 mode | `rgmii-rxid`, `tx_delay=0x21` | `rgmii-id` (PHY handles both delays) |
| HDMI 5V | Always-on from carrier board | GPIO2 PB0 (MOSFET switch) |
| WiFi reset | GPIO1 PC6 active-low | GPIO2 PD1 active-high |
| BT enable | GPIO1 PC7 active-high | Shared WiFi supply |
| LEDs | GPIO0_PB4 (module work LED) + GPIO2_PD0 (carrier green) + GPIO2_PD1 (carrier red) | GPIO0_PB4 (power) + GPIO0_PC4 (user) |

**What is implemented (`Platform/ArmSoM/CM5IO/`):**

- Full `CM5IO.dsc` with `RK_EMMC_ENABLE=TRUE`, PCIe, HDMI, GbE, USB
- `RockchipPlatformLib.c`: all GPIO differences vs ROCK 4D reflected
- `DeviceTree/Mainline.inf`: builds `rk3576-armsom-cm5-io-uefi.dts` at compile time
- `DeviceTree/Vendor.inf`: present but commented out (supply your own DTB)
- `rk3576-armsom-cm5-io.dts` + `rk3576-armsom-cm5-io-uefi.dts`: mainline-style board DTS

**Known gaps / TODOs:**

- No hardware sample available for boot testing
- Vendor DTB must be compiled from ArmSoM BSP kernel (`linux-6.1-stan-rkr6.1`)
- FUSB302 USB-C PD controller and ES8388 audio codec not yet in DTS

**Building for CM5-IO:**

```bash
cd edk2_port
bash build_rock4d_uefi.sh --config configs/armsom-cm5-io.conf
# Output: Build/CM5IO/RELEASE_GCC/FV/BL33_AP_UEFI.Fv
```

### ArmSoM CM5 RPI-CM4-IO — Initial Support

The **CM5 RPI-CM4-IO** is the official RPi CM4-compatible carrier board for the
ArmSoM CM5 module, featuring a full-size HDMI, M.2 M-Key PCIe slot, 4× USB
2.0/3.0, Gigabit Ethernet, 40-pin GPIO header, and RPi-compatible connectors.

GPIO wiring is **identical to CM5-IO** for all UEFI-relevant peripherals
(PCIe reset, GMAC reset, USB power, LEDs, WiFi/BT). Key carrier-board
differences from CM5-IO:

| Feature | CM5 RPI-CM4-IO | CM5-IO |
|---|---|---|
| USB HOST 5V regulator | Always-on (no GPIO gate) | GPIO4 PB0 |
| Carrier RTC | PCF85063a on I2C5 (addr 0x51) | — |
| Fan controller | EMC2301 on I2C5 (addr 0x2f) | — |
| USB-C PD controller | None (power-in only) | — |

**What is implemented (`Platform/ArmSoM/CM5RpiCM4IO/`):**

- `CM5RpiCM4IO.dsc` — same PCDs as CM5-IO; `PcdDeviceTreeName="rk3576-armsom-cm5-rpi-cm4-io"`
- `RockchipPlatformLib.c` — identical GPIO callbacks to CM5-IO
- `rk3576-armsom-cm5-rpi-cm4-io.dts` + UEFI overlay DTS
- I2C5 node: EMC2301 fan controller + PCF85063a RTC

**Building:**

```bash
cd edk2_port
bash build_rock4d_uefi.sh --config configs/armsom-cm5-rpi-cm4-io.conf
```

### Waveshare CM4-IO-BASE-B — Initial Support

The **Waveshare CM4-IO-BASE-B** is a CM4-form-factor carrier board compatible
with the ArmSoM CM5 module. Hardware is effectively identical to the
CM5 RPI-CM4-IO from a UEFI perspective (same GPIO routing, same I2C5
peripherals: PCF85063a RTC + EMC2301 fan).

Only the `compatible` string and `model` differ:

```
compatible = "waveshare,cm4-io-base-b", "armsom,cm5", "rockchip,rk3576";
model = "Waveshare CM4-IO-BASE-B";
```

**Building:**

```bash
cd edk2_port
bash build_rock4d_uefi.sh --config configs/armsom-cm5-waveshare-cm4b.conf
```

---

## Credits

* [TianoCore EDK2](https://github.com/tianocore/edk2) — UEFI reference implementation
* [edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588) — structural template for the RK3576 silicon package
* [Trusted Firmware-A](https://www.trustedfirmware.org/projects/tf-a/) — BL31
* [Radxa](https://radxa.com/products/rock4/4d/) — hardware
* [Rockchip](https://www.rock-chips.com/) — SoC and DDR init blobs

---

## License

* Repository scaffolding, build scripts, README and docs → **MIT**
  (see [LICENSE](LICENSE))
* EDK2 platform / silicon overlay code → **BSD-2-Clause-Patent** (TianoCore)
* TF-A binaries (`bl31.elf`) → **BSD-3-Clause**
* `rk3576_ddr.bin` → **Rockchip proprietary**, redistributable

See individual file headers.
