/** @file
 *
 *  Secondary System Description Table (SSDT): the Radxa ROCK 4D fan header
 *  (J16) as ACPI cooling devices, with active trip points on the big core
 *  thermal zone.
 *
 *  The same model, trip points, hysteresis and register writes as the
 *  TaishanPi 3M's Fan.asl (Platform/LCKFB/TaishanPi3M/AcpiTables/Fan.asl),
 *  which explains them in full.  Only what differs is here.
 *
 *  Installed only when "Fan Mode" in setup (PcdAcpiFanControl) is not
 *  "Always on", the default.  With "Always on" the firmware leaves the fan
 *  where it starts it, PcdPwmFanSpeedDefault = 100%.  Otherwise the trip
 *  points follow the mode's on-point T, \_SB.FTON, which
 *  RK3576AcpiPlatformDxe patches (Performance 50 C, Balanced 60 C, Quiet
 *  70 C):
 *
 *          on at   off below   fan    duty
 *    _AC2  T       T - 10 C    FAN0   100%
 *    _AC1  T + 10  T + 5 C     FAN1   100%
 *    _AC0  T + 20  T + 15 C    FAN2   100%
 *    below all      -           -       0%
 *
 *  Every level is 100%, so the fan is on or off and the pin never switches
 *  at a partial duty, as on the TaishanPi 3M.
 *
 *  PWM writes: PWM2 channel 5, 0x2ADE5000 (vendor rk3576.dtsi:6131-6141;
 *  PwmFanSetSpeed in this board's RockchipPlatformLib.c), normal polarity,
 *  left by the firmware enabled in continuous mode.
 *
 *  NOT RUN ON A ROCK 4D.  It compiles.
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
    OperationRegion (FPWM, SystemMemory, 0x2ADE5000, 0x18)
    Field (FPWM, DWordAcc, NoLock, WriteAsZeros) {
      Offset (0x04),
      FENA, 32,     // ENABLE: [2] CTRL_UPDATE_EN (W1T), [1] PWM_EN, [0] PWM_CLK_EN, mask in [18:16]
      Offset (0x10),
      FPER, 32,     // PERIOD, in PWM clock cycles
      FDTY, 32,     // DUTY, in PWM clock cycles
    }

    Mutex (FNMX, 0)
    Name (FNST, 7)  // bit n: FPRn on

    // Fan mode on-point, tenths of a kelvin: 3232 Performance (50.0 C),
    // 3332 Balanced (60.0 C), 3432 Quiet (70.0 C).  Patched by
    // RK3576AcpiPlatformDxe; every value keeps the WordConst encoding.
    Name (FTON, 3332)

    // Duty in percent for each level, FAN0..FAN2, and with all off
    Name (FDL0, 100)
    Name (FDL1, 100)
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
      Local0 = FNST
      If (Arg1) {
        FNST |= Arg0
      } Else {
        FNST &= (0x7 ^ Arg0)
      }
      Local1 = (FNST != Local0)
      FNUP ()
      Release (FNMX)

      // The level's trip point moved (hysteresis): have OSPM re-read them
      If (Local1) {
        Notify (\_SB.TZ01, 0x81)
      }
    }

    // Arg0: level bit, Arg1: on-point, Arg2: off-point
    Method (FTRP, 3, NotSerialized) {
      If (FNST & Arg0) {
        Return (Arg2)
      }
      Return (Arg1)
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
    Method (_AC0, 0, NotSerialized) {
      Return (\_SB.FTRP (4, \_SB.FTON + 200, \_SB.FTON + 150))
    }
    Name (_AL0, Package () { \_SB.FAN2 })
    Method (_AC1, 0, NotSerialized) {
      Return (\_SB.FTRP (2, \_SB.FTON + 100, \_SB.FTON + 50))
    }
    Name (_AL1, Package () { \_SB.FAN1 })
    Method (_AC2, 0, NotSerialized) {
      Return (\_SB.FTRP (1, \_SB.FTON, \_SB.FTON - 100))
    }
    Name (_AL2, Package () { \_SB.FAN0 })
  }
}
