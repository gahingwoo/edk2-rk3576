/** @file
 *
 *  [SSDT] The CM5-IO's 40-pin header UART, for Windows applications
 *  (woa-rk3576 rk3xuart).  Installed only when "40-pin UART" is on in setup
 *  (PcdPin40Uart, off by default); RK3576Dxe sets the pins and clock under
 *  the same option (Cm5ioPin40.c, Pin40.c).  Off, the pins are GPIOs and the
 *  RhProxy SSDT lists them as such.
 *
 *    UART2 M1  RX header pin 3 (GPIO4_B4), TX pin 5 (GPIO4_B5)
 *              0x2ad50000, GIC SPI 78 (GSIV 110), port name "UART2"
 *
 *  This replaces the DSDT's old UART2 device (Uart.asl, HISI0031, always
 *  declared, no Windows driver), which named the same registers.  UART0 on
 *  header pins 8/10 is the console and is not touched; UART4 is the CM5
 *  module's Bluetooth (Bluetooth.asl).
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM ON THIS BOARD.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include "AcpiTables.h"

DefinitionBlock ("Pin40Uart.aml", "SSDT", 2, "RKCP  ", "RKUART40", 1)
{
  Scope (\_SB_)
  {
#define RK_UART40_NAME      URT2
#define RK_UART40_UID       2
#define RK_UART40_BASE      0x2ad50000
#define RK_UART40_GSIV      110
#define RK_UART40_PORTNAME  "UART2"
    include ("Uart40.asl")
  }
}
