/** @file
 *
 *  DwcSdhciDxe platform helper library — RK3576 variant.
 *
 *  Differs from the RK3588 version only in the CRU base address
 *  (0x27200000) and the eMMC clock-select register offset.
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DwcSdhciPlatformLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

/*
 * RK3576 eMMC clock control.
 *
 *   CRU_CLKSEL_CON89 (CRU + 0x464), bits [15:8] (RK3576 TRM V1.2 Part 1):
 *     bits[15:14] cclk_src_emmc_sel : 00 = clk_gpll_mux  01 = clk_cpll_mux
 *                                     10 = xin_osc0_func
 *     bits[13:8]  cclk_src_emmc_div : divide by (div_con + 1)
 *   The gate (CRU_CLKGATE_CON(33), bit 8) is opened by SdhciEmmcIoMux.
 *
 * The parents are the full GPLL and CPLL outputs, not fixed 400 MHz taps.
 * Mainline Linux lists them as { "gpll", "cpll", "xin24m" }
 * (drivers/clk/rockchip/clk-rk3576.c, CCLK_SRC_EMMC), and rk3576.dtsi
 * assigns GPLL = 1188 MHz and CPLL = 1000 MHz, which is also what U-Boot
 * SPL programs (GPLL_HZ and CPLL_HZ in cru_rk3576.h, rk3576_clk_init()).
 * This file used to assume a 400 MHz parent on mux 0, so a 52 MHz request
 * was written as GPLL / 8: 148.5 MHz on a bus in HighSpeed timing, whose
 * limit is 52 MHz.
 *
 * The parent rate is now read back from the PLL registers instead of being
 * assumed, and the parent is chosen as rk3576_mmc_set_clk() in U-Boot
 * (drivers/clk/rockchip/clk_rk3576.c) chooses it: the oscillator if it
 * divides the rate exactly, else CPLL if it does, else GPLL.  The divider
 * rounds up, so the card never sees more than it asked for, as with
 * clk_set_rate() in Linux.  52 MHz becomes GPLL / 23 = 51.65 MHz, the rate
 * Linux and U-Boot both use.
 */
#define RK3576_CLKSEL_EMMC        CRU_CLKSEL_CON(89)
#define EMMC_SEL_GPLL             0
#define EMMC_SEL_CPLL             1
#define EMMC_SEL_XIN_24M          2

#define RK3576_OSC_HZ             24000000U

/*
 * PLL registers (RK3576 TRM V1.2 Part 1, CRU):
 *   CRU_CPLL_CON0 0x1A0, CRU_GPLL_CON0 0x1C0
 *     CON0 bit15 bypass, bits[9:0] m
 *     CON1 bits[8:6] s, bits[5:0] p
 *     CON2 bits[15:0] k (two's complement DSM fraction)
 *   CRU_MODE_CON00 0x280: bits[9:8] cpll mode, bits[3:2] gpll mode,
 *     01 = PLL output, 00 = oscillator.
 *   Fout = 24 MHz * (m + k / 65536) / (p * 2^s)
 */
#define RK3576_CPLL_CON0          (CRU_BASE + 0x1A0)
#define RK3576_GPLL_CON0          (CRU_BASE + 0x1C0)
#define RK3576_MODE_CON00         (CRU_BASE + 0x280)
#define RK3576_CPLL_MODE_SHIFT    8
#define RK3576_GPLL_MODE_SHIFT    2

STATIC
UINT32
Rk3576PllRate (
  IN UINTN  Con0,
  IN UINTN  ModeShift
  )
{
  UINT32  Mode;
  UINT32  Con0Val;
  UINT32  Con1Val;
  UINT32  M;
  UINT32  P;
  UINT32  S;
  INT16   K;
  UINT64  Rate;

  Mode = (MmioRead32 (RK3576_MODE_CON00) >> ModeShift) & 0x3;
  if (Mode != 1) {
    return RK3576_OSC_HZ;
  }

  Con0Val = MmioRead32 (Con0);
  Con1Val = MmioRead32 (Con0 + 4);
  K       = (INT16)(MmioRead32 (Con0 + 8) & 0xFFFF);

  if ((Con0Val & BIT15) != 0) {
    return RK3576_OSC_HZ;
  }

  M = Con0Val & 0x3FF;
  P = Con1Val & 0x3F;
  S = (Con1Val >> 6) & 0x7;
  if ((M == 0) || (P == 0)) {
    return RK3576_OSC_HZ;
  }

  Rate = MultU64x32 ((UINT64)RK3576_OSC_HZ, M);
  if (K != 0) {
    Rate += DivS64x64Remainder (MultS64x64 ((INT64)RK3576_OSC_HZ, K), 65536, NULL);
  }

  return (UINT32)DivU64x32 (Rate, P << S);
}

EFI_STATUS
EFIAPI
DwcSdhciSetClockRate (
  IN UINTN  Frequency
  )
{
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Div;
  UINT32  Cpll;
  UINT32  Gpll;

  if (Frequency == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Cpll = Rk3576PllRate (RK3576_CPLL_CON0, RK3576_CPLL_MODE_SHIFT);
  Gpll = Rk3576PllRate (RK3576_GPLL_CON0, RK3576_GPLL_MODE_SHIFT);

  if ((Frequency <= RK3576_OSC_HZ) && ((RK3576_OSC_HZ % Frequency) == 0)) {
    Sel    = EMMC_SEL_XIN_24M;
    Parent = RK3576_OSC_HZ;
  } else if ((Frequency <= Cpll) && ((Cpll % Frequency) == 0)) {
    Sel    = EMMC_SEL_CPLL;
    Parent = Cpll;
  } else if (Frequency <= RK3576_OSC_HZ) {
    Sel    = EMMC_SEL_XIN_24M;
    Parent = RK3576_OSC_HZ;
  } else {
    Sel    = EMMC_SEL_GPLL;
    Parent = Gpll;
  }

  Div = (UINT32)((Parent + Frequency - 1) / Frequency);
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
    "DwcSdhciSetClockRate: req=%lu Hz -> mux=%u parent=%u Hz / %u = %u Hz (GPLL %u CPLL %u)\n",
    (UINT64)Frequency, Sel, Parent, Div, Parent / Div, Gpll, Cpll
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
