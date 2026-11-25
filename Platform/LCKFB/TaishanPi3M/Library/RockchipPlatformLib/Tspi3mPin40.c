/** @file
 *
 *  Tspi3mPin40.c -- the TaishanPi 3M's 40-pin header, for the "40-pin UART"
 *  and "40-pin GPIO/I2C/SPI for apps" setup options (RK3576Dxe Pin40.c does
 *  the work from this description).  The ACPI side is Pin40Uart.asl and
 *  Pin40RhProxy.asl in this board's AcpiTables; the three must agree.
 *
 *  Source: LCKFB's pin table for the board, "泰山派3M-RK3576开发板40Pin_v1.3"
 *  (40Pin排针复用功能表, shipped with the board's documentation), checked
 *  against mainline rk3576-pinctrl.dtsi (Linux 7.3) for every function
 *  number and against the board's mainline device tree, which uses none of
 *  these pins.  The header pins and their SoC pins:
 *
 *     1 3V3                         2 5V
 *     3 GPIO3_A1 I2C7_SDA_M1        4 5V
 *     5 GPIO3_A0 I2C7_SCL_M1        6 GND
 *     7 GPIO4_A6                    8 GPIO2_D0 UART4_TX_M0
 *     9 GND                        10 GPIO2_D1 UART4_RX_M0
 *    11 GPIO4_A4                   12 GPIO0_B5
 *    13 GPIO2_C6                   14 GND
 *    15 GPIO0_C7                   16 GPIO0_C6
 *    17 3V3                        18 GPIO3_A3
 *    19 GPIO2_C2 SPI1_MOSI_M1      20 GND
 *    21 GPIO2_C3 SPI1_MISO_M1      22 GPIO2_D6
 *    23 GPIO2_C5 SPI1_CLK_M1       24 GPIO2_C4 SPI1_CSN0_M1
 *    25 GND                        26 GPIO2_C1 SPI1_CSN1_M1
 *    27 GPIO2_B7 I2C8_SDA_M2       28 GPIO2_B6 I2C8_SCL_M2
 *    29 GPIO2_C0                   30 GND
 *    31 GPIO2_B0                   32 GPIO2_C7
 *    33 GPIO4_A7                   34 GND
 *    35 GPIO0_D2                   36 GPIO2_D7
 *    37 GPIO2_A6 UART8_TX_M1       38 GPIO3_A2
 *    39 GND                        40 GPIO2_A7 UART8_RX_M1
 *
 *  Only the functions this firmware turns on are named.  The choice puts
 *  I2C and SPI where a Raspberry Pi has them (I2C on 3/5 and 27/28, SPI on
 *  19/21/23/24/26) and the UARTs on pins no bus uses: UART4 on 8/10 (where
 *  a Raspberry Pi has its UART) and UART8 on 37/40.  UART0, the console,
 *  is on its own debug connector, not on the header.
 *
 *  Function numbers (rk3576-pinctrl.dtsi): uart4m0_xfer and uart8m1_xfer 9,
 *  pull-up; i2c7m1_xfer and i2c8m2_xfer 11, no pull; spi1m1_pins and
 *  spi1m1_csn0/csn1 10, no pull.
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/GpioLib.h>
#include <Library/RockchipPlatformLib.h>

STATIC CONST GPIO_IOMUX_CONFIG  mTspiPin40UartPins[] = {
  { "uart4_tx_m0 (pin 8)",  2, GPIO_PIN_PD0, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart4_rx_m0 (pin 10)", 2, GPIO_PIN_PD1, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart8_tx_m1 (pin 37)", 2, GPIO_PIN_PA6, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart8_rx_m1 (pin 40)", 2, GPIO_PIN_PA7, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST GPIO_IOMUX_CONFIG  mTspiPin40BusPins[] = {
  { "i2c7_sda_m1 (pin 3)",   3, GPIO_PIN_PA1, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c7_scl_m1 (pin 5)",   3, GPIO_PIN_PA0, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c8_sda_m2 (pin 27)",  2, GPIO_PIN_PB7, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c8_scl_m2 (pin 28)",  2, GPIO_PIN_PB6, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_mosi_m1 (pin 19)", 2, GPIO_PIN_PC2, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_miso_m1 (pin 21)", 2, GPIO_PIN_PC3, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_clk_m1 (pin 23)",  2, GPIO_PIN_PC5, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_csn0_m1 (pin 24)", 2, GPIO_PIN_PC4, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_csn1_m1 (pin 26)", 2, GPIO_PIN_PC1, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

/*
 * In header-pin order, which is also the order (and so the Windows pin
 * number, 0 upwards) of Pin40RhProxy.asl's GpioIo list, except that the
 * four UART pins come last there.  Pull is not applied (Pin40.c).
 */
STATIC CONST GPIO_IOMUX_CONFIG  mTspiPin40GpioPins[] = {
  { "pin 7",  4, GPIO_PIN_PA6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 11", 4, GPIO_PIN_PA4, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 12", 0, GPIO_PIN_PB5, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 13", 2, GPIO_PIN_PC6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 15", 0, GPIO_PIN_PC7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 16", 0, GPIO_PIN_PC6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 18", 3, GPIO_PIN_PA3, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 22", 2, GPIO_PIN_PD6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 29", 2, GPIO_PIN_PC0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 31", 2, GPIO_PIN_PB0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 32", 2, GPIO_PIN_PC7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 33", 4, GPIO_PIN_PA7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 35", 0, GPIO_PIN_PD2, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 36", 2, GPIO_PIN_PD7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 38", 3, GPIO_PIN_PA2, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 8",  2, GPIO_PIN_PD0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 10", 2, GPIO_PIN_PD1, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 37", 2, GPIO_PIN_PA6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 40", 2, GPIO_PIN_PA7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST PIN40_CONFIG  mTspiPin40 = {
  2, { 4, 8 },
  2, { 7, 8 },
  1, { 1 },
  mTspiPin40UartPins, ARRAY_SIZE (mTspiPin40UartPins),
  mTspiPin40BusPins,  ARRAY_SIZE (mTspiPin40BusPins),
  mTspiPin40GpioPins, ARRAY_SIZE (mTspiPin40GpioPins),
};

CONST PIN40_CONFIG *
EFIAPI
PlatformGetPin40Config (
  VOID
  )
{
  return &mTspiPin40;
}
