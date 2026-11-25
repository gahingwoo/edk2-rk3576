/** @file
 *
 *  The RK806's power key as the OS's power button.
 *
 *  Installed only with "Power button (RK806)" on in setup (PcdAcpiPwrKey,
 *  default off) on a board whose RK806 the firmware reaches
 *  (PcdRk806I2cBase): RK3576AcpiPlatformDxe drops this SSDT otherwise
 *  (OEM table ID "RKPWRKEY").
 *
 *  The board's power button is wired to the RK806's PWRON pin, not to the
 *  SoC: ROCK 4D SW1 -> PWRON_L (schematic V1.12, Power-DC IN/RTC page);
 *  CM5-IO SW4 "PWR_KEY" -> PWRON_L on the module connector (carrier
 *  schematic 1V1); TaishanPi 3M: the KEY page's power key drives net PWRON
 *  (LCKFB project v1.4).  The PMIC reports a press and a release as
 *  PWRON_FALL and PWRON_RISE in INT_STS0 (0x77, bits 0 and 1; mainline
 *  rk808.h, rk8xx-core.c rk806_irqs, rk805-pwrkey.c) and pulls its INT line
 *  low until the status is written back.  That line is GPIO0_A6 on all three
 *  boards (rk3576-armsom-cm5.dtsi, rk3576-rock-4d.dts and
 *  rk3576-lckfb-tspi-3m.dts pmic@23: interrupt-parent = <&gpio0>,
 *  interrupts = <RK_PA6 IRQ_TYPE_LEVEL_LOW>; pmic_pins pulls it up), and the
 *  PMIC is at 0x23 on I2C1 on all three.
 *
 *  Why a driver and not an ACPI-only power button (a GED, ACPI0013, whose
 *  _EVT reads INT_STS0 and does Notify (PNP0C0C, 0x80)): the interrupt has
 *  to be acknowledged over I2C, which AML can do only through a
 *  GenericSerialBus operation region that Windows routes to the bus driver.
 *  rk3xi2c is an SpbCx driver, so that path may well work, but nothing on
 *  these boards has ever exercised it, a failing access in _EVT is silent,
 *  and a level interrupt left unacknowledged keeps the line low for good.
 *  So the device below is for a driver: woa-rk3576 rk806pwrkey reads and
 *  clears INT_STS0 on each interrupt (a passive-level ISR) and reports a
 *  press as HID System Control "Power Down" through vhf.sys, the path
 *  rk3xsaradc already uses for keys, which Windows treats as its power
 *  button.  The PMIC's own long press (power off) is untouched.
 *
 *  \_SB.I2C1.PMC0 (the PMIC itself, RKCP0806, which the firmware owns) stays
 *  as it is; this is a second, separate device on the same address, with
 *  the connection marked shared.
 *
 *  Rk806I2cPowerOffDxe makes GPIO0_A6 a pulled-up GPIO input (the GPIO
 *  driver does not set pin functions) and at ExitBootServices sets the
 *  PMIC's interrupt active low with only PWRON_FALL and PWRON_RISE
 *  unmasked, and clears what is pending; the driver does the same at start.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("PwrKey.aml", "SSDT", 2, "RKCP  ", "RKPWRKEY", 1)
{
  External (\_SB.I2C1, DeviceObj)
  External (\_SB.GPI0, DeviceObj)

  Scope (\_SB.I2C1) {
    Device (PWRK) {
      Name (_HID, "RKCP806B")
      Name (_UID, 0)
      Name (_CRS, ResourceTemplate () {
        // 100 kHz: none of the three device trees sets clock-frequency on
        // &i2c1, which is the Linux default.
        I2cSerialBusV2 (0x0023, ControllerInitiated, 100000,
                        AddressingMode7Bit, "\\_SB.I2C1",
                        0x00, ResourceConsumer, , Shared,)
        // GPIO0_A6, pin 6 of bank 0, level low, pulled up (pmic_pins).
        GpioInt (Level, ActiveLow, Exclusive, PullUp, 0x0000,
                 "\\_SB.GPI0", 0x00, ResourceConsumer, ,) { 6 }
      })
      Method (_STA) { Return (0xF) }
    }
  }
}
