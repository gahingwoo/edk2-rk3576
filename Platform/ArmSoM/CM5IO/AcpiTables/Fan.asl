/** @file
 *
 *  Secondary System Description Table (SSDT): the CM5-IO carrier's fan
 *  header (CN32) as ACPI cooling devices, with active trip points on the big
 *  core thermal zone.
 *
 *  The same model and the same register writes as the TaishanPi 3M's
 *  Fan.asl (Platform/LCKFB/TaishanPi3M/AcpiTables/Fan.asl), which explains
 *  them in full: three ACPI 1.0 PNP0C0B fans, each with a power resource,
 *  for one physical fan at three speeds, because the inbox Windows ACPI
 *  driver only turns fans on (D0) and off (D3).  Only what differs is here.
 *
 *  Installed only when "Fan Control by ACPI Thermal Zone" is enabled in
 *  setup (PcdAcpiFanControl, default off on this board).  With it off the
 *  firmware leaves the fan where it starts it, PcdPwmFanSpeedDefault = 100%,
 *  which holds the pin at the level it has always had (see the polarity
 *  note in PwmFanIoSetup, RockchipPlatformLib.c).
 *
 *  Trip points (tenths of a kelvin, 2732 = 0.0 C), lower than the TaishanPi
 *  3M's because this fan's behaviour at partial duty has not been seen yet,
 *  and its default, before this SSDT, never turned it down:
 *
 *    _AC2 40.0 C  FAN0  50%
 *    _AC1 50.0 C  FAN1  75%
 *    _AC0 60.0 C  FAN2 100%
 *    below all     -     0%
 *
 *  The vendor device tree runs this fan from 50 C (20%) to 70 C (100%)
 *  (rk3576-armsom-cm5-io.dts:53-59, rockchip,temp-trips); every level here
 *  is reached earlier and is at least as fast.
 *
 *  The thermal zones' _CRT (115 C) and the hardware TSHUT (120 C) are in
 *  Thermal.asl and Rk3576TsadcLib; this table adds no shutdown of its own.
 *
 *  PWM writes: PWM2 channel 6, 0x2ADE6000 (vendor rk3576.dtsi:6049-6051),
 *  left by the firmware enabled in continuous mode with the output inverted
 *  (CTRL bit 3 inactive positive, duty negative; Rk3576PwmLib.c), so DUTY
 *  as a fraction of PERIOD is still "fan on" for the duty part of the
 *  period.  DUTY (+0x14), then 0x00040004 to ENABLE (+0x04), the
 *  CTRL_UPDATE_EN commit with its write mask; nothing is written unless
 *  ENABLE reads back PWM_EN and PWM_CLK_EN set and PERIOD is not 0.
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
    OperationRegion (FPWM, SystemMemory, 0x2ADE6000, 0x18)
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
    Name (FDL0, 50)
    Name (FDL1, 75)
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
    Name (_AC0, 3332)   // 60.0 C
    Name (_AL0, Package () { \_SB.FAN2 })
    Name (_AC1, 3232)   // 50.0 C
    Name (_AL1, Package () { \_SB.FAN1 })
    Name (_AC2, 3132)   // 40.0 C
    Name (_AL2, Package () { \_SB.FAN0 })
  }
}
