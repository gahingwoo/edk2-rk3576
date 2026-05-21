/** @file
 *
 *  Differentiated System Description Table (DSDT)
 *  FriendlyElec NanoPi M5 (RK3576)
 *
 *  Key difference from ROCK 4D DSDT: Gmac1.asl is included because
 *  NanoPi M5 has two 1Gbps Ethernet ports (GMAC0 + GMAC1).
 *
 *  Copyright (c) 2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2018-2020, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2025, FriendlyElec NanoPi M5 EDK2 Port
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

    // PCIe root complexes (RK3576: 1x PCIe3.0 x2 + 1x PCIe2.0 x1)
    include ("Pcie.asl")

    // Storage
    include ("Emmc.asl")
    include ("Sdhc.asl")

    // GPIO pin controller and banks
    include ("Gpio.asl")

    // DMA controllers (DMA0 @ 0x2ab90000, DMA1 @ 0x2abb0000, DMA2 @ 0x2abd0000)
    include ("Dma.asl")

    // Serial (UART2 @ 0x2AD50000)
    include ("Uart.asl")

    // I2C buses (SoC-level)
    // I2C1: RK806 PMIC @ 0x23 (on CM5/NanoPi M5 module)
    // I2C2: HYM8563 RTC @ 0x51
    include ("I2c.asl")

    // SPI buses (SPI0..SPI4 on RK3576; SFC0 disabled — UEFI owns SPI-NOR flash)
    include ("Spi.asl")

    // USB (2x DWC3 OTG — DRD0 and DRD1)
    include ("Usb3Host0.asl")
    include ("Usb3Host1.asl")
    // XHC2 stub — disabled; RK3576 has only 2 USB DRD controllers
    include ("Usb3Host2.asl")

    // SATA — disabled stub; RK3576 has no SATA controller
    include ("Sata.asl")

    // Network (GMAC0 + GMAC1, DWC EQoS)
    // NanoPi M5 has BOTH ports connected (unlike ROCK 4D which only uses GMAC0)
    include ("Gmac0.asl")
    include ("Gmac1.asl")
  }
}
