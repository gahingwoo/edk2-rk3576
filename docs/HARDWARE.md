# Hardware Verification

Tested on **Radxa ROCK 4D, 12 GB LPDDR5 SKU**.

## Boot stack

| Component   | Version / Address                              |
|-------------|------------------------------------------------|
| TF-A BL31   | v2.14.0, loaded at `0x00040000` (EL3)          |
| U-Boot      | 2026.04, EL2 (BL33 fallback path)              |
| EDK2        | edk2 commit `46548b1`, loaded at `0x00200000`  |

## Peripherals

| Item                | Result                                       |
|---------------------|----------------------------------------------|
| LPDDR5              | 2736 MHz, dual-channel, 12 GB OK             |
| RK806 PMIC          | I²C0 enumerated                              |
| eMMC / SD           | Boot + read/write OK                         |
| SPI NOR (16 MB)     | Boot + read/write OK                         |
| USB host            | OK (mass-storage enumerated)                 |
| Ethernet (1 GbE)    | OK                                           |
| EDK2 UEFI Shell     | OK over UART                                 |

## Serial console

* **1,500,000 baud, 8N1**, no flow control
* 3-pin debug header on the board

## Display / HDMI

**Not yet working.** See [Known issues](KNOWN_ISSUES.md).
