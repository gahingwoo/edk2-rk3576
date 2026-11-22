/** @file
 *
 *  RK3576 TSADC thermal zones: temperature read-out and a critical trip.
 *
 *  Included inside Scope (\_SB).  Three zones, one per TSADC channel whose
 *  placement mainline and the TRM agree on (TRM V1.2 Table 19-1, rk3576.dtsi
 *  thermal-zones):
 *
 *    TZ00  channel 0, near the chip centre (mainline package-thermal)
 *    TZ01  channel 1, big core cluster (Cortex-A72; bigcore-thermal)
 *    TZ02  channel 2, little core cluster (Cortex-A53; littlecore-thermal)
 *
 *  Channels 3-5 are left out: rk3576.dtsi gives sensor@3 the DDR trim cell
 *  and sensor@5 the GPU one, while its own thermal zones and the TRM say
 *  channel 3 is the GPU and channel 5 the DDR.
 *
 *  Trip points
 *  -----------
 *
 *  Two layers protect the chip, from the bottom:
 *
 *    120 C + trim  TSHUT: the TSADC itself resets the chip through the CRU,
 *                  armed by Rk3576TsadcLib at boot (PcdTsadcTshutTemp).  It
 *                  needs no OS and no driver.  The same limit and mode
 *                  rk3576.dtsi gives Linux.
 *    115 C         _CRT, here, in every zone: the OS shuts down in an
 *                  orderly way before the hardware resets it.  5 C below
 *                  TSHUT so that it fires first; both compare the trimmed
 *                  temperature, and the two conversions (this one and the
 *                  firmware's temperature to code) differ by under 1 C.
 *
 *  No _HOT: Windows answers _HOT with a hibernate, and this platform has no
 *  S4 (no DSDT here defines \_S4 or any other \_Sx object), so it would at
 *  best do nothing.
 *
 *  No _PSV, _TC1, _TC2 or _TSP: passive cooling throttles the processors
 *  through _PSS or _CPC, and the CPUs here have neither (frequency is set
 *  once by the firmware).  A _PSV would be a trip point with nothing to act
 *  on.  So nothing slows the CPUs down when they get hot; that is why a CPU
 *  frequency above the firmware default must not be the default while
 *  these two limits are the only protection.
 *
 *  The fan's active trip points (_ACx/_ALx) are in the board's fan SSDT,
 *  which the firmware installs only when fan control is enabled in setup.
 *
 *  The TSADC is started by Rk3576TsadcLib (RK3576Dxe) in auto mode, with
 *  the TSHUT output to the CRU and no interrupt.  Nothing notifies these
 *  zones, so _TZP asks the OS to poll.
 *
 *  Conversion
 *  ----------
 *
 *  Mainline's rk3576 table (rk3588_code_table, rockchip_thermal.c:568-575),
 *  linear between rows, with the same integer division as
 *  rk_tsadcv2_code_to_temp (:686-689):
 *
 *    code 215..285   -40 C .. 25 C
 *    code 285..350    25 C .. 85 C
 *    code 350..395    85 C .. 125 C
 *    code 395..511   125 C
 *
 *  minus the channel's OTP trim (923 m C per code away from the 30 C code,
 *  rockchip_thermal.c:1092-1098 and :1641-1647), which the firmware folds
 *  into TSBn at boot.  AML integers are unsigned, so the table is evaluated
 *  as millidegrees above -40 C and TSBn carries the offset to millikelvin:
 *  233150 (= -40 C) minus the trim.
 *
 *  A data word below code 215 (including 0, before the first sample) or with
 *  bits above the 9-bit data mask set is not a temperature, and returns
 *  TBAD, 3732 (373.2 K, 100.0 C).  So does every read while TSEN is 0.
 *  A reading that cannot be trusted has to look hot, not cool: with the fan
 *  under ACPI control a cool value would stop the fan, a hot one runs it at
 *  full speed.  It stays below _CRT (115.0 C), so an untrusted reading never
 *  shuts the machine down.  100.0 C exactly, in a tool that shows the zones,
 *  means this.  A data word above the table (code 396-511) is a real
 *  reading of 125 C or more, as in mainline, and is at or above _CRT.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

// 1 once RK3576AcpiPlatformDxe has seen the TSADC out of reset, clocked and
// sampling.  While 0 the zones never touch the TSADC.
Name (TSEN, 0)
Name (TBAD, 3732)   // what _TMP returns for a reading it cannot trust: 100.0 C
Name (TCRT, 3882)   // _CRT of every zone: 115.0 C (tenths of a kelvin, 2732 = 0 C)

// Per-channel origin in millikelvin: 233150 minus the trim in millidegrees.
// Patched by RK3576AcpiPlatformDxe; written as DWord constants so the patch
// keeps their encoding.
Name (TSB0, 233150)
Name (TSB1, 233150)
Name (TSB2, 233150)

// TSADC_DATA0..2 (TRM 19.4.2, rockchip_thermal.c TSADCV3_DATA)
OperationRegion (TSAD, SystemMemory, 0x2AE7002C, 0x0C)
Field (TSAD, DWordAcc, NoLock, Preserve) {
  TSD0, 32,
  TSD1, 32,
  TSD2, 32,
}

// Arg0: raw data word, Arg1: TSBn.  Returns tenths of a kelvin.
Method (TSCV, 2, Serialized) {
  Local0 = Arg0
  If ((Local0 > 0x1FF) || (Local0 < 215)) {
    Return (TBAD)
  }

  If (Local0 <= 285) {
    Local1 = (65000 * (Local0 - 215)) / 70
  } ElseIf (Local0 <= 350) {
    Local1 = 65000 + ((60000 * (Local0 - 285)) / 65)
  } ElseIf (Local0 <= 395) {
    Local1 = 125000 + ((40000 * (Local0 - 350)) / 45)
  } Else {
    Local1 = 165000
  }

  // millidegrees above -40 C + origin = millikelvin; round to 0.1 K
  Return ((Local1 + Arg1 + 50) / 100)
}

ThermalZone (TZ00) {
  Name (_STR, Unicode ("SoC centre (TSADC 0)"))
  Name (_TZP, 50)     // poll every 5.0 s
  Method (_CRT, 0, NotSerialized) {
    Return (TCRT)
  }
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (TBAD)
    }
    Return (TSCV (TSD0, TSB0))
  }
}

ThermalZone (TZ01) {
  Name (_STR, Unicode ("Big cores (TSADC 1)"))
  Name (_TZP, 50)
  Method (_CRT, 0, NotSerialized) {
    Return (TCRT)
  }
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (TBAD)
    }
    Return (TSCV (TSD1, TSB1))
  }
}

ThermalZone (TZ02) {
  Name (_STR, Unicode ("Little cores (TSADC 2)"))
  Name (_TZP, 50)
  Method (_CRT, 0, NotSerialized) {
    Return (TCRT)
  }
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (TBAD)
    }
    Return (TSCV (TSD2, TSB2))
  }
}
