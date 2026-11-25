/** @file
 *
 *  [SSDT] The ROCK 4D's 40-pin header for Windows applications: its GPIO
 *  pins, two I2C buses and one SPI bus through \_SB.RHPX (RhProxy.asl).
 *  Installed only when "40-pin GPIO/I2C/SPI for apps" is on in setup
 *  (PcdPin40RhProxy, off by default); RK3576Dxe muxes the I2C and SPI pins,
 *  opens those controllers' clocks and gives the GPIO pins their GPIO
 *  function under the same option (Rock4dPin40.c, Pin40.c).
 *
 *  Buses (names are what Windows.Devices.I2c and .Spi list):
 *
 *    _CRS 0  SPI1 M0 CS0   "SPI1"  MOSI pin 19, MISO 21, SCLK 23, CS0 24
 *    _CRS 1  SPI1 M0 CS1   "SPI1"  CS1 pin 26
 *    _CRS 2  I2C8 M1       "I2C8"  SDA pin 3, SCL pin 5 (the default I2C bus)
 *    _CRS 3  I2C6 M3       "I2C6"  SDA pin 27, SCL pin 28; the board's
 *                                  BL24C16F EEPROM answers at 0x50
 *
 *  GPIO pins (Windows pin number = position below; board pin; SoC pin):
 *
 *     0  pin 7   GPIO1_C3       7  pin 33  GPIO1_C2
 *     1  pin 12  GPIO1_D1       8  pin 35  GPIO1_D2
 *     2  pin 13  GPIO2_C0       9  pin 36  GPIO1_D4
 *     3  pin 22  GPIO2_D7      10  pin 37  GPIO3_A3
 *     4  pin 29  GPIO3_A2      11  pin 38  GPIO1_D3
 *     5  pin 31  GPIO1_C1      12  pin 40  GPIO1_D0
 *     6  pin 32  GPIO1_D5
 *
 *    and, while "40-pin UART" is off (no \_SB.URT4):
 *
 *    13  pin 11  GPIO1_C4      15  pin 16  GPIO2_B6
 *    14  pin 15  GPIO1_C5      16  pin 18  GPIO2_B7
 *
 *  rk3xspi supports 8-bit frames only, 200 MHz divided by an even 2 to
 *  65534, capped at 50 MHz here.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM ON THIS BOARD.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include "AcpiTables.h"

DefinitionBlock ("Pin40RhProxy.aml", "SSDT", 2, "RKCP  ", "RKRHPX  ", 1)
{
  External (\_SB.GPI1, DeviceObj)
  External (\_SB.GPI2, DeviceObj)
  External (\_SB.GPI3, DeviceObj)
  External (\_SB.I2C6, DeviceObj)
  External (\_SB.I2C8, DeviceObj)
  External (\_SB.SPI1, DeviceObj)
  External (\_SB.URT4, DeviceObj)

  Scope (\_SB_)
  {
#define RHPX_BOARD_BUSES \
    SPISerialBusV2 (0, PolarityLow, FourWireMode, 8, ControllerInitiated, 4000000, \
                    ClockPolarityLow, ClockPhaseFirst, "\\_SB.SPI1", 0, ResourceConsumer, , Exclusive, ) \
    SPISerialBusV2 (1, PolarityLow, FourWireMode, 8, ControllerInitiated, 4000000, \
                    ClockPolarityLow, ClockPhaseFirst, "\\_SB.SPI1", 0, ResourceConsumer, , Exclusive, ) \
    I2CSerialBusV2 (0xFFFF, ControllerInitiated, 0, AddressingMode7Bit, "\\_SB.I2C8", \
                    0, ResourceConsumer, , Exclusive, ) \
    I2CSerialBusV2 (0xFFFF, ControllerInitiated, 0, AddressingMode7Bit, "\\_SB.I2C6", \
                    0, ResourceConsumer, , Exclusive, )

#define RHPX_BOARD_GPIOS \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PC3) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD1) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PC0) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD7) \
    RHPX_GPIO ("\\_SB.GPI3", GPIO_PIN_PA2) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PC1) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD5) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PC2) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD2) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD4) \
    RHPX_GPIO ("\\_SB.GPI3", GPIO_PIN_PA3) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD3) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PD0)

#define RHPX_BOARD_UART_GPIOS \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PC4) \
    RHPX_GPIO ("\\_SB.GPI1", GPIO_PIN_PC5) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PB6) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PB7)

#define RHPX_BOARD_UART_PROBE  \_SB.URT4

#define RHPX_BOARD_DSD \
      Package (2) { "bus-SPI-SPI1", Package () { 0, 1 } }, \
      Package (2) { "SPI1-MinClockInHz", 3100 }, \
      Package (2) { "SPI1-MaxClockInHz", 50000000 }, \
      Package (2) { "SPI1-SupportedDataBitLengths", Package () { 8 } }, \
      Package (2) { "bus-I2C-I2C8", Package () { 2 } }, \
      Package (2) { "bus-I2C-I2C6", Package () { 3 } },

    include ("RhProxy.asl")
  }
}
