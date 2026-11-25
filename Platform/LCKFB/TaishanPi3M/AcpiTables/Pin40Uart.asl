/** @file
 *
 *  [SSDT] The TaishanPi 3M's 40-pin header UARTs, for Windows applications
 *  (woa-rk3576 rk3xuart).  Installed only when "40-pin UART" is on in setup
 *  (PcdPin40Uart, off by default); RK3576Dxe sets the pins and clocks under
 *  the same option (Tspi3mPin40.c, Pin40.c).  Off, the pins are GPIOs and
 *  the RhProxy SSDT lists them as such.
 *
 *    UART4 M0  TX header pin 8 (GPIO2_D0), RX pin 10 (GPIO2_D1)
 *              0x2ad70000, GIC SPI 80 (GSIV 112), port name "UART4"
 *    UART8 M1  TX header pin 37 (GPIO2_A6), RX pin 40 (GPIO2_A7)
 *              0x2adb0000, GIC SPI 84 (GSIV 116), port name "UART8"
 *
 *  Addresses and interrupts from rk3576.dtsi, pins from LCKFB's 40-pin table
 *  (Tspi3mPin40.c lists the whole header).  UART0, the console, is not on
 *  the header.  UART6 is the Bluetooth module's (Bluetooth.asl).
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

#define RK_UART40_NAME      URT8
#define RK_UART40_UID       8
#define RK_UART40_BASE      0x2adb0000
#define RK_UART40_GSIV      116
#define RK_UART40_PORTNAME  "UART8"
    include ("Uart40.asl")
  }
}
