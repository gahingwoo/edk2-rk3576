/** @file
 *
 *  RkSdmmcDxe platform helper library — RK3576 variant.
 *
 *  Differences from the RK3588 version:
 *    - RK3576 has no SCMI clock provider (no SCP/MHU mailbox).  The
 *      SD card clock is therefore programmed by writing the relevant
 *      CLKSEL/CLKGATE registers directly through the CRU.
 *    - The "force_jtag" workaround (RK3588 SYS_GRF_SOC_CON6) does
 *      not apply: ROCK 4D's SD slot is not muxed with JTAG.
 *    - Card-detect uses the DW MMC CDETECT hardware register (offset 0x050,
 *      bit0 = cdetect_n) via GPIO0 PA7 configured as function 1 (sdmmc0_detn)
 *      by SdmmcIoMux(). The vendor and mainline Linux DTS for ROCK 4D have no
 *      cd-gpios at all — they also rely on the hardware CDETECT path.
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/RkSdmmcPlatformLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/IoLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

/*
 * RK3576 SDMMC0 clock control.
 *
 *   CCLK_SRC_SDMMC0, CRU_CLKSEL_CON(105):
 *     bits[14:13] mux : 00 = gpll, 01 = cpll, 10 = xin24m
 *     bits[12:7]  div : divide by (div + 1)
 *   Gate : CRU_CLKGATE_CON(43), bit 1 (handled by SdmmcIoMux)
 *
 * This is mainline Linux's description (drivers/clk/rockchip/clk-rk3576.c:
 * COMPOSITE(CCLK_SRC_SDMMC0, ..., gpll_cpll_24m_p, RK3576_CLKSEL_CON(105),
 * 13, 2, MFLAGS, 7, 6, DFLAGS)).  The field is one bit lower than the eMMC's
 * in CLKSEL_CON(89), and the parents are the full GPLL (1188 MHz) and CPLL
 * (1000 MHz) outputs.
 *
 * This file used to write the eMMC's layout (mux at [15:14], divider at
 * [13:8]) and to assume 350 MHz and 400 MHz parents.  The driver's one
 * request, 52 MHz, was written as mux 0, divider field 0x600: in the real
 * layout that is GPLL with a divider of 13 or 14 depending on what bit 7
 * held, 85 or 91 MHz where the DW MMC driver believed it had 52.  Bit 15,
 * which belongs to no SDMMC0 field, was cleared as a side effect.
 *
 * The parent rates are now read from the PLL registers and the parent is
 * chosen as for the eMMC (DwcSdhciPlatformLib): the oscillator if it divides
 * the rate exactly, else CPLL if it does, else GPLL, with the divider rounded
 * up so the result is never above the request.  52 MHz becomes
 * GPLL / 23 = 51.65 MHz.
 */
#define RK3576_CLKSEL_SDMMC0      CRU_CLKSEL_CON(105)
#define SDMMC0_SEL_GPLL           0
#define SDMMC0_SEL_CPLL           1
#define SDMMC0_SEL_XIN_24M        2
#define SDMMC0_SEL_SHIFT          13
#define SDMMC0_SEL_MASK           0x3U
#define SDMMC0_DIV_SHIFT          7
#define SDMMC0_DIV_MASK           0x3FU

#define RK3576_OSC_HZ             24000000U

/*
 * PLL registers (RK3576 TRM V1.2 Part 1, CRU); the same reading as in
 * DwcSdhciPlatformLib.
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

/* SDMMC0 (mmc@2a310000) base address and DW MMC CDETECT register offset.
 * CDETECT bit 0 = cdetect_n: 0 → card present, 1 → no card. */
#define SDMMC0_BASE               0x2A310000UL
#define DW_MMC_CDETECT_OFF        0x050U

EFI_STATUS
EFIAPI
RkSdmmcSetClockRate (
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
    Sel    = SDMMC0_SEL_XIN_24M;
    Parent = RK3576_OSC_HZ;
  } else if ((Frequency <= Cpll) && ((Cpll % Frequency) == 0)) {
    Sel    = SDMMC0_SEL_CPLL;
    Parent = Cpll;
  } else if (Frequency <= RK3576_OSC_HZ) {
    Sel    = SDMMC0_SEL_XIN_24M;
    Parent = RK3576_OSC_HZ;
  } else {
    Sel    = SDMMC0_SEL_GPLL;
    Parent = Gpll;
  }

  Div = (UINT32)((Parent + Frequency - 1) / Frequency);
  if (Div < 1)  { Div = 1; }
  if (Div > 64) { Div = 64; }

  /* bits[14:13] = mux, bits[12:7] = divider-1, bits[31:16] = write enable. */
  MmioWrite32 (
    RK3576_CLKSEL_SDMMC0,
    (((SDMMC0_SEL_MASK << SDMMC0_SEL_SHIFT) | (SDMMC0_DIV_MASK << SDMMC0_DIV_SHIFT)) << 16) |
    (Sel << SDMMC0_SEL_SHIFT) |
    ((Div - 1) << SDMMC0_DIV_SHIFT)
    );

  DEBUG ((
    DEBUG_INFO,
    "RkSdmmcSetClockRate: req=%lu Hz -> mux=%u parent=%u Hz / %u = %u Hz (GPLL %u CPLL %u)\n",
    (UINT64)Frequency, Sel, Parent, Div, Parent / Div, Gpll, Cpll
    ));

  return EFI_SUCCESS;
}

VOID
EFIAPI
RkSdmmcSetIoMux (
  VOID
  )
{
  /*
   * SdmmcIoMux() already configures GPIO0 PA7 as function 1 (sdmmc0_detn),
   * connecting the hardware card-detect signal to DW_MMC_CDETECT.
   * No separate GPIO setup is needed.
   */
  SdmmcIoMux ();
}

RKSDMMC_CARD_PRESENCE_STATE
EFIAPI
RkSdmmcGetCardPresenceState (
  VOID
  )
{
  UINT32  Cdetect;

  /*
   * Read hardware CDETECT register via DW MMC controller.
   * GPIO0 PA7 (sdmmc0_detn, function 1, set by SdmmcIoMux) feeds
   * directly into this register.
   * bit 0 = cdetect_n: 0 -> card present, 1 -> no card.
   */
  Cdetect = MmioRead32 (SDMMC0_BASE + DW_MMC_CDETECT_OFF);
  return (Cdetect & BIT0) ? RkSdmmcCardNotPresent : RkSdmmcCardPresent;
}
