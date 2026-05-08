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

    // PCIe root complexes (RK3576: 1x PCIe3.0 x2 + 1x PCIe2.0 x1)
    include ("Pcie.asl")

    // Storage
    include ("Emmc.asl")
    include ("Sdhc.asl")

    // GPIO pin controller and banks (used by SDHC card detect and other GPIO consumers)
    include ("Gpio.asl")

    // Serial (UART2)
    include ("Uart.asl")

    // USB (2x DWC3 OTG — DRD0 and DRD1)
    include ("Usb3Host0.asl")
    include ("Usb3Host1.asl")

    // Network (GMAC0 + GMAC1, DWC EQoS)
    include ("Gmac0.asl")
    include ("Gmac1.asl")
  }
}
