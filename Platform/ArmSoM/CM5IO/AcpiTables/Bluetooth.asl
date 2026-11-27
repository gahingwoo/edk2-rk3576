/** @file
 *
 *  [DSDT] Bluetooth of the CM5 module's BW3752-50B1 (Synaptics/Broadcom
 *  BCM43752, whose Bluetooth core identifies as BCM4362A2) on UART4, for
 *  the woa-rk3576 bcm4345bt driver, which binds RKCP4362.
 *
 *  Windows has no driver for the RK3576's DesignWare UART, so, as on the
 *  TaishanPi 3M (Platform/LCKFB/TaishanPi3M/AcpiTables/Bluetooth.asl, which
 *  says why), the Bluetooth device claims the UART's registers and
 *  interrupt and its driver runs the UART itself.
 *
 *  From rk3576.dtsi (uart4: serial@2ad70000, GIC_SPI 80, reg-shift 2,
 *  reg-io-width 4), rk3576-armsom-cm5.dtsi (&uart4 with uart4m1_xfer,
 *  uart4m1_ctsn and uart4m1_rtsn, uart-has-rtscts) and the ArmSoM-CM5-1V1
 *  schematic (V1.1, pages 11 and 17), which names every pin:
 *    UART4_TX/RX/CTSN/RTSN_M1   GPIO1 PC4/PC5/PC3/PC2 to U8 UART_*
 *    BT_REG_ON_H                GPIO1 PC7 to U8 BT_REG_ON
 *    HOST_WAKE_BT_H             GPIO1 PD4 to U8 HOST_WAKE_BT (host to module)
 *    BT_WAKE_HOST_H             GPIO0 PB1 from U8 BT_WAKE_HOST (module to host)
 *  ArmSoM's vendor tree (rockchip-kernel linux-6.1-stan-rkr6.1,
 *  rk3576-armsom-cm5.dtsi, wireless-bluetooth) agrees on UART4 M1 and
 *  BT,power_gpio = GPIO1 PC7 active high.  These are the TaishanPi 3M's
 *  GPIOs too; only the UART differs (UART6 there).  Cm5ioBluetooth.c sets
 *  the pins and the 24 MHz UART clock.
 *
 *  Resources, in the order the driver expects them:
 *    0  Memory     UART4 registers
 *    1  Interrupt  UART4, SPI 80 = GSIV 112, level high
 *    2  GpioIo     BT_REG_ON, GPIO1 PC7 (pin 23 of GPI1), output, active high
 *    3  GpioIo     BT_WAKE (HOST_WAKE_BT_H), GPIO1 PD4 (pin 28 of GPI1),
 *                  output, active high
 *
 *  BT_WAKE_HOST (GPIO0 PB1, pin 9 of GPI0, module to host) is not declared,
 *  for the reason the TaishanPi 3M's table gives: the driver does not sleep
 *  the module.
 *
 *  max-speed 1500000 is not from a device tree (the CM5's has no bluetooth
 *  node); it is the driver's default and the TaishanPi 3M's.  If the module
 *  does not answer at that rate the driver stays at 115200.
 *
 *  The HID is not RKCP4345: the patchram differs (BCM4362A2.hcd, not
 *  BCM4345C5.hcd), and the driver's INF picks it by the HID.
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

Device (BTH0) {
  Name (_HID, "RKCP4362")
  Name (_UID, 0)
  Name (_DDN, "CM5 BCM43752 Bluetooth (BCM4362A2) on UART4")

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x2AD70000, 0x100)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 112 }
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
      Package () { "compatible", "brcm,bcm43752" },
      Package () { "clock-frequency", 24000000 },   // sclk_uart4, Cm5ioBluetooth.c
      Package () { "max-speed", 1500000 },
      Package () { "reg-shift", 2 },
      Package () { "reg-io-width", 4 },
    }
  })

  Method (_STA) { Return (0xf) }
}
