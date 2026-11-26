/** @file
 *
 *  Secondary System Description Table (SSDT): the TaishanPi 3M's LED for
 *  the OS.
 *
 *  Installed only with "Board LEDs" on in setup (PcdAcpiLeds,
 *  default off): RK3576AcpiPlatformDxe skips any SSDT with OEM table ID
 *  "RKLED" otherwise.  The driver is woa-rk3576 rkled (RKCP1ED0).
 *
 *  One LED (rk3576-lckfb-tspi-3m.dts leds/led-0): green, GPIO0_A0, active
 *  low, linux,default-trigger = "heartbeat".  The firmware's StatusLedDxe
 *  blinks it during boot and turns it off at ExitBootServices
 *  (PlatformSetStatusLed); the driver takes it from there.
 *
 *  _DSD, one entry per GpioIo in _CRS, in order (see the rkled README):
 *    rockchip,led-active-low   1 = the LED is on when the pin is low
 *    rockchip,led-trigger      0 off, 1 on, 2 heartbeat, 3 disk activity
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("Leds.aml", "SSDT", 2, "RKCP  ", "RKLED   ", 1)
{
  External (\_SB.GPI0, DeviceObj)

  Scope (\_SB) {
    Device (LEDS) {
      Name (_HID, "RKCP1ED0")
      Name (_UID, 0)
      Name (_CRS, ResourceTemplate () {
        // led-0, green heartbeat: GPIO0_A0 (pin 0 of bank 0)
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.GPI0", 0, ResourceConsumer, ,) { 0 }
      })
      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
        Package () {
          Package () { "rockchip,led-active-low", Package () { 1 } },
          Package () { "rockchip,led-trigger", Package () { 2 } },
        }
      })
      Method (_STA) { Return (0xF) }
    }
  }
}
