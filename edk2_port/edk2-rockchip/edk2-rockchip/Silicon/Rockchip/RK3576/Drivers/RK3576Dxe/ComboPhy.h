/** @file
 *
 *  RK3576 DXE ComboPHY variable management.
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef COMBO_PHY_H_
#define COMBO_PHY_H_

VOID
EFIAPI
SetupComboPhyVariables (
  VOID
  );

VOID
EFIAPI
ApplyComboPhyVariables (
  VOID
  );

#endif // COMBO_PHY_H_
