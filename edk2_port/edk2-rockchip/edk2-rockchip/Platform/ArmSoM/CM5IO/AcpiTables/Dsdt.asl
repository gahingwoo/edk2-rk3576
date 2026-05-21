/** @file
 *
 *  Differentiated System Description Table (DSDT)
 *  ArmSoM CM5 on CM5-IO carrier board (RK3576)
 *
 *  Same RK3576 silicon as Radxa ROCK 4D and FriendlyElec NanoPi M5.
 *  Board-level differences vs ROCK 4D that affect ACPI:
 *    - eMMC: onboard on CM5 module (same silicon path, Emmc.asl unchanged)
 *    - Ethernet: GMAC0 only (GMAC1 not connected on CM5-IO carrier)
 *      Confirmed from rk3576-armsom-cm5-io.dts and RockchipPlatformLib.c:
 *      GmacIomux() returns early for Id != 0.
 *    - No I2C5 peripherals on CM5-IO carrier:
 *      No EMC2301 fan controller, no PCF85063 RTC on I2C5.
 *      The common I2c.asl covers SoC-level I2C buses; carrier-specific
 *      I2C5 devices are absent and do not need ACPI entries.
 *    - USB: DWC3 DRD0 (USBDP PHY, USB-A on carrier) +
 *            DWC3 DRD1 (combphy1, USB3 upper port on carrier)
 *      USB HOST 5V: GPIO4_PB0; USB OTG 5V: GPIO2_PB6
 *    - PCIe: single M.2 M-key slot via combphy0 (same MMIO as ROCK 4D)
 *      PCIe power: GPIO0_PC3; PCIe reset: GPIO2_PB1
 *    - GMAC0 reset: GPIO2_PB3 (same as ROCK 4D)
 *
 *  NOTE: ACPI mode is not the default boot mode for this platform.
 *  The default image uses FDT (Device Tree) for maximum Linux compatibility.
 *  To build with ACPI support, enable the AcpiTables.inf and
 *  RK3576AcpiPlatformDxe.inf entries in CM5IO.dsc and
 *  CM5IO.Modules.fdf.inc, then rebuild.
 *
 *  Copyright (c) 2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2018-2020, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2025, ArmSoM CM5-IO EDK2 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

DefinitionBlock ("Dsdt.aml", "DSDT", 2, "RKCP  ", "RK3576  ", 2)
{
  Scope (\_SB_)
  {
    // SCMI mailbox and PSCI
    include ("DsdtCommon.asl")

    // CPU topology (4x Cortex-A53 + 4x Cortex-A72)
    include ("Cpu.asl")

    // PCIe root complex (M.2 M-key slot via combphy0)
    include ("Pcie.asl")

    // Storage
    // eMMC: onboard on CM5 module (sdhci @ 0x2A330000)
    include ("Emmc.asl")
    // SD card: carrier microSD slot (sdmmc @ 0x2A310000)
    include ("Sdhc.asl")

    // GPIO pin controller and banks (GPIO0..GPIO4)
    include ("Gpio.asl")

    // DMA controllers (DMA0 @ 0xFEA10000, DMA1 @ 0xFEA30000)
    include ("Dma.asl")

    // Serial (UART0 @ 0x2AD40000, 1.5 Mbaud debug console)
    include ("Uart.asl")

    // I2C buses (SoC-level; CM5-IO carrier has no I2C5 devices)
    // I2C1: RK806 PMIC @ 0x23 (on CM5 module)
    // I2C2: HYM8563 RTC @ 0x51 (on CM5 module)
    // Note: CM5-IO carrier has no I2C5 EMC2301 fan or PCF85063 RTC
    include ("I2c.asl")

    // SPI buses (SPI0..SPI2 on RK3576; SFC0 is the separate SPI-NOR boot flash)
    include ("Spi.asl")

    // USB — 2x DWC3 OTG cores, both operated as host
    // DRD0 @ 0x23000000: USB-A port via USBDP PHY (HS+SS)
    include ("Usb3Host0.asl")
    // DRD1 @ 0x23400000: USB3 upper port via combphy1 (HS+SS)
    include ("Usb3Host1.asl")
    // XHC2 stub — disabled; RK3576 only has 2 USB DRD controllers
    include ("Usb3Host2.asl")

    // SATA — disabled stub; RK3576 has no SATA controller
    include ("Sata.asl")

    // Network: GMAC0 only (RTL8211F GbE, rgmii-rxid)
    // CM5-IO has a single Ethernet port; GMAC1 is not connected
    include ("Gmac0.asl")
  }
}
