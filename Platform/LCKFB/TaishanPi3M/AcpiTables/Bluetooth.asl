/** @file
 *
 *  [DSDT] Bluetooth of the AP6256 module (BCM4345C5) on UART6, for the
 *  woa-rk3576 bcm4345bt driver, which binds RKCP4345.
 *
 *  Windows has no driver for the RK3576's DesignWare UART: the ARM64 image
 *  (22621) ships SerCx and SerCx2 but no serial.sys and no 16550 controller
 *  driver, and serial.sys would not handle a 4-byte register stride anyway.
 *  So the UART is not a device of its own with the Bluetooth part behind a
 *  UartSerialBusV2 connection, as Microsoft's SerialHCIBus model would have
 *  it.  The Bluetooth device claims the UART's registers and interrupt and
 *  its driver runs the UART itself.  If a SerCx2 driver for this UART ever
 *  exists, split this into a UAR6 device and a UartSerialBusV2 resource.
 *
 *  From rk3576.dtsi (uart6: serial@2ad90000, GIC_SPI 82, reg-shift 2,
 *  reg-io-width 4) and rk3576-lckfb-tspi-3m.dts (&uart6 with uart6m1 pins
 *  and RTS/CTS, and its bluetooth child: brcm,bcm4345c5, max-speed 1500000,
 *  shutdown-gpios GPIO1 PC7, device-wakeup-gpios GPIO1 PD4, host-wakeup
 *  GPIO0 PB1 level high).  Wiring checked against the schematic; see
 *  Tspi3mBluetooth.c, which sets the pins and the 24 MHz UART clock.
 *
 *  Resources, in the order the driver expects them:
 *    0  Memory     UART6 registers
 *    1  Interrupt  UART6, SPI 82 = GSIV 114, level high
 *    2  GpioIo     BT_REG_ON, GPIO1 PC7 (pin 23 of GPI1), output, active high
 *    3  GpioIo     BT_WAKE, GPIO1 PD4 (pin 28 of GPI1), output, active high
 *
 *  HOST_WAKE (GPIO0 PB1, pin 9 of GPI0, level high, module to host) is not
 *  declared: the driver does not sleep the module, so nothing would wait for
 *  it, and a GpioInt would arrive as a second interrupt resource that the
 *  driver would have to tell apart from the UART's.  Add it with
 *  GpioInt (Level, ActiveHigh, Exclusive, PullDown, 0, "\\_SB.GPI0")
 *  { GPIO_PIN_PB1 } once the driver uses the module's sleep mode.
 *
 *  Linux is not affected: it boots from the device tree and does not read
 *  this table.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (BTH0) {
  Name (_HID, "RKCP4345")
  Name (_UID, 0)
  Name (_DDN, "AP6256 Bluetooth (BCM4345C5) on UART6")

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x2AD90000, 0x100)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 114 }
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
              "\\_SB.GPI1", 0, ResourceConsumer) { GPIO_PIN_PC7 }
      GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
              "\\_SB.GPI1", 0, ResourceConsumer) { GPIO_PIN_PD4 }
    })
    Return (RBUF)
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "brcm,bcm4345c5" },
      Package () { "clock-frequency", 24000000 },   // sclk_uart6, Tspi3mBluetooth.c
      Package () { "max-speed", 1500000 },          // dts &uart6 bluetooth
      Package () { "reg-shift", 2 },
      Package () { "reg-io-width", 4 },
    }
  })

  Method (_STA) { Return (0xf) }
}
