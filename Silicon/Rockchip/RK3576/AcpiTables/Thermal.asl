/** @file
 *
 *  RK3576 TSADC thermal zones: temperature read-out only.
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
 *  No trip points here: no _CRT, _HOT, _PSV or _ACx, so nothing the OS reads
 *  from these zones can shut the machine down or throttle it.  The fan's
 *  active trip points are in the board's fan SSDT, which the firmware only
 *  installs when fan control is enabled in setup.  _PSV is not used at all:
 *  the CPUs have no _PSS or _CPC, so there is nothing passive cooling could
 *  act on.
 *
 *  The TSADC is started by Rk3576TsadcLib (RK3576Dxe) in auto mode with no
 *  TSHUT output and no interrupt.  Nothing notifies these zones, so _TZP asks
 *  the OS to poll.
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
 *  3000 (300.0 K, 26.85 C).  So does every read while TSEN is 0.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

// 1 once RK3576AcpiPlatformDxe has seen the TSADC out of reset, clocked and
// sampling.  While 0 the zones never touch the TSADC.
Name (TSEN, 0)

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
    Return (3000)
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
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (3000)
    }
    Return (TSCV (TSD0, TSB0))
  }
}

ThermalZone (TZ01) {
  Name (_STR, Unicode ("Big cores (TSADC 1)"))
  Name (_TZP, 50)
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (3000)
    }
    Return (TSCV (TSD1, TSB1))
  }
}

ThermalZone (TZ02) {
  Name (_STR, Unicode ("Little cores (TSADC 2)"))
  Name (_TZP, 50)
  Method (_TMP, 0, Serialized) {
    If (TSEN == 0) {
      Return (3000)
    }
    Return (TSCV (TSD2, TSB2))
  }
}
