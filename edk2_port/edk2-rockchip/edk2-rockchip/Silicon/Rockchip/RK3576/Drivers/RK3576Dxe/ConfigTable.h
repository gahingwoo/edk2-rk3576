/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RK3576DXE_CONFIG_TABLE_H__
#define __RK3576DXE_CONFIG_TABLE_H__

//
// Don't declare these in the VFR file.
//
#ifndef VFR_FILE_INCLUDE
VOID
EFIAPI
ApplyConfigTableVariables (
  VOID
  );

VOID
EFIAPI
SetupConfigTableVariables (
  VOID
  );

#endif // VFR_FILE_INCLUDE

#endif // __RK3576DXE_CONFIG_TABLE_H__
