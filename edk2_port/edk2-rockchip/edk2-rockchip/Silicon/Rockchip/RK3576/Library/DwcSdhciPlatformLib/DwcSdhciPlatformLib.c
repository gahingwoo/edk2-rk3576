/** @file
 *
 *  DwcSdhciDxe platform helper library — RK3576 variant.
 *
 *  Differs from the RK3588 version only in the CRU base address
 *  (0x27200000) and the eMMC clock-select register offset.
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DwcSdhciPlatformLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

/*
 * RK3576 eMMC clock control (per clk-rk3576 driver):
 *   CCLK_SRC_EMMC mux+div is in CRU_CLKSEL_CON(89), bits [15:8].
 *     bits[15:14] : mux  00 = gpll_400m  01 = cpll_400m  10 = xin_24m
 *     bits[13:8]  : divider-1 (1..64)
 *   The gate (CRU_CLKGATE_CON(33), bit 8) is opened by SdhciEmmcIoMux.
 *
 * If the divider write turns out wrong on a particular silicon
 * revision the controller will still expose a working base clock
 * via its capability register and the SDHCI core will tune from
 * the slow-mode 400 KHz init clock.
 */
#define RK3576_CLKSEL_EMMC        CRU_CLKSEL_CON(89)
#define EMMC_SEL_GPLL_400M        0
#define EMMC_SEL_CPLL_400M        1
#define EMMC_SEL_XIN_24M          2

EFI_STATUS
EFIAPI
DwcSdhciSetClockRate (
  IN UINTN  Frequency
  )
{
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Div;

  if (Frequency == 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (Frequency <= 24000000) {
    Sel    = EMMC_SEL_XIN_24M;
    Parent = 24000000;
  } else {
    Sel    = EMMC_SEL_GPLL_400M;
    Parent = 400000000;
  }

  Div = (Parent + Frequency - 1) / Frequency;
  if (Div < 1)  { Div = 1; }
  if (Div > 64) { Div = 64; }

  MmioWrite32 (
    RK3576_CLKSEL_EMMC,
    (((0x3U << 14) | (0x3FU << 8)) << 16) |
    (Sel << 14) |
    ((Div - 1) << 8)
    );

  DEBUG ((
    DEBUG_INFO,
    "DwcSdhciSetClockRate: req=%lu Hz -> parent=%u Hz / %u (mux=%u)\n",
    (UINT64)Frequency, Parent, Div, Sel
    ));

  return EFI_SUCCESS;
}

VOID
EFIAPI
DwcSdhciSetIoMux (
  VOID
  )
{
  SdhciEmmcIoMux ();
}
