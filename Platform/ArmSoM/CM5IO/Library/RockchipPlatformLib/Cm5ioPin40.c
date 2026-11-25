/** @file
 *
 *  Cm5ioPin40.c -- the CM5-IO's 40-pin header (J4), for the "40-pin UART"
 *  and "40-pin GPIO/I2C/SPI for apps" setup options (RK3576Dxe Pin40.c does
 *  the work from this description).  The ACPI side is Pin40Uart.asl and
 *  Pin40RhProxy.asl in this board's AcpiTables; the three must agree.
 *
 *  Source: ArmSoM-CM5-IO V1.1 schematic, sheet 7 (Audio_40PIN, connector J4,
 *  PZ2.54-2x20), the net next to each pin; the SoC pin behind each net from
 *  mainline rk3576-pinctrl.dtsi (Linux 7.3), where the net names the
 *  functions it carries:
 *
 *     3 CAN1_RX/I2C3_SDA/UART_RX2    GPIO4_B4 (uart2m1 rx, i2c3m0 sda, can1m2 rx)
 *     5 CAN1_TX/I2C3_SCL/UART_TX2    GPIO4_B5
 *     7 PWM0_CH0_M0_IR               GPIO0_C4 (pwm0m0_ch0)
 *     8 UART0_TX_M0_PORT             GPIO0_D4, the debug console: not touched
 *    10 UART0_RX_M0_PORT             GPIO0_D5, the same
 *    16 LED_RED_EN/UART4_RX/I2C6_SDA   GPIO2_D1 (uart4m0 rx, i2c6m2 sda)
 *    18 LED_GREEN_EN/UART4_TX/I2C6_SCL GPIO2_D0
 *    19 SPI3_MOSI_M0/SAI3_LRCK_M2   GPIO3_A1
 *    21 SPI3_MISO_M0/SAI3_SDO_M2    GPIO3_A2
 *    22 ADC_VIN4_3V3                 SARADC input, not a GPIO
 *    23 SPI3_CLK_M0/SAI3_SCLK_M2    GPIO3_A0
 *    24 SPI3_CSN0_M0/SAI3_SDI_M2    GPIO3_A3
 *    27 I2C4_SDA_M3_MIPI_CAM2_3V3   GPIO3_B7
 *    28 PWM2_CH6_M2/SAI3_MCLK_M2    GPIO2_D6, also FAN_PWM ("CHOOSE ONE"): left out
 *    29 I2C4_SCL_M3_MIPI_CAM2_3V3   GPIO3_C0
 *    31 MIPI_DPHY_CSI_CAM2_RST_3V3  a camera reset whose SoC pin the carrier
 *                                   schematic does not show: left out
 *
 *  UART4 is not offered although pins 16/18 carry its M0 option: the CM5
 *  module's Bluetooth uses UART4 (M1, rk3576-armsom-cm5.dtsi; Bluetooth.asl).
 *  Pins 16/18 drive the carrier's LEDs and are offered as GPIOs; the
 *  firmware's status LED is the green one (PlatformSetStatusLed).
 *
 *  Function numbers (rk3576-pinctrl.dtsi): uart2m1_xfer 10, pull-up;
 *  i2c4m3_xfer 11, no pull; spi3m0_pins and spi3m0_csn0 10, no pull.  None
 *  of these pins is used by the board's device tree or by this library.
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

STATIC CONST GPIO_IOMUX_CONFIG  mCm5ioPin40UartPins[] = {
  { "uart2_rx_m1 (pin 3)", 4, GPIO_PIN_PB4, 10, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
  { "uart2_tx_m1 (pin 5)", 4, GPIO_PIN_PB5, 10, GPIO_PIN_PULL_UP, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST GPIO_IOMUX_CONFIG  mCm5ioPin40BusPins[] = {
  { "i2c4_sda_m3 (pin 27)",  3, GPIO_PIN_PB7, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "i2c4_scl_m3 (pin 29)",  3, GPIO_PIN_PC0, 11, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi3_mosi_m0 (pin 19)", 3, GPIO_PIN_PA1, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi3_miso_m0 (pin 21)", 3, GPIO_PIN_PA2, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi3_clk_m0 (pin 23)",  3, GPIO_PIN_PA0, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "spi3_csn0_m0 (pin 24)", 3, GPIO_PIN_PA3, 10, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

/* In Pin40RhProxy.asl's GpioIo order; the two UART pins come last there. */
STATIC CONST GPIO_IOMUX_CONFIG  mCm5ioPin40GpioPins[] = {
  { "pin 7",  0, GPIO_PIN_PC4, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 16", 2, GPIO_PIN_PD1, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 18", 2, GPIO_PIN_PD0, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 3",  4, GPIO_PIN_PB4, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
  { "pin 5",  4, GPIO_PIN_PB5, 0, GPIO_PIN_PULL_NONE, GPIO_PIN_DRIVE_DEFAULT },
};

STATIC CONST PIN40_CONFIG  mCm5ioPin40 = {
  1, { 2 },
  1, { 4 },
  1, { 3 },
  mCm5ioPin40UartPins, ARRAY_SIZE (mCm5ioPin40UartPins),
  mCm5ioPin40BusPins,  ARRAY_SIZE (mCm5ioPin40BusPins),
  mCm5ioPin40GpioPins, ARRAY_SIZE (mCm5ioPin40GpioPins),
};

CONST PIN40_CONFIG *
EFIAPI
PlatformGetPin40Config (
  VOID
  )
{
  return &mCm5ioPin40;
}
