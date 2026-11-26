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
 *  Rk3576Fusb302Probe also says what the far end was, so a board that owns
 *  a VBUS switch can decide whether to power the port (TaishanPi 3M).
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

#include "Fusb302Internal.h"

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

/*
 * For Rk3576Fusb302Pd.c (Fusb302Internal.h): the same transfers as above,
 * a register and up to 30 data bytes in one write (address, register and
 * data fill the controller's 32-byte TXDATA0..7), and up to 32 bytes in one
 * read.  FIFOS (0x43) does not auto-increment, so a burst on it pushes or
 * pops that many FIFO bytes.
 */
BOOLEAN
Fusb302RegWrite (
  IN UINTN  Base,
  IN UINT8  Reg,
  IN UINT8  Val
  )
{
  return Fusb302Write (Base, Reg, Val);
}

BOOLEAN
Fusb302RegRead (
  IN  UINTN  Base,
  IN  UINT8  Reg,
  OUT UINT8  *Val
  )
{
  return Fusb302Read (Base, Reg, Val);
}

BOOLEAN
Fusb302RegWriteBurst (
  IN UINTN        Base,
  IN UINT8        Reg,
  IN CONST UINT8  *Data,
  IN UINTN        Len
  )
{
  UINT8    Buf[32];
  UINTN    N;
  UINTN    Index;
  UINTN    Byte;
  UINT32   Word;
  BOOLEAN  Ok;

  if ((Len == 0) || (Len > 30)) {
    return FALSE;
  }

  Buf[0] = FUSB302_ADDR << 1;
  Buf[1] = Reg;
  for (Index = 0; Index < Len; Index++) {
    Buf[2 + Index] = Data[Index];
  }

  N = Len + 2;
  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  for (Index = 0; Index < N; Index += 4) {
    Word = 0;
    for (Byte = 0; (Byte < 4) && (Index + Byte < N); Byte++) {
      Word |= (UINT32)Buf[Index + Byte] << (8 * Byte);
    }

    MmioWrite32 (Base + RKI2C_TXDATA0 + Index, Word);
  }

  MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TX | RKI2C_CON_START);
  Ok = RkI2cWait (Base, RKI2C_IPD_START);
  if (Ok) {
    MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TX);
    MmioWrite32 (Base + RKI2C_MTXCNT, (UINT32)N);
    Ok = RkI2cWait (Base, RKI2C_IPD_MBTF);
  }

  RkI2cStop (Base);
  return Ok;
}

BOOLEAN
Fusb302RegReadBurst (
  IN  UINTN  Base,
  IN  UINT8  Reg,
  OUT UINT8  *Data,
  IN  UINTN  Len
  )
{
  UINTN    Index;
  UINT32   Word;
  BOOLEAN  Ok;

  if ((Len == 0) || (Len > 32)) {
    return FALSE;
  }

  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Base + RKI2C_MRXADDR, BIT24 | (FUSB302_ADDR << 1) | 1);
  MmioWrite32 (Base + RKI2C_MRXRADDR, BIT24 | Reg);
  MmioWrite32 (
    Base + RKI2C_CON,
    RKI2C_CON_EN | RKI2C_CON_MOD_TRX | RKI2C_CON_START | RKI2C_CON_LASTACK
    );
  Ok = RkI2cWait (Base, RKI2C_IPD_START);
  if (Ok) {
    MmioWrite32 (Base + RKI2C_CON, RKI2C_CON_EN | RKI2C_CON_MOD_TRX | RKI2C_CON_LASTACK);
    MmioWrite32 (Base + RKI2C_MRXCNT, (UINT32)Len);
    Ok = RkI2cWait (Base, RKI2C_IPD_MBRF);
    if (Ok) {
      Word = 0;
      for (Index = 0; Index < Len; Index++) {
        if ((Index % 4) == 0) {
          Word = MmioRead32 (Base + RKI2C_RXDATA0 + Index);
        }

        Data[Index] = (UINT8)(Word >> (8 * (Index % 4)));
      }
    }
  }

  RkI2cStop (Base);
  return Ok;
}

/*
 * The bus as Rk3576Fusb302Probe sets it up, at 375 kHz rather than 100 kHz
 * for the PD exchange (the FUSB302 takes 1 MHz, the TaishanPi 3M's HYM8563
 * on the same bus 400 kHz): 24 MHz / (8 * (3 + 3 + 2)).  Close puts the
 * mux, the divider and the interrupt enables back as Open found them.
 */
VOID
Fusb302BusOpen (
  IN  CONST RK3576_FUSB302_BUS  *Bus,
  OUT FUSB302_BUS_SAVED         *Saved
  )
{
  UINT32  FieldMask;

  FieldMask       = 3U << Bus->ClkSelShift;
  Saved->ClkSel   = MmioRead32 (Bus->ClkSelReg) & FieldMask;
  Saved->ClkDiv   = MmioRead32 (Bus->I2cBase + RKI2C_CLKDIV);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | ((UINT32)Bus->ClkSelXin24m << Bus->ClkSelShift));
  MmioWrite32 (Bus->I2cBase + RKI2C_CLKDIV, (3U << 16) | 3U);
  MmioWrite32 (Bus->I2cBase + RKI2C_IEN, RKI2C_IPD_ALL);
}

VOID
Fusb302BusClose (
  IN CONST RK3576_FUSB302_BUS  *Bus,
  IN CONST FUSB302_BUS_SAVED   *Saved
  )
{
  UINT32  FieldMask;

  FieldMask = 3U << Bus->ClkSelShift;
  MmioWrite32 (Bus->I2cBase + RKI2C_IEN, 0);
  MmioWrite32 (Bus->I2cBase + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Bus->I2cBase + RKI2C_CLKDIV, Saved->ClkDiv);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | Saved->ClkSel);
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

CONST CHAR8 *
EFIAPI
Rk3576Fusb302PartnerName (
  IN RK3576_FUSB302_PARTNER  Partner
  )
{
  switch (Partner) {
    case Rk3576Fusb302NoChip:   return "no FUSB302";
    case Rk3576Fusb302I2cError: return "i2c error";
    case Rk3576Fusb302Open:     return "nothing attached";
    case Rk3576Fusb302Source:   return "source (Rp)";
    case Rk3576Fusb302Sink:     return "sink (Rd)";
    case Rk3576Fusb302RdBoth:   return "Rd on both lines";
    default:                    return "?";
  }
}

VOID
EFIAPI
Rk3576Fusb302Probe (
  IN  CONST RK3576_FUSB302_BUS  *Bus,
  OUT RK3576_FUSB302_STATE      *State
  )
{
  UINTN    Base;
  UINT32   FieldMask;
  UINT32   SavedSel;
  UINT32   SavedDiv;
  BOOLEAN  Ok1;
  BOOLEAN  Ok2;
  BOOLEAN  Cc1;
  BOOLEAN  Cc2;

  Base                = Bus->I2cBase;
  State->Partner      = Rk3576Fusb302NoChip;
  State->Orientation  = UsbTypeCOrientationUnknown;
  State->DeviceId     = 0;
  State->BcLvlCc1     = 0;
  State->BcLvlCc2     = 0;
  State->RpPresented  = FALSE;
  Cc1                 = FALSE;
  Cc2                 = FALSE;
  FieldMask           = 3U << Bus->ClkSelShift;

  SavedSel = MmioRead32 (Bus->ClkSelReg) & FieldMask;
  SavedDiv = MmioRead32 (Base + RKI2C_CLKDIV);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | ((UINT32)Bus->ClkSelXin24m << Bus->ClkSelShift));
  // 100 kHz from 24 MHz: 24e6 / (8 * 100e3) - 2 = 28, split low/high.
  MmioWrite32 (Base + RKI2C_CLKDIV, (14U << 16) | 14U);
  // The RK I2C driver enables each event before polling for it; do the same
  // for all of them, and clear it again on the way out so no interrupt is
  // left armed for the OS.
  MmioWrite32 (Base + RKI2C_IEN, RKI2C_IPD_ALL);

  if (!Fusb302Read (Base, FUSB302_DEVICE_ID, &State->DeviceId)) {
    DEBUG ((DEBUG_ERROR, "%a TypeC: no FUSB302 answering at 0x%02x\n", Bus->Name, FUSB302_ADDR));
    goto Restore;
  }

  State->Partner = Rk3576Fusb302I2cError;
  if (!(Fusb302SoftReset (Base) &&
        Fusb302Write (Base, FUSB302_POWER, FUSB302_POWER_ALL)))
  {
    goto Done;
  }

  //
  // Passive: is the other end a source?  Rd stays on both lines, so what
  // the partner sees does not change.
  //
  State->BcLvlCc1 = Fusb302SinkBcLvl (Base, FUSB302_SW0_MEAS_CC1, &Ok1);
  State->BcLvlCc2 = Fusb302SinkBcLvl (Base, FUSB302_SW0_MEAS_CC2, &Ok2);
  if (!Ok1 || !Ok2) {
    goto Done;
  }

  if ((State->BcLvlCc1 != 0) || (State->BcLvlCc2 != 0)) {
    DEBUG ((
      DEBUG_INFO,
      "%a TypeC: far end shows Rp (BC_LVL CC1 %u, CC2 %u): it is a source, not measured as one\n",
      Bus->Name,
      State->BcLvlCc1,
      State->BcLvlCc2
      ));
    State->Partner = Rk3576Fusb302Source;
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

  if (!Ok1 || !Ok2) {
    State->Partner = Rk3576Fusb302I2cError;
  } else if (Cc1 && Cc2) {
    State->Partner = Rk3576Fusb302RdBoth;
  } else if (!Cc1 && !Cc2) {
    State->Partner = Rk3576Fusb302Open;
  } else {
    State->Partner     = Rk3576Fusb302Sink;
    State->Orientation = Cc1 ? UsbTypeCOrientationNormal : UsbTypeCOrientationReverse;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a TypeC: FUSB302 id 0x%02x, CC1 %a, CC2 %a\n",
    Bus->Name,
    State->DeviceId,
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
  // A failed final reset keeps what was measured (the lanes still want the
  // orientation) but presents no Rp, so a board leaves VBUS off.
  Ok1 = Fusb302SoftReset (Base);
  if (!Ok1) {
    DEBUG ((DEBUG_ERROR, "%a TypeC: FUSB302 final reset failed\n", Bus->Name));
  }

  if (Ok1 && (State->Partner == Rk3576Fusb302Sink)) {
    if (Fusb302Write (Base, FUSB302_POWER, FUSB302_POWER_ALL) &&
        Fusb302Write (
          Base,
          FUSB302_SWITCHES0,
          (State->Orientation == UsbTypeCOrientationNormal) ? FUSB302_SW0_PU_EN1 : FUSB302_SW0_PU_EN2
          ))
    {
      State->RpPresented = TRUE;
    } else {
      // Rp could not be set: put the chip back to defaults rather than leave
      // it half-configured.  The orientation itself was measured, keep it.
      DEBUG ((DEBUG_ERROR, "%a TypeC: could not present Rp, resetting\n", Bus->Name));
      Fusb302SoftReset (Base);
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "%a TypeC: partner %a, orientation %a; FUSB302 left %a\n",
    Bus->Name,
    Rk3576Fusb302PartnerName (State->Partner),
    (State->Orientation == UsbTypeCOrientationNormal) ? "normal" :
    (State->Orientation == UsbTypeCOrientationReverse) ? "reverse" : "unknown",
    !State->RpPresented ? "reset (Rd on both lines)" :
    (State->Orientation == UsbTypeCOrientationNormal) ? "with Rp on CC1" : "with Rp on CC2"
    ));

Restore:
  MmioWrite32 (Base + RKI2C_IEN, 0);
  MmioWrite32 (Base + RKI2C_IPD, RKI2C_IPD_ALL);
  MmioWrite32 (Base + RKI2C_CLKDIV, SavedDiv);
  MmioWrite32 (Bus->ClkSelReg, (FieldMask << 16) | SavedSel);
}

USB_TYPEC_ORIENTATION
EFIAPI
Rk3576Fusb302GetOrientation (
  IN CONST RK3576_FUSB302_BUS  *Bus
  )
{
  RK3576_FUSB302_STATE  State;

  Rk3576Fusb302Probe (Bus, &State);
  return State.Orientation;
}
