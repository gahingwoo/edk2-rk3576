/** @file
 *
 *  [SSDT] The CM5-IO's 40-pin header for Windows applications: its GPIO
 *  pins, one I2C bus and one SPI bus through \_SB.RHPX (RhProxy.asl).
 *  Installed only when "40-pin GPIO/I2C/SPI for apps" is on in setup
 *  (PcdPin40RhProxy, off by default); RK3576Dxe muxes the I2C and SPI pins,
 *  opens those controllers' clocks and gives the GPIO pins their GPIO
 *  function under the same option (Cm5ioPin40.c, Pin40.c).
 *
 *  Buses (names are what Windows.Devices.I2c and .Spi list):
 *
 *    _CRS 0  SPI3 M0 CS0   "SPI3"  MOSI pin 19, MISO 21, SCLK 23, CS0 24
 *    _CRS 1  I2C4 M3       "I2C4"  SDA pin 27, SCL pin 29
 *
 *  GPIO pins (Windows pin number = position below; board pin; SoC pin):
 *
 *     0  pin 7   GPIO0_C4   (also the IR receiver's line)
 *     1  pin 16  GPIO2_D1   (red LED)
 *     2  pin 18  GPIO2_D0   (green LED, the firmware's status LED)
 *
 *    and, while "40-pin UART" is off (no \_SB.URT2):
 *
 *     3  pin 3   GPIO4_B4
 *     4  pin 5   GPIO4_B5
 *
 *  SPI3 has a second chip select, but its M0 pin (GPIO2_D7) is not on the
 *  header.  I2C4 M3 is the carrier's camera 2 I2C as well; no camera is
 *  declared for Windows.  rk3xspi supports 8-bit frames only, 200 MHz
 *  divided by an even 2 to 65534, capped at 50 MHz here.
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
  External (\_SB.GPI0, DeviceObj)
  External (\_SB.GPI2, DeviceObj)
  External (\_SB.GPI4, DeviceObj)
  External (\_SB.I2C4, DeviceObj)
  External (\_SB.SPI3, DeviceObj)
  External (\_SB.URT2, DeviceObj)

  Scope (\_SB_)
  {
#define RHPX_BOARD_BUSES \
    SPISerialBusV2 (0, PolarityLow, FourWireMode, 8, ControllerInitiated, 4000000, \
                    ClockPolarityLow, ClockPhaseFirst, "\\_SB.SPI3", 0, ResourceConsumer, , Exclusive, ) \
    I2CSerialBusV2 (0xFFFF, ControllerInitiated, 0, AddressingMode7Bit, "\\_SB.I2C4", \
                    0, ResourceConsumer, , Exclusive, )

#define RHPX_BOARD_GPIOS \
    RHPX_GPIO ("\\_SB.GPI0", GPIO_PIN_PC4) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD1) \
    RHPX_GPIO ("\\_SB.GPI2", GPIO_PIN_PD0)

#define RHPX_BOARD_UART_GPIOS \
    RHPX_GPIO ("\\_SB.GPI4", GPIO_PIN_PB4) \
    RHPX_GPIO ("\\_SB.GPI4", GPIO_PIN_PB5)

#define RHPX_BOARD_UART_PROBE  \_SB.URT2

#define RHPX_BOARD_DSD \
      Package (2) { "bus-SPI-SPI3", Package () { 0 } }, \
      Package (2) { "SPI3-MinClockInHz", 3100 }, \
      Package (2) { "SPI3-MaxClockInHz", 50000000 }, \
      Package (2) { "SPI3-SupportedDataBitLengths", Package () { 8 } }, \
      Package (2) { "bus-I2C-I2C4", Package () { 1 } },

    include ("RhProxy.asl")
  }
}
