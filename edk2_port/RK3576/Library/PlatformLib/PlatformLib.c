/** @file
 *
 *  RK3576 Silicon-level Platform Library stub
 *  Board-specific init is in Platform/Radxa/ROCK4D/Library/RockchipPlatformLib/
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/DebugLib.h>

/* DRAM size detection — placeholder, actual size set by BL31/SPL */
UINT64
EFIAPI
PlatformGetDramSize (
  VOID
  )
{
  /* RK3576 ROCK 4D ships in 4GB and 8GB variants.
   * BL31 passes actual size via HOBs; return 4GB as safe default. */
  return (UINT64)SIZE_4GB;
}
