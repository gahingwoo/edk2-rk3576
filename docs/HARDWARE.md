# Hardware Verification

Tested on **Radxa ROCK 4D, 12 GB LPDDR5 SKU**.

## Boot stack

| Component   | Version / Address                              |
|-------------|------------------------------------------------|
| TF-A BL31   | v2.14.0, loaded at `0x00040000` (EL3)          |
| U-Boot SPL  | 2026.04, FIT loader → BL31 + EDK2 + DTB        |
| EDK2        | edk2 commit `46548b1`, loaded at `0x00200000` (EL2, BL33) |
| Detected RAM| 12544 MB total (2× 6 GB main + 256 MB extra)   |

## Peripherals

| Item                           | UEFI                       | Linux (Fedora 44 aarch64) |
|--------------------------------|----------------------------|---------------------------|
| LPDDR5 (2736 MHz, dual ch)     | OK                         | OK                        |
| RK806 PMIC (I²C0)              | Enumerated                 | OK                        |
| eMMC                           | Boot + R/W OK              | OK                        |
| SD card (SDMMC0)               | Detected, init issues *    | OK                        |
| SPI NOR (16 MB)                | Boot + R/W OK              | OK                        |
| USB 2.0 host (DWC2 / EHCI/OHCI)| Enumerated                 | OK                        |
| USB 3.0 host (DWC3 / xHCI SS)  | **OK** — devices @ 5 Gbps  | **OK** — 5000M root hub   |
| Ethernet (1 GbE)               | OK                         | OK                        |
| GIC v3                         | OK                         | OK                        |
| Generic Timer                  | OK                         | OK                        |
| HDMI / display                 | **Working** — GOP at native res (2560×1440@60) † | OK           |
| PCIe 2.1 x1 (Combo PHY0)       | RC enumerated, link fails ‡| OK                        |
| EDK2 UEFI Shell over UART      | OK                         | —                         |
| GRUB on USB → Linux            | OK                         | OK                        |

\* SDMMC0 reports `DwSdExecTrb: Command error … IntStatus=104` for CMD1/CMD8/CMD55/CMD7/CMD3
  during identification when no card is present; harmless.

† VOP2 + DW HDMI QP TX PHY are fully initialised by EDK2. EDID is read
  via DDC; GOP is installed at the monitor's native resolution. UEFI menus
  and GRUB render on the monitor without requiring a Linux display driver.
  Minor visual artifacts (horizontal stripes, slight horizontal shift) are
  under investigation — see [KNOWN_ISSUES.md](KNOWN_ISSUES.md#display-hdmi--visual-artifacts).

‡ PCIe DBI is reachable (`VID:DID = 0x1D87:0x3576`), but LTSSM stays in
  `0x0003000D → 0x00000003` and never reaches L0 — `PciHostBridgeDxe` aborts
  with *Link up timeout*. Same physical slot works under Linux. See
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md#pcie-link-training-fails-in-uefi).

## Serial console

* **1,500,000 baud, 8N1**, no flow control
* 3-pin debug header on the board

## Boot screenshots

| Stage                    | Screenshot                                |
|--------------------------|-------------------------------------------|
| **UEFI front page (HDMI)**| ![monitor](imgs/monitor-4d.png)          |
| GRUB on Fedora 44 USB    | ![grub](imgs/grub.png)                    |
| Fedora live console      | ![fedora](imgs/fedora.png)                |
| Fedora 44 GNOME desktop  | ![desktop](imgs/desktop.png)              |

The Fedora *About → System Details* panel correctly reports the SMBIOS data
written by `PlatformSmbiosDxe`:

* **Model:** Radxa ROCK 4D
* **Firmware Version:** rk3576-rock4d-v0.1
* **Memory:** 11.5 GiB
