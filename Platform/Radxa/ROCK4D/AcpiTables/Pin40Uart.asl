/** @file
 *
 *  [SSDT] The ROCK 4D's 40-pin header UARTs, for Windows applications
 *  (woa-rk3576 rk3xuart).  Installed only when "40-pin UART" is on in setup
 *  (PcdPin40Uart, off by default); RK3576Dxe sets the pins and clocks under
 *  the same option (Rock4dPin40.c, Pin40.c).  Off, the pins are GPIOs and
 *  the RhProxy SSDT lists them as such.
 *
 *    UART4 M1  TX header pin 11 (GPIO1_C4), RX pin 15 (GPIO1_C5)
 *              0x2ad70000, GIC SPI 80 (GSIV 112), port name "UART4"
 *    UART7 M0  TX header pin 16 (GPIO2_B6), RX pin 18 (GPIO2_B7)
 *              0x2ada0000, GIC SPI 83 (GSIV 115), port name "UART7"
 *
 *  UART0 on header pins 8/10 is the console and is not touched.  The DSDT's
 *  UART2 device (Uart.asl) is a different controller and stays.
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
#define RK_UART40_NAME      URT4
#define RK_UART40_UID       4
#define RK_UART40_BASE      0x2ad70000
#define RK_UART40_GSIV      112
#define RK_UART40_PORTNAME  "UART4"
    include ("Uart40.asl")
#undef RK_UART40_NAME
#undef RK_UART40_UID
#undef RK_UART40_BASE
#undef RK_UART40_GSIV
#undef RK_UART40_PORTNAME

#define RK_UART40_NAME      URT7
#define RK_UART40_UID       7
#define RK_UART40_BASE      0x2ada0000
#define RK_UART40_GSIV      115
#define RK_UART40_PORTNAME  "UART7"
    include ("Uart40.asl")
  }
}
