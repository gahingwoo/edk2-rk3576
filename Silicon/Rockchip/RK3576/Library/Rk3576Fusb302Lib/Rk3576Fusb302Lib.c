/** @file
 *
 *  Plug orientation of a USB-C port from an FCS FUSB302.
 *
 *  Moved out of the CM5-IO board library (e07fde3, 71dc514) so the TaishanPi
 *  3M, whose FUSB302 sits on I2C2 next to the RTC, can use the same code.
 *
 *  Called once, from UsbHcd's ExitBootServices handler, before the USBDP
 *  PHY's lanes are muxed.  Nothing else in UEFI drives the chip, and nothing
 *  must be left running in it for the OS beyond a source's Rp on the attached
 *  line: Linux's fusb302 driver starts with a software reset of its own, and
 *  Windows has no driver for it.
 *
 *  What is done to the chip, in order:
 *
 *   1. DEVICE_ID is read.  No ACK: nothing else is written.
 *   2. Software reset (all registers to their power-on values: Rd on both CC
 *      lines, MDAC and comparator off), then the internal blocks are powered
 *      so the comparator and BC_LVL work.
 *   3. Passive check, Rd kept on both lines: each CC line is measured with
 *      BC_LVL.  A level above 200 mV means the other end presents Rp, i.e.
 *      it is a source (a PC on the MaskROM port, a charger).  This board is
 *      not its host then, so the answer is Unknown and no Rp is ever shown.
 *   4. Active check, as a source: the 80 uA Rp on one CC line, the line
 *      compared against 1.6 V and 0.2 V, then the other line.  A sink's Rd
 *      (5.1k) sits at about 0.4 V, an open line goes high, a cable's Ra (1k)
 *      stays under 0.2 V.  CC1 = normal, CC2 = reverse (mainline typec
 *      convention; the USBDP PHY flips its lanes for reverse).  This is
 *      mainline fusb302.c's fusb302_get_src_cc_status() without its toggling
 *      state machine.  Each line has Rp for about 2 ms.
 *   5. Software reset again, whatever the result.  If a sink was found on
 *      exactly one line, Rp (80 uA, default USB power, CONTROL0's reset
 *      value) is then presented on that line only, as a host port should,
 *      so a sink or dock with its own CC logic sees a stable attach under
 *      Windows, which has no port manager.  Otherwise (nothing attached, a
 *      source, Rd on both lines, an I2C error) the chip is left reset, as a
 *      cold boot leaves it.  Rp is never presented against a source.
 *      Linux's fusb302 starts with its own software reset either way.
 *
 *  Never written: VBUS (the board's own GPIO switch), the PD transmitter and
 *  receiver (CONTROL1..3, FIFOs), the toggle state machine, VCONN, the
 *  interrupt masks.
 *
 *  The RK I2C master here is polled and stand-alone.  At ExitBootServices
 *  other drivers' protocols may already be gone, and on the TaishanPi 3M the
 *  RTC's I2cDxe shares the controller, so its clock mux and divider are put
 *  back as found.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rk3576Fusb302Lib.h>
#include <Library/TimerLib.h>

#define RKI2C_CON               0x000
#define RKI2C_CLKDIV            0x004
#define RKI2C_MRXADDR           0x008
#define RKI2C_MRXRADDR          0x00c
#define RKI2C_MTXCNT            0x010
#define RKI2C_MRXCNT            0x014
#define RKI2C_IEN               0x018
#define RKI2C_IPD               0x01c
#define RKI2C_TXDATA0           0x100
#define RKI2C_RXDATA0           0x200
#define RKI2C_CON_EN            BIT0
#define RKI2C_CON_MOD_TX        (0U << 1)
#define RKI2C_CON_MOD_TRX       (1U << 1)
#define RKI2C_CON_START         BIT3
#define RKI2C_CON_STOP          BIT4
#define RKI2C_CON_LASTACK       BIT5
#define RKI2C_IPD_MBTF          BIT2
#define RKI2C_IPD_MBRF          BIT3
#define RKI2C_IPD_START         BIT4
#define RKI2C_IPD_STOP          BIT5
#define RKI2C_IPD_NAK           BIT6
#define RKI2C_IPD_ALL           0x7F

#define FUSB302_ADDR            0x22
#define FUSB302_DEVICE_ID       0x01
#define FUSB302_SWITCHES0       0x02
#define FUSB302_SW0_PU_EN2      BIT7
#define FUSB302_SW0_PU_EN1      BIT6
#define FUSB302_SW0_MEAS_CC2    BIT3
#define FUSB302_SW0_MEAS_CC1    BIT2
#define FUSB302_SW0_PDWN2       BIT1
#define FUSB302_SW0_PDWN1       BIT0
#define FUSB302_MEASURE         0x04
#define FUSB302_MDAC_1V6        0x26    // 42 mV steps
#define FUSB302_MDAC_0V2        0x05
#define FUSB302_POWER           0x0B
#define FUSB302_POWER_ALL       0x0F
#define FUSB302_RESET           0x0C
#define FUSB302_RESET_SW        BIT0
#define FUSB302_STATUS0         0x40
#define FUSB302_STATUS0_COMP    BIT5
#define FUSB302_STATUS0_BC_LVL  (BIT1 | BIT0)

STATIC
BOOLEAN
RkI2cWait (
  IN UINTN   Base,
  IN UINT32  Bits
  )
{
  UINTN   Us;
  UINT32  Ipd;

  for (Us = 0; Us < 10000; Us++) {
    Ipd = MmioRead32 (Base + RKI2C_IPD);
    if ((Ipd & RKI2C_IPD_NAK) != 0) {
      MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_NAK);
      return FALSE;
    }

    if ((Ipd & Bits) != 0) {
      MmioWrite32 (Base + RKI2C_IPD, Bits);
      return TRUE;
    }

    MicroSecondDelay (1);
  }

  return FALSE;
}

STATIC
VOID
RkI2cStop (
  IN UINTN  Base
  )
{
  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_STOP);
  RkI2cWait (Base, RKI2C_IPD_STOP);
  MmioWrite32 (Base + RKI2C_CON, 0);
}

STATIC
BOOLEAN
Fusb302Write (
  IN UINTN  Base,
  IN UINT8  Reg,
  IN UINT8  Val
  )
{
  BOOLEAN  Ok;

  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Base + RKI2C_TXDATA0, (FUSB302_ADDR << 1) | ((UINT32)Reg << 8) | ((UINT32)Val << 16));
  MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TX | RKI2C_CON_START);
  Ok = RkI2cWait (Base, RKI2C_IPD_START);
  if (Ok) {
    MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TX);
    MmioWrite32 (Base + RKI2C_MTXCNT, 3);
    Ok = RkI2cWait (Base, RKI2C_IPD_MBTF);
  }

  RkI2cStop (Base);
  return Ok;
}

STATIC
BOOLEAN
Fusb302Read (
  IN  UINTN  Base,
  IN  UINT8  Reg,
  OUT UINT8  *Val
  )
{
  BOOLEAN  Ok;

  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  // Register read: address+W, register, restart, address+R, one byte, NAK.
  MmioWrite32 (Base + RKI2C_MRXADDR, BIT24 | (FUSB302_ADDR << 1) | 1);
  MmioWrite32 (Base + RKI2C_MRXRADDR, BIT24 | Reg);
  MmioWrite32 (
    Base + RKI2C_CON,
    RKI2C_CON_EN | RKI2C_CON_MOD_TRX | RKI2C_CON_START | RKI2C_CON_LASTACK
    );
  Ok = RkI2cWait (Base, RKI2C_IPD_START);
  if (Ok) {
    MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TRX | RKI2C_CON_LASTACK);
    MmioWrite32 (Base + RKI2C_MRXCNT, 1);
    Ok = RkI2cWait (Base, RKI2C_IPD_MBRF);
    if (Ok) {
      *Val = (UINT8)MmioRead32 (Base + RKI2C_RXDATA0);
    }
  }

  RkI2cStop (Base);
  return Ok;
}

/* BC_LVL of one CC line with Rd kept on both: non-zero = the far end shows Rp. */
STATIC
UINT8
Fusb302SinkBcLvl (
  IN  UINTN    Base,
  IN  UINT8    MeasCc,
  OUT BOOLEAN  *Ok
  )
{
  UINT8  Status0;

  Status0 = 0;
  *Ok     = Fusb302Write (Base, FUSB302_SWITCHES0, FUSB302_SW0_PDWN1 | FUSB302_SW0_PDWN2 | MeasCc);
  MicroSecondDelay (1000);
  *Ok = *Ok && Fusb302Read (Base, FUSB302_STATUS0, &Status0);
  return *Ok ? (Status0 & FUSB302_STATUS0_BC_LVL) : 0;
}

/* TRUE when the line reads between the two thresholds, i.e. a sink's Rd. */
STATIC
BOOLEAN
Fusb302CcHasRd (
  IN  UINTN    Base,
  IN  UINT8    PullUpAndMeasure,
  OUT BOOLEAN  *Ok
  )
{
  UINT8  High;
  UINT8  Low;

  High = 0;
  Low  = 0;
  *Ok  = Fusb302Write (Base, FUSB302_SWITCHES0, PullUpAndMeasure) &&
         Fusb302Write (Base, FUSB302_MEASURE, FUSB302_MDAC_1V6);
  MicroSecondDelay (1000);
  *Ok = *Ok && Fusb302Read (Base, FUSB302_STATUS0, &High);
  *Ok = *Ok && Fusb302Write (Base, FUSB302_MEASURE, FUSB302_MDAC_0V2);
  MicroSecondDelay (1000);
  *Ok = *Ok && Fusb302Read (Base, FUSB302_STATUS0, &Low);
  if (!*Ok) {
    return FALSE;
  }

  // COMP = 1 when the line is above MDAC.
  return ((High & FUSB302_STATUS0_COMP) == 0) && ((Low & FUSB302_STATUS0_COMP) != 0);
}

STATIC
BOOLEAN
Fusb302SoftReset (
  IN UINTN  Base
  )
{
  BOOLEAN  Ok;

  Ok = Fusb302Write (Base, FUSB302_RESET, FUSB302_RESET_SW);
  // 100 us was not enough on CM5-IO: the next access failed, 2026-10-17.
  MicroSecondDelay (10000);
  return Ok;
}

USB_TYPEC_ORIENTATION
EFIAPI
Rk3576Fusb302GetOrientation (
  IN CONST RK3576_FUSB302_BUS  *Bus
  )
{
  USB_TYPEC_ORIENTATION  Result;
  UINTN                  Base;
  UINT32                 FieldMask;
  UINT32                 SavedSel;
  UINT32                 SavedDiv;
  UINT8                  Id;
  UINT8                  Lvl1;
  UINT8                  Lvl2;
  BOOLEAN                Ok1;
  BOOLEAN                Ok2;
  BOOLEAN                Cc1;
  BOOLEAN                Cc2;
  BOOLEAN                Attached;
  CONST CHAR8            *Why;

  Base      = Bus->I2cBase;
  Result    = UsbTypeCOrientationUnknown;
  Id        = 0;
  Cc1       = FALSE;
  Cc2       = FALSE;
  Why       = "";
  FieldMask = 3U << Bus->ClkSelShift;

  SavedSel = MmioRead32 (Bus->ClkSelReg) & FieldMask;
  SavedDiv = MmioRead32 (Base + RKI2C_CLKDIV);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | ((UINT32)Bus->ClkSelXin24m << Bus->ClkSelShift));
  // 100 kHz from 24 MHz: 24e6 / (8 * 100e3) - 2 = 28, split low/high.
  MmioWrite32 (Base + RKI2C_CLKDIV, (14U << 16) | 14U);
  // The RK I2C driver enables each event before polling for it; do the same
  // for all of them, and clear it again on the way out so no interrupt is
  // left armed for the OS.
  MmioWrite32 (Base + RKI2C_IEN, RKI2C_IPD_ALL);

  if (!Fusb302Read (Base, FUSB302_DEVICE_ID, &Id)) {
    DEBUG ((DEBUG_ERROR, "%a TypeC: no FUSB302 answering at 0x%02x\n", Bus->Name, FUSB302_ADDR));
    goto Restore;
  }

  if (!(Fusb302SoftReset (Base) &&
        Fusb302Write (Base, FUSB302_POWER, FUSB302_POWER_ALL)))
  {
    Why = "setup failed";
    goto Done;
  }

  //
  // Passive: is the other end a source?  Rd stays on both lines, so what
  // the partner sees does not change.
  //
  Lvl1 = Fusb302SinkBcLvl (Base, FUSB302_SW0_MEAS_CC1, &Ok1);
  Lvl2 = Fusb302SinkBcLvl (Base, FUSB302_SW0_MEAS_CC2, &Ok2);
  if (!Ok1 || !Ok2) {
    Why = "i2c error in the passive check";
    goto Done;
  }

  if ((Lvl1 != 0) || (Lvl2 != 0)) {
    DEBUG ((
      DEBUG_INFO,
      "%a TypeC: far end shows Rp (BC_LVL CC1 %u, CC2 %u): it is a source, not measured as one\n",
      Bus->Name,
      Lvl1,
      Lvl2
      ));
    Why = "partner is a source";
    goto Done;
  }

  //
  // Active, as a source.  A measurement that fails on I2C is retried once.
  //
  Cc1 = Fusb302CcHasRd (Base, FUSB302_SW0_PU_EN1 | FUSB302_SW0_MEAS_CC1, &Ok1);
  if (!Ok1) {
    Cc1 = Fusb302CcHasRd (Base, FUSB302_SW0_PU_EN1 | FUSB302_SW0_MEAS_CC1, &Ok1);
  }

  Cc2 = Fusb302CcHasRd (Base, FUSB302_SW0_PU_EN2 | FUSB302_SW0_MEAS_CC2, &Ok2);
  if (!Ok2) {
    Cc2 = Fusb302CcHasRd (Base, FUSB302_SW0_PU_EN2 | FUSB302_SW0_MEAS_CC2, &Ok2);
  }

  if (Ok1 && Ok2 && (Cc1 != Cc2)) {
    Result = Cc1 ? UsbTypeCOrientationNormal : UsbTypeCOrientationReverse;
  }

  Why = (!Ok1 || !Ok2) ? "i2c error" :
        (Cc1 && Cc2) ? "Rd on both lines" :
        (!Cc1 && !Cc2) ? "nothing attached" : "measured";
  DEBUG ((
    DEBUG_INFO,
    "%a TypeC: FUSB302 id 0x%02x, CC1 %a, CC2 %a\n",
    Bus->Name,
    Id,
    !Ok1 ? "i2c error" : (Cc1 ? "Rd" : "open"),
    !Ok2 ? "i2c error" : (Cc2 ? "Rd" : "open")
    ));

Done:
  //
  // Back to the power-on state, whatever happened above.  Then, only when a
  // sink was found on exactly one line (never against a source), present Rp
  // on that line, as a host port should, so a sink or dock with its own CC
  // logic sees a stable attach under an OS with no port manager (Windows).
  // CONTROL0 is left at its reset value, HOST_CUR = 01 (80 uA, default USB
  // power), with INT_MASK set.  Linux's fusb302 software-resets the chip in
  // its init (tcpm_init -> fusb302_sw_reset) and starts from defaults.
  //
  Attached = (Result != UsbTypeCOrientationUnknown);
  if (!Fusb302SoftReset (Base)) {
    DEBUG ((DEBUG_ERROR, "%a TypeC: FUSB302 final reset failed\n", Bus->Name));
    Attached = FALSE;
  }

  if (Attached &&
      !(Fusb302Write (Base, FUSB302_POWER, FUSB302_POWER_ALL) &&
        Fusb302Write (
          Base,
          FUSB302_SWITCHES0,
          (Result == UsbTypeCOrientationNormal) ? FUSB302_SW0_PU_EN1 : FUSB302_SW0_PU_EN2
          )))
  {
    // Rp could not be set: put the chip back to defaults rather than leave
    // it half-configured.  The orientation itself was measured, keep it.
    DEBUG ((DEBUG_ERROR, "%a TypeC: could not present Rp, resetting\n", Bus->Name));
    Fusb302SoftReset (Base);
    Attached = FALSE;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a TypeC: orientation %a (%a); FUSB302 left %a\n",
    Bus->Name,
    (Result == UsbTypeCOrientationNormal) ? "normal" :
    (Result == UsbTypeCOrientationReverse) ? "reverse" : "unknown",
    Why,
    !Attached ? "reset" :
    (Result == UsbTypeCOrientationNormal) ? "with Rp on CC1" : "with Rp on CC2"
    ));

Restore:
  MmioWrite32 (Base + RKI2C_IEN, 0);
  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Base + RKI2C_CLKDIV, SavedDiv);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | SavedSel);
  return Result;
}
