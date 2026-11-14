/** @file
 *
 *  Secondary System Description Table (SSDT): the TaishanPi 3M fan as ACPI
 *  cooling devices, with active trip points on the big core thermal zone.
 *
 *  Installed only when "Fan Control by ACPI Thermal Zone" is enabled in setup
 *  (PcdAcpiFanControl, default off): RK3576AcpiPlatformDxe skips any SSDT
 *  with OEM table ID "RKFAN" otherwise, and the DSDT's zones then only report
 *  the temperature.
 *
 *  Model: ACPI 1.0 fans
 *  --------------------
 *
 *  Windows controls a fan with its inbox ACPI driver only as an ACPI 1.0,
 *  D-state fan: a PNP0C0B device that a thermal zone's _ALx lists, put in D0
 *  (on) or D3 (off) when the zone's temperature crosses _ACx.  The ACPI 4.0
 *  fan objects _FIF, _FPS and _FSL are not supported; only _FST is, and only
 *  for reporting.  ("Thermal management in Windows", section "Fan control by
 *  ACPI thermal zone" and the ACPI 4.0 table that follows it,
 *  https://learn.microsoft.com/windows-hardware/design/device-experiences/design-guide)
 *  The same page gives the way to more than one speed: several "fans" for
 *  one physical fan, more of them on meaning faster (ACPI specification,
 *  the multiple-speed fan example in the thermal management chapter).
 *
 *  So there are three PNP0C0B devices, FAN0..FAN2, each powered by its own
 *  power resource FPR0..FPR2.  The power resources only set a bit in FNST;
 *  the fan speed is the one for the highest bit that is set:
 *
 *    _AC2 45.0 C  FAN0  40%
 *    _AC1 60.0 C  FAN1  70%
 *    _AC0 75.0 C  FAN2 100%
 *    below all     -     0%
 *
 *  No _CRT and no _HOT: the code to temperature conversion has not been
 *  checked against a thermometer on this board, and a wrong reading at or
 *  above _CRT makes Windows shut down.  No hysteresis either: OSPM re-reads
 *  the trip points only on Notify 0x81, and none is sent; near a trip point
 *  the fan can change speed at most once per _TZP poll (5 s).
 *
 *  FNST starts at 7, all three on, because the firmware leaves the fan at
 *  100% (PcdPwmFanSpeedDefault): the power resources' _STA tell the OS the
 *  truth, and the OS turns off what the temperature does not need.  If it
 *  never does, the fan stays at full speed, which is the safe way to fail.
 *
 *  PWM writes
 *  ----------
 *
 *  The fan is PWM2 channel 3 (0x2ADE3000, see PwmFanSetSpeed in this board's
 *  RockchipPlatformLib.c), which the firmware left enabled in continuous
 *  mode.  A speed change writes DUTY (+0x14) as a fraction of the PERIOD
 *  (+0x10) the firmware programmed, then 0x00040004 to ENABLE (+0x04):
 *  CTRL_UPDATE_EN with its write mask, which commits DUTY at the end of the
 *  current period and changes no other bit of ENABLE (the PWMv4 order of the
 *  mainline-bound driver: PERIOD, DUTY, OFFSET, CTRL, then CTRL_UPDATE; here
 *  PERIOD, OFFSET and CTRL are unchanged).  Nothing is written unless
 *  ENABLE reads back PWM_EN and PWM_CLK_EN set and PERIOD is not 0, i.e.
 *  unless the channel is still running as the firmware left it.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("Fan.aml", "SSDT", 2, "RKCP  ", "RKFAN   ", 1)
{
  External (\_SB.TZ01, ThermalZoneObj)

  Scope (\_SB) {
    OperationRegion (FPWM, SystemMemory, 0x2ADE3000, 0x18)
    Field (FPWM, DWordAcc, NoLock, WriteAsZeros) {
      Offset (0x04),
      FENA, 32,     // ENABLE: [2] CTRL_UPDATE_EN (W1T), [1] PWM_EN, [0] PWM_CLK_EN, mask in [18:16]
      Offset (0x10),
      FPER, 32,     // PERIOD, in PWM clock cycles
      FDTY, 32,     // DUTY, in PWM clock cycles
    }

    Mutex (FNMX, 0)
    Name (FNST, 7)  // bit n: FPRn on

    // Duty in percent for each level, FAN0..FAN2, and with all off
    Name (FDL0, 40)
    Name (FDL1, 70)
    Name (FDL2, 100)
    Name (FDLO, 0)

    // Called with FNMX held
    Method (FNUP, 0, Serialized) {
      If ((FENA & 3) != 3) {
        Return ()
      }

      Local1 = FPER
      If (Local1 == 0) {
        Return ()
      }

      If (FNST & 4) {
        Local0 = FDL2
      } ElseIf (FNST & 2) {
        Local0 = FDL1
      } ElseIf (FNST & 1) {
        Local0 = FDL0
      } Else {
        Local0 = FDLO
      }

      FDTY = (Local1 * Local0) / 100
      FENA = 0x00040004
    }

    Method (FNSE, 2, Serialized) {
      // Arg0: bit, Arg1: 1 on / 0 off
      Acquire (FNMX, 0xFFFF)
      If (Arg1) {
        FNST |= Arg0
      } Else {
        FNST &= (0x7 ^ Arg0)
      }
      FNUP ()
      Release (FNMX)
    }

    PowerResource (FPR0, 0, 0) {
      Method (_STA) { Return ((FNST >> 0) & 1) }
      Method (_ON)  { FNSE (1, 1) }
      Method (_OFF) { FNSE (1, 0) }
    }

    PowerResource (FPR1, 0, 0) {
      Method (_STA) { Return ((FNST >> 1) & 1) }
      Method (_ON)  { FNSE (2, 1) }
      Method (_OFF) { FNSE (2, 0) }
    }

    PowerResource (FPR2, 0, 0) {
      Method (_STA) { Return ((FNST >> 2) & 1) }
      Method (_ON)  { FNSE (4, 1) }
      Method (_OFF) { FNSE (4, 0) }
    }

    Device (FAN0) {
      Name (_HID, EISAID ("PNP0C0B"))
      Name (_UID, 0)
      Name (_PR0, Package () { FPR0 })
    }

    Device (FAN1) {
      Name (_HID, EISAID ("PNP0C0B"))
      Name (_UID, 1)
      Name (_PR0, Package () { FPR1 })
    }

    Device (FAN2) {
      Name (_HID, EISAID ("PNP0C0B"))
      Name (_UID, 2)
      Name (_PR0, Package () { FPR2 })
    }
  }

  // Active trip points, tenths of a kelvin (2732 = 0.0 C), greatest first.
  Scope (\_SB.TZ01) {
    Name (_AC0, 3482)   // 75.0 C
    Name (_AL0, Package () { \_SB.FAN2 })
    Name (_AC1, 3332)   // 60.0 C
    Name (_AL1, Package () { \_SB.FAN1 })
    Name (_AC2, 3182)   // 45.0 C
    Name (_AL2, Package () { \_SB.FAN0 })
  }
}
