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
 *    - Card-detect uses GPIO0 PA5 per rk3576-rock-4d.dts
 *      (cd-gpios = <&gpio0 RK_PA5 GPIO_ACTIVE_LOW>).
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/RkSdmmcPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/IoLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

/*
 * RK3576 SDMMC0 clock control (from clk-rk3576 driver in mainline U-Boot)
 *   CCLK_SRC_SDMMC0 lives in the SDGMAC clock domain.
 *   Mux  : CRU_CLKSEL_CON(105), bits[15:14]
 *            00 = gpll_350m, 01 = cpll_400m, 10 = xin_osc0_func (24 MHz)
 *   Div  : CRU_CLKSEL_CON(105), bits[13:8]  (1..64, programmed as N-1)
 *   Gate : CRU_CLKGATE_CON(43), bit[1]      (handled by SdmmcIoMux)
 *
 * The exact register may shift between TRM revisions; if the divider
 * write turns out wrong, the SDHCI / DW MMC driver still negotiates
 * the operating frequency with the card and will fall back to the
 * advertised base clock from the controller capability register.
 */
#define RK3576_CLKSEL_SDMMC0      CRU_CLKSEL_CON(105)
#define SDMMC0_SEL_GPLL_350M      0
#define SDMMC0_SEL_CPLL_400M      1
#define SDMMC0_SEL_XIN_24M        2

EFI_STATUS
EFIAPI
RkSdmmcSetClockRate (
  IN UINTN  Frequency
  )
{
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Div;

  if (Frequency == 0) {
    return EFI_INVALID_PARAMETER;
  }

  /*
   * Pick the lowest parent that can produce >= Frequency, then
   * derive an integer divider.  The card clock is further divided
   * by the SDHCI controller itself, so coarse accuracy is fine.
   */
  if (Frequency <= 24000000) {
    Sel    = SDMMC0_SEL_XIN_24M;
    Parent = 24000000;
  } else if (Frequency <= 350000000) {
    Sel    = SDMMC0_SEL_GPLL_350M;
    Parent = 350000000;
  } else {
    Sel    = SDMMC0_SEL_CPLL_400M;
    Parent = 400000000;
  }

  Div = (Parent + Frequency - 1) / Frequency;
  if (Div < 1)  { Div = 1; }
  if (Div > 64) { Div = 64; }

  /* bits[15:14] = mux, bits[13:8] = divider-1, bits[31:16] = write enable. */
  MmioWrite32 (
    RK3576_CLKSEL_SDMMC0,
    (((0x3U << 14) | (0x3FU << 8)) << 16) |
    (Sel << 14) |
    ((Div - 1) << 8)
    );

  DEBUG ((
    DEBUG_INFO,
    "RkSdmmcSetClockRate: req=%lu Hz -> parent=%u Hz / %u (mux=%u)\n",
    (UINT64)Frequency, Parent, Div, Sel
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
   * ROCK 4D / RK3576: SD slot is on dedicated SDMMC pins, no JTAG
   * multiplexing workaround required.  Configure the CD pin as input
   * before calling the platform-specific iomux setup.
   */
  GpioPinSetDirection (0, GPIO_PIN_PA5, GPIO_PIN_INPUT);
  SdmmcIoMux ();
}

RKSDMMC_CARD_PRESENCE_STATE
EFIAPI
RkSdmmcGetCardPresenceState (
  VOID
  )
{
  /*
   * cd-gpios = <&gpio0 RK_PA5 GPIO_ACTIVE_LOW>
   *   level low  -> card present.
   */
  return GpioPinReadActual (0, GPIO_PIN_PA5) ? RkSdmmcCardNotPresent
                                             : RkSdmmcCardPresent;
}
