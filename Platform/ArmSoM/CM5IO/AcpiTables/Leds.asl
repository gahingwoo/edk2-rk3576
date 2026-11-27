/** @file
 *
 *  Secondary System Description Table (SSDT): the CM5-IO's LEDs for the OS.
 *
 *  Installed only with "Board LEDs for the OS" on in setup (PcdAcpiLeds,
 *  default off): RK3576AcpiPlatformDxe skips any SSDT with OEM table ID
 *  "RKLED" otherwise.  The driver is woa-rk3576 rkled (RKCP1ED0).
 *
 *  Two carrier LEDs (mainline rk3576-armsom-cm5-io.dts leds: led-0 and
 *  led-1; carrier schematic 1V1 nets LED_GREEN_EN and LED_RED_EN):
 *    green  GPIO2_D0, active high, LED_FUNCTION_STATUS, default-state "on"
 *    red    GPIO2_D1, active high, LED_FUNCTION_FAULT,  default-state "off"
 *  The device tree has no heartbeat LED, so the green one gets the
 *  heartbeat by default (the driver's default use) and the red one stays
 *  off; TriggerN in the registry changes either (rkled README).
 *
 *  The same two pins are 40-pin header pins 18 (GPIO2_D0) and 16 (GPIO2_D1)
 *  (Cm5ioPin40.c), which the header's RhProxy option also offers to
 *  applications as GPIOs.  With both options on, rkled holds them from boot
 *  and an application's open of those two pins fails; turn this option off
 *  to use them from an application.
 *
 *  The firmware's StatusLedDxe blinks the green LED during boot and turns
 *  both off at ExitBootServices (PlatformSetStatusLed); the driver takes
 *  them from there.
 *
 *  _DSD, one entry per GpioIo in _CRS, in order:
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
  External (\_SB.GPI2, DeviceObj)

  Scope (\_SB) {
    Device (LEDS) {
      Name (_HID, "RKCP1ED0")
      Name (_UID, 0)
      Name (_CRS, ResourceTemplate () {
        // led-0, green: GPIO2_D0 (bank 2 pin 24)
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.GPI2", 0, ResourceConsumer, ,) { 24 }
        // led-1, red: GPIO2_D1 (bank 2 pin 25)
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.GPI2", 0, ResourceConsumer, ,) { 25 }
      })
      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
        Package () {
          Package () { "rockchip,led-active-low", Package () { 0, 0 } },
          Package () { "rockchip,led-trigger", Package () { 2, 0 } },
        }
      })
      Method (_STA) { Return (0xF) }
    }
  }
}
