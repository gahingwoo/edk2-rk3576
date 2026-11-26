/** @file
 *
 *  [SSDT] The TaishanPi 3M's 40-pin header for Windows applications: its
 *  GPIO pins, two I2C buses and one SPI bus through \_SB.RHPX (RhProxy.asl).
 *  Installed only when "40-pin GPIO/I2C/SPI for apps" is on in setup
 *  (PcdPin40RhProxy, off by default); RK3576Dxe muxes the I2C and SPI pins,
 *  opens those controllers' clocks and gives the GPIO pins their GPIO
 *  function under the same option (Tspi3mPin40.c, Pin40.c).
 *
 *  Buses (names are what Windows.Devices.I2c and .Spi list):
 *
 *    _CRS 0  SPI1 M1 CS0   "SPI1"  MOSI pin 19, MISO 21, SCLK 23, CS0 24
 *    _CRS 1  SPI1 M1 CS1   "SPI1"  CS1 pin 26
 *    _CRS 2  I2C7 M1       "I2C7"  SDA pin 3, SCL pin 5 (the default I2C bus)
 *    _CRS 3  I2C8 M2       "I2C8"  SDA pin 27, SCL pin 28
 *
 *  GPIO pins (Windows pin number = position below; board pin; SoC pin):
 *
 *     0  pin 7   GPIO4_A6       8  pin 29  GPIO2_C0
 *     1  pin 11  GPIO4_A4       9  pin 31  GPIO2_B0
 *     2  pin 12  GPIO0_B5      10  pin 32  GPIO2_C7
 *     3  pin 13  GPIO2_C6      11  pin 33  GPIO4_A7
 *     4  pin 15  GPIO0_C7      12  pin 35  GPIO0_D2
 *     5  pin 16  GPIO0_C6      13  pin 36  GPIO2_D7
 *     6  pin 18  GPIO3_A3      14  pin 38  GPIO3_A2
 *     7  pin 22  GPIO2_D6
 *
 *    and, while "40-pin UART" is off:
 *
 *    15  pin 8   GPIO2_D0      17  pin 37  GPIO2_A6
 *    16  pin 10  GPIO2_D1      18  pin 40  GPIO2_A7
 *
 *  The controllers are the DSDT's (Gpio.asl GPI0..GPI4, I2c.asl I2C7 and
 *  I2C8, Spi.asl SPI1).  SPI clock limits are rk3xspi's: 200 MHz divided by
 *  an even BAUDR of 2 to 65534, capped at 50 MHz here.  rk3xspi supports
 *  8-bit frames only.
 *
 *  Windows 11 23H2 did not boot with this table and the UART SSDT both
 *  installed (2026-11-25); see RhProxy.asl for the forward reference that
 *  was removed.  The fixed table has not been booted yet.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include "AcpiTables.h"

DefinitionBlock ("Pin40RhProxy.aml", "SSDT", 2, "RKCP  ", "RKRHPX  ", 1)
{
  External (\_SB.GPI0, DeviceObj)
  External (\_SB.GPI2, DeviceObj)
  External (\_SB.GPI3, DeviceObj)
  External (\_SB.GPI4, DeviceObj)
  External (\_SB.I2C7, DeviceObj)
  External (\_SB.I2C8, DeviceObj)
  External (\_SB.SPI1, DeviceObj)

  Scope (\_SB_)
  {
#define RHPX_BOARD_BUSES \
    SPISerialBusV2 (0, PolarityLow, FourWireMode, 8, ControllerInitiated, 4000000, \
                    ClockPolarityLow, ClockPhaseFirst, "\\_SB.SPI1", 0, ResourceConsumer, , Exclusive, ) \
    SPISerialBusV2 (1, PolarityLow, FourWireMode, 8, ControllerInitiated, 4000000, \
                    ClockPolarityLow, ClockPhaseFirst, "\\_SB.SPI1", 0, ResourceConsumer, , Exclusive, ) \
    I2CSerialBusV2 (0xFFFF, ControllerInitiated, 0, AddressingMode7Bit, "\\_SB.I2C7", \
                    0, ResourceConsumer, , Exclusive, ) \
    I2CSerialBusV2 (0xFFFF, ControllerInitiated, 0, AddressingMode7Bit, "\\_SB.I2C8", \
                    0, ResourceConsumer, , Exclusive, )

#define RHPX_BOARD_GPIOS \
    RHPX_GPIO ("\\_SB.GPI4", GPIO_PIN_PA6) \
    RHPX_GPIO ("\\_SB.GPI4", GPIO_PIN_PA4) \
    RHPX_GPIO ("\\_SB.GPI0", GPIO_PIN_PB5) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PC6) \
    RHPX_GPIO ("\\_SB.GPI0", GPIO_PIN_PC7) \
    RHPX_GPIO ("\\_SB.GPI0", GPIO_PIN_PC6) \
    RHPX_GPIO ("\\_SB.GPI3", GPIO_PIN_PA3) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD6) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PC0) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PB0) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PC7) \
    RHPX_GPIO ("\\_SB.GPI4", GPIO_PIN_PA7) \
    RHPX_GPIO ("\\_SB.GPI0", GPIO_PIN_PD2) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD7) \
    RHPX_GPIO ("\\_SB.GPI3", GPIO_PIN_PA2)

#define RHPX_BOARD_UART_GPIOS \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD0) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD1) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PA6) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PA7)

#define RHPX_BOARD_DSD \
      Package (2) { "bus-SPI-SPI1", Package () { 0, 1 } }, \
      Package (2) { "SPI1-MinClockInHz", 3100 }, \
      Package (2) { "SPI1-MaxClockInHz", 50000000 }, \
      Package (2) { "SPI1-SupportedDataBitLengths", Package () { 8 } }, \
      Package (2) { "bus-I2C-I2C7", Package () { 2 } }, \
      Package (2) { "bus-I2C-I2C8", Package () { 3 } },

    include ("RhProxy.asl")
  }
}
