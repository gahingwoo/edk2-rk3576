/** @file
 *
 *  Secondary System Description Table (SSDT): the ROCK 4D's LEDs for the OS.
 *
 *  Installed only with "Board LEDs for the OS" on in setup (PcdAcpiLeds,
 *  default off): RK3576AcpiPlatformDxe skips any SSDT with OEM table ID
 *  "RKLED" otherwise.  The driver is woa-rk3576 rkled (RKCP1ED0).
 *
 *  Two LEDs (rk3576-rock-4d.dts leds, dts:48-66; schematic V1.12
 *  "40PIN/FAN/LED/KEY" page, PWR_LED and USE_LED):
 *    power-led  green, GPIO0_B4, active high, default-state = "on"
 *    user-led   blue,  GPIO0_C4, active low,  linux,default-trigger =
 *               "heartbeat"
 *  The firmware's StatusLedDxe blinks the power LED during boot and turns it
 *  off at ExitBootServices (PlatformSetStatusLed); the driver takes both
 *  from there.
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
        // power-led, green: GPIO0_B4 (bank 0 pin 12)
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.GPI0", 0, ResourceConsumer, ,) { 12 }
        // user-led, blue: GPIO0_C4 (bank 0 pin 20)
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.GPI0", 0, ResourceConsumer, ,) { 20 }
      })
      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
        Package () {
          Package () { "rockchip,led-active-low", Package () { 0, 1 } },
          Package () { "rockchip,led-trigger", Package () { 1, 2 } },
        }
      })
      Method (_STA) { Return (0xF) }
    }
  }
}
