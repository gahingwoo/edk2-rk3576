/** @file
 *
 *  RK3576 DXE display variable management.
 *
 *  Copyright (c) 2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef DISPLAY_H_
#define DISPLAY_H_

VOID
EFIAPI
SetupDisplayVariables (
  VOID
  );

VOID
EFIAPI
ApplyDisplayVariables (
  VOID
  );

#endif // DISPLAY_H_
