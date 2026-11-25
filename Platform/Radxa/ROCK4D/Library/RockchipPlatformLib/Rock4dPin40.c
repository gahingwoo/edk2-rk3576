/** @file
 *
 *  Rock4dPin40.c -- the ROCK 4D's 40-pin header, for the "40-pin UART" and
 *  "40-pin GPIO/I2C/SPI for apps" setup options (RK3576Dxe Pin40.c does the
 *  work from this description).  The ACPI side is Pin40Uart.asl and
 *  Pin40RhProxy.asl in this board's AcpiTables; the three must agree.
 *
 *  Source: Radxa's ROCK 4D 40-pin table (radxa-docs, docs/rock4/rock4d/
 *  hardware-use/40-pin-gpio.md), checked against mainline
 *  rk3576-pinctrl.dtsi (Linux 7.3) for every function number and against
 *  the board's mainline device tree, which uses only pins 27/28 (I2C6, the
 *  BL24C16F EEPROM at 0x50) and 8/10 (UART0, the console).  The header:
 *
 *     1 3V3                        2 5V
 *     3 GPIO1_C7 I2C8_SDA_M1       4 5V
 *     5 GPIO1_C6 I2C8_SCL_M1       6 GND
 *     7 GPIO1_C3                   8 GPIO0_D4 UART0_TX (console)
 *     9 GND                       10 GPIO0_D5 UART0_RX (console)
 *    11 GPIO1_C4 UART4_TX_M1      12 GPIO1_D1
 *    13 GPIO2_C0                  14 GND
 *    15 GPIO1_C5 UART4_RX_M1      16 GPIO2_B6 UART7_TX_M0
 *    17 3V3                       18 GPIO2_B7 UART7_RX_M0
 *    19 GPIO1_B5 SPI1_MOSI_M0     20 GND
 *    21 GPIO1_B6 SPI1_MISO_M0     22 GPIO2_D7
 *    23 GPIO1_B4 SPI1_CLK_M0      24 GPIO1_B7 SPI1_CSN0_M0
 *    25 GND                       26 GPIO1_C0 SPI1_CSN1_M0
 *    27 GPIO4_C7 I2C6_SDA_M3      28 GPIO4_C6 I2C6_SCL_M3
 *    29 GPIO3_A2                  30 GND
 *    31 GPIO1_C1                  32 GPIO1_D5
 *    33 GPIO1_C2                  34 GND
 *    35 GPIO1_D2                  36 GPIO1_D4
 *    37 GPIO3_A3                  38 GPIO1_D3
 *    39 GND                       40 GPIO1_D0
 *
 *  Only the functions this firmware turns on are named.  I2C8 and SPI1 sit
 *  where a Raspberry Pi has its I2C and SPI; the UARTs are UART4 (11/15) and
 *  UART7 (16/18), which no chosen bus uses.  UART2 M0 (pins 3/5) is not
 *  offered: those pins are I2C8 here.
 *
 *  Function numbers (rk3576-pinctrl.dtsi): uart4m1_xfer and uart7m0_xfer 9,
 *  pull-up; i2c8m1_xfer 10, i2c6m3_xfer 11, no pull; spi1m0_pins and
 *  spi1m0_csn0/csn1 11, no pull.
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

STATIC CONST GPIO_IOMUX_CONFIG  mRock4dPin40UartPins[] = {
  { "uart4_tx_m1 (pin 11)", 1, GPIO_PIN_PC4, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart4_rx_m1 (pin 15)", 1, GPIO_PIN_PC5, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart7_tx_m0 (pin 16)", 2, GPIO_PIN_PB6, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart7_rx_m0 (pin 18)", 2, GPIO_PIN_PB7, 9, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST GPIO_IOMUX_CONFIG  mRock4dPin40BusPins[] = {
  { "i2c8_sda_m1 (pin 3)",   1, GPIO_PIN_PC7, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c8_scl_m1 (pin 5)",   1, GPIO_PIN_PC6, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c6_sda_m3 (pin 27)",  4, GPIO_PIN_PC7, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c6_scl_m3 (pin 28)",  4, GPIO_PIN_PC6, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_mosi_m0 (pin 19)", 1, GPIO_PIN_PB5, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_miso_m0 (pin 21)", 1, GPIO_PIN_PB6, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_clk_m0 (pin 23)",  1, GPIO_PIN_PB4, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_csn0_m0 (pin 24)", 1, GPIO_PIN_PB7, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi1_csn1_m0 (pin 26)", 1, GPIO_PIN_PC0, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

/* In Pin40RhProxy.asl's GpioIo order; the four UART pins come last there. */
STATIC CONST GPIO_IOMUX_CONFIG  mRock4dPin40GpioPins[] = {
  { "pin 7",  1, GPIO_PIN_PC3, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 12", 1, GPIO_PIN_PD1, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 13", 2, GPIO_PIN_PC0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 22", 2, GPIO_PIN_PD7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 29", 3, GPIO_PIN_PA2, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 31", 1, GPIO_PIN_PC1, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 32", 1, GPIO_PIN_PD5, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 33", 1, GPIO_PIN_PC2, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 35", 1, GPIO_PIN_PD2, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 36", 1, GPIO_PIN_PD4, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 37", 3, GPIO_PIN_PA3, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 38", 1, GPIO_PIN_PD3, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 40", 1, GPIO_PIN_PD0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 11", 1, GPIO_PIN_PC4, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 15", 1, GPIO_PIN_PC5, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 16", 2, GPIO_PIN_PB6, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 18", 2, GPIO_PIN_PB7, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST PIN40_CONFIG  mRock4dPin40 = {
  2, { 4, 7 },
  2, { 8, 6 },
  1, { 1 },
  mRock4dPin40UartPins, ARRAY_SIZE (mRock4dPin40UartPins),
  mRock4dPin40BusPins,  ARRAY_SIZE (mRock4dPin40BusPins),
  mRock4dPin40GpioPins, ARRAY_SIZE (mRock4dPin40GpioPins),
};

CONST PIN40_CONFIG *
EFIAPI
PlatformGetPin40Config (
  VOID
  )
{
  return &mRock4dPin40;
}
