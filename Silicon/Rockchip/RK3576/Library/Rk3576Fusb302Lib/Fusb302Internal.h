/** @file
 *
 *  What Rk3576Fusb302Lib.c shares with Rk3576Fusb302Pd.c: the polled I2C
 *  transfers to the chip at 0x22 and the bus set-up around them.  Not a
 *  library class; nothing outside this library includes it.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef FUSB302_INTERNAL_H_
#define FUSB302_INTERNAL_H_

#include <Library/Rk3576Fusb302Lib.h>

typedef struct {
  UINT32    ClkSel;
  UINT32    ClkDiv;
} FUSB302_BUS_SAVED;

BOOLEAN
Fusb302RegWrite (
  IN UINTN  Base,
  IN UINT8  Reg,
  IN UINT8  Val
  );

BOOLEAN
Fusb302RegRead (
  IN  UINTN  Base,
  IN  UINT8  Reg,
  OUT UINT8  *Val
  );

BOOLEAN
Fusb302RegWriteBurst (
  IN UINTN        Base,
  IN UINT8        Reg,
  IN CONST UINT8  *Data,
  IN UINTN        Len
  );

BOOLEAN
Fusb302RegReadBurst (
  IN  UINTN  Base,
  IN  UINT8  Reg,
  OUT UINT8  *Data,
  IN  UINTN  Len
  );

VOID
Fusb302BusOpen (
  IN  CONST RK3576_FUSB302_BUS  *Bus,
  OUT FUSB302_BUS_SAVED         *Saved
  );

VOID
Fusb302BusClose (
  IN CONST RK3576_FUSB302_BUS  *Bus,
  IN CONST FUSB302_BUS_SAVED   *Saved
  );

#endif // FUSB302_INTERNAL_H_
