/** @file
 *
 *  Cm5ioBluetooth.c -- pins and clocks of the CM5 module's Bluetooth UART,
 *  set up for an operating system that has no pin or clock driver of its own
 *  (Windows with the woa-rk3576 bcm4345bt driver, which binds \_SB.BTH0 in
 *  Bluetooth.asl).  Linux reprograms all of it from its device tree.
 *
 *  The module is U8, a BW3752-50B1 (BCM43752: Wi-Fi on SDIO, Bluetooth on
 *  UART).  From the ArmSoM-CM5-1V1 schematic (V1.1, pages 11 and 17), with
 *  rk3576-armsom-cm5.dtsi (&uart4) and rk3576-pinctrl.dtsi (uart4m1_xfer,
 *  uart4m1_ctsn, uart4m1_rtsn):
 *
 *    UART4 M1  RTS GPIO1 PC2, CTS GPIO1 PC3, TX GPIO1 PC4, RX GPIO1 PC5,
 *              function 9, pull-up on TX and RX, none on RTS and CTS.
 *              Straight to U8's UART_* pins (both sides 1.8 V: the
 *              module's VDDIO is VCC_1V8_S3).
 *    BT_REG_ON GPIO1 PC7, active high (BT_REG_ON_H).  PlatformEarlyInit
 *              raises it at boot, as this board always has; the driver
 *              cycles it.
 *    BT_WAKE   GPIO1 PD4, host to module, active high (HOST_WAKE_BT_H).
 *    HOST_WAKE GPIO0 PB1, module to host, active high (BT_WAKE_HOST_H).
 *    32 kHz    the LK8563S CLKOUT through R2425 (32KOUT_RTC2WIFI) to U8's
 *              LPO; kept on by Hym8563RealTimeClockLib
 *              (PcdRtc8563ClkoutEnable).  Not touched here.
 *
 *  These are the TaishanPi 3M's Bluetooth GPIOs as well
 *  (Tspi3mBluetooth.c); the UART is UART4 here, UART6 there.
 *
 *  CRU (0x27200000), from mainline clk-rk3576.c and rst-rk3576.c:
 *    CLKSEL_CON63  0x3FC  [10:8] sclk_uart4 source = 3 (xin24m), [7:0] div 0
 *    GATE_CON13    0x834  bit 13  pclk_uart4
 *    GATE_CON14    0x838  bit 12  sclk_uart4
 *    SOFTRST_CON13 0xA34  bit 13  SRST_P_UART4
 *    SOFTRST_CON14 0xA38  bit 12  SRST_S_UART4
 *
 *  24 MHz gives exactly 1500000 baud with divisor 1 and 115200 with divisor
 *  13 (0.16 % off), the speed the module starts at.  Bluetooth.asl tells
 *  the driver 24000000.
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

#define BT_UART4_SCLK_SEL       63
#define BT_UART4_SCLK_XIN24M    3

VOID
Cm5ioBluetoothIoInit (
  VOID
  )
{
  /* UART4 clocks: 24 MHz from xin24m, both gates open, both resets released. */
  MmioWrite32 (
    CRU_CLKSEL_CON (BT_UART4_SCLK_SEL),
    (0x7FFU << 16) | (BT_UART4_SCLK_XIN24M << 8) | 0
    );
  MmioWrite32 (CRU_CLKGATE_CON (13), (BIT13 << 16) | 0);
  MmioWrite32 (CRU_CLKGATE_CON (14), (BIT12 << 16) | 0);
  MmioWrite32 (CRU_SOFTRST_CON (13), (BIT13 << 16) | 0);
  MmioWrite32 (CRU_SOFTRST_CON (14), (BIT12 << 16) | 0);

  /* uart4m1: RTS PC2, CTS PC3, TX PC4, RX PC5, function 9. */
  GpioPinSetFunction (1, GPIO_PIN_PC2, 9);
  GpioPinSetFunction (1, GPIO_PIN_PC3, 9);
  GpioPinSetFunction (1, GPIO_PIN_PC4, 9);
  GpioPinSetFunction (1, GPIO_PIN_PC5, 9);
  GpioPinSetPull (1, GPIO_PIN_PC2, GPIO_PIN_PULL_NONE);
  GpioPinSetPull (1, GPIO_PIN_PC3, GPIO_PIN_PULL_NONE);
  GpioPinSetPull (1, GPIO_PIN_PC4, GPIO_PIN_PULL_UP);
  GpioPinSetPull (1, GPIO_PIN_PC5, GPIO_PIN_PULL_UP);

  /* BT_WAKE as a GPIO; direction and level are the driver's. */
  GpioPinSetFunction (1, GPIO_PIN_PD4, 0);
  GpioPinSetPull (1, GPIO_PIN_PD4, GPIO_PIN_PULL_NONE);

  /* HOST_WAKE: GPIO input, pulled down (active high, idle low). */
  GpioPinSetFunction (0, GPIO_PIN_PB1, 0);
  GpioPinSetPull (0, GPIO_PIN_PB1, GPIO_PIN_PULL_DOWN);

  DEBUG ((
    DEBUG_INFO,
    "CM5-IO Bluetooth: UART4 M1 GPIO1 PC2-PC5 fn9, sclk_uart4 24 MHz\n"
    ));
}
