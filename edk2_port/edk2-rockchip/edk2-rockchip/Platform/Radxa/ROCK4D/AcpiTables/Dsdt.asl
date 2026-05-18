/** @file
 *
 *  Differentiated System Description Table (DSDT)
 *  Radxa ROCK 4D (RK3576)
 *
 *  Copyright (c) 2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2018-2020, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
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

    // PCIe root complexes (RK3576: PCIe3.0 x2 via combphy0 + PCIe2.0 x1 via combphy1)
    include ("Pcie.asl")

    // Storage
    include ("Emmc.asl")
    include ("Sdhc.asl")

    // GPIO pin controller and banks (GPIO0..GPIO4)
    include ("Gpio.asl")

    // DMA controllers (DMA0 @ 0xFEA10000, DMA1 @ 0xFEA30000)
    include ("Dma.asl")

    // Serial (UART2)
    include ("Uart.asl")

    // I2C buses (I2C1..I2C8 on RK3576)
    // I2C1: PMIC RK806 @ 0x23
    // I2C2: RTC HYM8563 @ 0x51
    // I2C6: EEPROM @ 0x50 (pinctrl i2c6m3)
    include ("I2c.asl")

    // SPI buses (SPI0..SPI2 on RK3576; SFC0 is a separate FSPI for SPI-NOR boot)
    include ("Spi.asl")

    // USB (2x DWC3 OTG used as host — DRD0 @ 0x23000000, DRD1 @ 0x23400000)
    include ("Usb3Host0.asl")
    include ("Usb3Host1.asl")
    // XHC2 stub — disabled, RK3576 has only 2 USB DRD controllers
    include ("Usb3Host2.asl")

    // SATA — disabled stub, RK3576 has no SATA controller
    include ("Sata.asl")

    // Network (GMAC0 only — ROCK 4D has a single 1GbE port via GMAC0/RTL8211F)
    include ("Gmac0.asl")
  }
}
