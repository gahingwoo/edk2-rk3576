/** @file
 *
 *  [SSDT body] \_SB.RHPX, the resource hub proxy that hands a board's 40-pin
 *  GPIO, I2C and SPI to Windows applications (Windows.Devices.Gpio, .I2c,
 *  .Spi), as Microsoft's "Enable user mode access to GPIO, I2C, and SPI"
 *  describes it.  Windows' inbox rhproxy.sys binds _CID MSFT8000 and
 *  starts only when every controller named below has started (rk3xgpio,
 *  rk3xi2c, rk3xspi from woa-rk3576); otherwise it waits with problem 51.
 *
 *  Included by a board's RhProxy SSDT (OEM table ID "RKRHPX") inside
 *  Scope (\_SB), with these defined first:
 *
 *    RHPX_BOARD_BUSES       the SPISerialBusV2 and I2CSerialBusV2
 *                           descriptors, first in _CRS, so that their
 *                           indices (0, 1, ...) are fixed
 *    RHPX_BOARD_GPIOS       RHPX_GPIO (Controller, Pin) for each header
 *                           pin that is a GPIO whatever the setup says
 *    RHPX_BOARD_UART_GPIOS  RHPX_GPIO for the header UARTs' pins, which
 *                           are GPIOs only while "40-pin UART" is off
 *    RHPX_BOARD_DSD         the bus-SPI-*, bus-I2C-* and per-bus
 *                           properties, each followed by a comma
 *
 *  Pin numbering is sequential (GPIO-UseDescriptorPinNumbers is not set):
 *  an application's pin n is the n-th GpioIo in _CRS.  Native numbering
 *  cannot work here, because the header's pins sit on four GPIO
 *  controllers that each number their pins 0 to 31.  The UART pins come
 *  last, so turning the UARTs on or off does not renumber the other pins.
 *
 *  Whether the UART pins are in _CRS is UAON, which RK3576AcpiPlatformDxe
 *  sets to 1 when it installs the UART SSDT as well ("40-pin UART" on).
 *  This table must not refer to anything the UART SSDT declares: it is
 *  installed first, so an External for one of those devices names an
 *  object that does not exist yet when Windows loads this table.  The
 *  first version did exactly that (External and CondRefOf on \_SB.URT4),
 *  the only forward reference between any of our tables, and Windows did
 *  not boot with both options on (TaishanPi 3M, 2026-11-25).  That is the
 *  suspected cause, not a proven one.
 *
 *  Each pin is a GpioIo followed by a GpioInt on the same pin, Shared, the
 *  GpioInt Edge and ActiveBoth (rk3xgpio asks GpioClx to emulate both
 *  edges).  Both carry RHPX_GPIO_PULL, PullDown unless the board says
 *  otherwise: Microsoft requires the pull to be the pin's power-on pull,
 *  and every RK3576 pin a board here offers comes out of reset with a
 *  pull-down (TRM V1.2 Part 1, the PMU0/PMU1/VCCIO_IOC_GPIOxx_PULL reset
 *  values).  "GPIO-SupportedDriveModes" is 0x9 (input, push-pull output):
 *  rk3xgpio does not change pulls, so a pin keeps that pull-down whatever
 *  drive mode an application picks.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#ifndef RHPX_GPIO_PULL
#define RHPX_GPIO_PULL  PullDown
#endif

#define RHPX_GPIO(Controller, Pin) \
  GpioIo (Shared, RHPX_GPIO_PULL, 0, 0, IoRestrictionNone, Controller, 0, ResourceConsumer, , ) { Pin } \
  GpioInt (Edge, ActiveBoth, Shared, RHPX_GPIO_PULL, 0, Controller, 0, ResourceConsumer, , ) { Pin }

Device (RHPX) {
  Name (_HID, "MSFT8000")
  Name (_CID, "MSFT8000")
  Name (_UID, 1)

  Name (RBUF, ResourceTemplate () {
    RHPX_BOARD_BUSES
    RHPX_BOARD_GPIOS
  })
  Name (UBUF, ResourceTemplate () {
    RHPX_BOARD_UART_GPIOS
  })

  //
  // 1: the UART SSDT is installed and its UARTs own their pins, so they are
  // not GPIOs.  Patched by RK3576AcpiPlatformDxe; keep it a plain Name with
  // a 0 initialiser (AML ZeroOp), which is what the patcher looks for.
  //
  Name (UAON, 0)

  Method (_CRS, 0x0, NotSerialized) {
    If (UAON) {
      Return (RBUF)
    }
    Return (ConcatenateResTemplate (RBUF, UBUF))
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      RHPX_BOARD_DSD
      Package (2) { "GPIO-SupportedDriveModes", 0x9 },
    }
  })

  Method (_STA) {
    Return (0xF)
  }
}
