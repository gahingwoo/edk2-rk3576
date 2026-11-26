/** @file
 *
 *  Tspi3mBluetooth.c: pins and clocks of the TaishanPi 3M's Bluetooth UART,
 *  set up for an operating system that has no pin or clock driver of its own
 *  (Windows with the woa-rk3576 bcm4345bt driver, which binds \_SB.BTH0 in
 *  Bluetooth.asl).  Linux reprograms all of it from its device tree.
 *
 *  The module is an Ampak AP6256 (U59, schematic LCEDA project v1.4): WiFi
 *  BCM43456 on SDIO and Bluetooth BCM4345C5 on UART.  From the board's
 *  mainline device tree (rk3576-lckfb-tspi-3m.dts, &uart6 and
 *  wireless-bluetooth, Linux 7.3) and the schematic:
 *
 *    UART6 M1  TX GPIO2 PD2, RX GPIO2 PD3, RTS GPIO2 PD4, CTS GPIO2 PD5,
 *              function 9, pull-up on TX and RX (rk3576-pinctrl.dtsi
 *              uart6m1_xfer, uart6m1_rtsn, uart6m1_ctsn).  Through a TXB0104
 *              level shifter (U115) to AP6256 pins 41-44.
 *    BT_REG_ON GPIO1 PC7, active high, pull none (bt_reg_on_h).  AP6256
 *              pin 34 through R194; R195, the option that would tie it to
 *              WIFI_REG_ON instead, is not fitted ("Add into BOM" = no).
 *    BT_WAKE   GPIO1 PD4, host to module, active high (bt_wake_h), pin 6.
 *    HOST_WAKE GPIO0 PB1, module to host, active high, pull-down
 *              (bt_host_wake_h), pin 7.
 *    32 kHz    the HYM8563's CLKOUT (U71 pin 7) through R454 and R198 to
 *              pin 24; R473 to ground is not fitted.  Not touched here.
 *    VBAT      VCC_3V3_S3 through R320 and R201.  Q9, the switch that
 *              WIFI_PWREN_L (GPIO0 PD1) would drive, is not fitted, so the
 *              module's supply is always on and that pin does nothing.
 *
 *  Only the iomux and the UART clock are set.  BT_REG_ON is not driven: the
 *  module stays off until the OS driver raises it (it has its own pull-down
 *  on REG_ON), which is also what Linux expects to find.
 *
 *  CRU (0x27200000), from mainline clk-rk3576.c and rst-rk3576.c:
 *    CLKSEL_CON65  0x404  [10:8] sclk_uart6 source = 3 (xin24m), [7:0] div 0
 *    GATE_CON13    0x834  bit 15  pclk_uart6
 *    GATE_CON15    0x83C  bit 2   sclk_uart6
 *    SOFTRST_CON13 0xA34  bit 15  SRST_P_UART6
 *    SOFTRST_CON15 0xA3C  bit 2   SRST_S_UART6
 *
 *  24 MHz gives exactly 1500000 baud with divisor 1, the module's max-speed
 *  in the device tree, and 115200 with divisor 13 (0.16 % off), the speed
 *  the module starts at.  Bluetooth.asl tells the driver 24000000.
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>
#include <Soc.h>

#define BT_UART6_SCLK_SEL       65
#define BT_UART6_SCLK_XIN24M    3

VOID
Tspi3mBluetoothIoInit (
  VOID
  )
{
  /* UART6 clocks: 24 MHz from xin24m, both gates open, both resets released. */
  MmioWrite32 (
    CRU_CLKSEL_CON (BT_UART6_SCLK_SEL),
    (0x7FFU << 16) | (BT_UART6_SCLK_XIN24M << 8) | 0
    );
  MmioWrite32 (CRU_CLKGATE_CON (13), (BIT15 << 16) | 0);
  MmioWrite32 (CRU_CLKGATE_CON (15), (BIT2 << 16) | 0);
  MmioWrite32 (CRU_SOFTRST_CON (13), (BIT15 << 16) | 0);
  MmioWrite32 (CRU_SOFTRST_CON (15), (BIT2 << 16) | 0);

  /* uart6m1: TX PD2, RX PD3, RTS PD4, CTS PD5, function 9. */
  GpioPinSetFunction (2, GPIO_PIN_PD2, 9);
  GpioPinSetFunction (2, GPIO_PIN_PD3, 9);
  GpioPinSetFunction (2, GPIO_PIN_PD4, 9);
  GpioPinSetFunction (2, GPIO_PIN_PD5, 9);
  GpioPinSetPull (2, GPIO_PIN_PD2, GPIO_PIN_PULL_UP);
  GpioPinSetPull (2, GPIO_PIN_PD3, GPIO_PIN_PULL_UP);
  GpioPinSetPull (2, GPIO_PIN_PD4, GPIO_PIN_PULL_NONE);
  GpioPinSetPull (2, GPIO_PIN_PD5, GPIO_PIN_PULL_NONE);

  /* BT_REG_ON and BT_WAKE as GPIOs; direction and level are the driver's. */
  GpioPinSetFunction (1, GPIO_PIN_PC7, 0);
  GpioPinSetPull (1, GPIO_PIN_PC7, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (1, GPIO_PIN_PD4, 0);
  GpioPinSetPull (1, GPIO_PIN_PD4, GPIO_PIN_PULL_NONE);

  /* HOST_WAKE: GPIO input with the pull-down the device tree asks for. */
  GpioPinSetFunction (0, GPIO_PIN_PB1, 0);
  GpioPinSetPull (0, GPIO_PIN_PB1, GPIO_PIN_PULL_DOWN);

  DEBUG ((
    DEBUG_INFO,
    "TSPI-3M Bluetooth: UART6 M1 GPIO2 PD2-PD5 fn9, sclk_uart6 24 MHz; BT_REG_ON GPIO1 PC7 left off\n"
    ));
}
