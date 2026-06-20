/** @file
 *
 *  RK3576 Silicon-level Platform Library stub
 *  Board-specific init is in Platform/Radxa/ROCK4D/Library/RockchipPlatformLib/
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
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
