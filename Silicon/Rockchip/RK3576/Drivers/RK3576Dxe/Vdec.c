/** @file
 *
 *  Power up the video decoder (RKVDEC, VDPU383) for an OS driver that has no
 *  clock or power-domain framework (Windows).
 *
 *  Off by default.  Runs only when "Video Decoder (VDPU383)" is enabled in
 *  setup (PcdAcpiVdec) and the firmware hands the OS ACPI tables; with the
 *  option off nothing in here touches the hardware, and RK3576AcpiPlatformDxe
 *  does not install the decoder's SSDT (Vdec.asl) either.
 *
 *  Everything below is taken from mainline Linux:
 *
 *    rk3576.dtsi, vdec: video-codec@27b00000
 *      clocks          ACLK_RKVDEC_ROOT, HCLK_RKVDEC, ACLK_RKVDEC_ROOT_BAK,
 *                      CLK_RKVDEC_CORE, CLK_RKVDEC_HEVC_CA
 *      assigned rates  aclk_rkvdec_root 600 MHz, clk_rkvdec_core 600 MHz
 *                      (a gate on aclk_rkvdec_root), aclk_rkvdec_root_bak
 *                      500 MHz, clk_rkvdec_hevc_ca 1000 MHz
 *      resets          SRST_A_RKVDEC_BIU, SRST_H_RKVDEC_BIU, SRST_H_RKVDEC,
 *                      SRST_RKVDEC_CORE, SRST_RKVDEC_HEVC_CA
 *      power-domains   RK3576_PD_VDEC
 *
 *    clk-rk3576.c ("vdec")
 *      CLKSEL_CON(110) [1:0]  hclk_rkvdec_root mux (200/100/50/24 MHz)
 *                      [8:7]  aclk_rkvdec_root mux (gpll cpll aupll spll)
 *                      [6:2]  aclk_rkvdec_root div
 *                      [15:14] aclk_rkvdec_root_bak mux (cpll vpll lpll bpll)
 *                      [13:9] aclk_rkvdec_root_bak div
 *      CLKSEL_CON(111) [6:5]  clk_rkvdec_hevc_ca mux (gpll cpll lpll bpll)
 *                      [4:0]  clk_rkvdec_hevc_ca div
 *      CLKGATE_CON(45) bit 0 hclk_rkvdec_root, 1 aclk_rkvdec_root,
 *                      2 aclk_rkvdec_root_bak, 3 hclk_rkvdec,
 *                      8 clk_rkvdec_hevc_ca, 9 clk_rkvdec_core
 *
 *    rst-rk3576.c: SOFTRST_CON(45) bit 3 H_RKVDEC, 5 H_RKVDEC_BIU,
 *                  6 A_RKVDEC_BIU, 8 RKVDEC_HEVC_CA, 9 RKVDEC_CORE
 *
 *    pm-domains.c, rk3576_pm_domains[RK3576_PD_VDEC]:
 *      DOMAIN_RK3576("vdec", 0x4, BIT(4), 0, BIT(20), 0x0, BIT(6), BIT(6),
 *                    BIT(6), 0, false)
 *      i.e. PWR_CON1 (0x214) bit 4, repair/memory status bit 20, bus idle
 *      request/ack/idle bit 6 in REQ0/ACK0/IDLE0, clock ungate bit 6.
 *      Not a child of any other domain, and no regulator: it is on the
 *      always-on logic supply.
 *
 *  The sequence is the one Gpu.c runs for PD_GPU, which has been measured on
 *  hardware; only the bits differ.  THIS FILE HAS NOT BEEN RUN ON HARDWARE.
 *
 *  The divider choice is clk_set_rate()'s: the parent the DT names, divided
 *  down to the highest rate that does not exceed the one asked for.  With
 *  the SPL's GPLL = 1188 MHz and CPLL = 1000 MHz that is 594, 500 and 1000
 *  MHz, the rates Linux reports in clk_summary for the same DT.  The PLLs are
 *  read back, not assumed.
 *
 *  hclk_rkvdec_root is left at its reset mux: mainline does not assign it.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include "Vdec.h"

#define PMU_BASE          0x27380000UL
#define PMU_REQ0          (PMU_BASE + 0x110)  /* bit 6: VDEC bus idle request */
#define PMU_ACK0          (PMU_BASE + 0x120)
#define PMU_IDLE0         (PMU_BASE + 0x128)
#define PMU_CLK_UNGATE    (PMU_BASE + 0x140)  /* bit 6: held open during the transition */
#define PMU_PWR_CON1      (PMU_BASE + 0x214)  /* bit 4: PD_VDEC off */
#define PMU_STATUS0       (PMU_BASE + 0x230)  /* bit 20: PD_VDEC off */
#define PMU_REPAIR0       (PMU_BASE + 0x570)  /* bit 20: PD_VDEC on (memory repair done) */

#define VDEC_PWR_BIT      BIT4
#define VDEC_STATUS_BIT   BIT20
#define VDEC_IDLE_BIT     BIT6

#define CRU_BASE          0x27200000UL
#define CRU_CLKSEL110     (CRU_BASE + 0x300 + 110 * 4)
#define CRU_CLKSEL111     (CRU_BASE + 0x300 + 111 * 4)
#define CRU_GATE45        (CRU_BASE + 0x800 + 45 * 4)
#define CRU_SOFTRST45     (CRU_BASE + 0xA00 + 45 * 4)

#define VDEC_GATE_MASK    (BIT0 | BIT1 | BIT2 | BIT3 | BIT8 | BIT9)
#define VDEC_RESET_MASK   (BIT3 | BIT5 | BIT6 | BIT8 | BIT9)

/* PLL registers, as DwcSdhciPlatformLib.c reads them. */
#define RK3576_CPLL_CON0        (CRU_BASE + 0x1A0)
#define RK3576_GPLL_CON0        (CRU_BASE + 0x1C0)
#define RK3576_MODE_CON00       (CRU_BASE + 0x280)
#define RK3576_CPLL_MODE_SHIFT  8
#define RK3576_GPLL_MODE_SHIFT  2
#define RK3576_OSC_HZ           24000000U

/* RKVDEC block: link registers at +0x000, function (decoder) at +0x100. */
#define VDEC_BASE         0x27B00000UL
#define VDEC_REG_VERSION  (VDEC_BASE + 0x100)  /* MPP's Vdpu383RegVersion: reg 0 */
#define VDEC_MMU0_STATUS  (VDEC_BASE + 0x800 + 0x04)
#define VDEC_MMU1_STATUS  (VDEC_BASE + 0x900 + 0x04)

#define ACLK_ROOT_HZ      600000000U
#define ACLK_ROOT_BAK_HZ  500000000U
#define HEVC_CA_HZ        1000000000U

STATIC
UINT32
VdecPllRate (
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

/*
 * The smallest divider (1..32) that brings Parent to Target or below:
 * clk_set_rate() rounding.  Returns the field value, divider - 1.
 */
STATIC
UINT32
VdecDivField (
  IN UINT32  Parent,
  IN UINT32  Target
  )
{
  UINT32  Div;

  Div = (Parent + Target - 1) / Target;
  if (Div == 0) {
    Div = 1;
  }

  if (Div > 32) {
    Div = 32;
  }

  return Div - 1;
}

STATIC
BOOLEAN
PollBits (
  IN UINTN   Address,
  IN UINT32  Mask,
  IN UINT32  Want
  )
{
  UINTN  Retry;

  for (Retry = 0; Retry < 1000; Retry++) {
    if ((MmioRead32 (Address) & Mask) == Want) {
      return TRUE;
    }

    MicroSecondDelay (100);
  }

  return FALSE;
}

VOID
RK3576VdecPowerOn (
  VOID
  )
{
  UINT32  Gpll;
  UINT32  Cpll;
  UINT32  AclkDiv;
  UINT32  BakDiv;
  UINT32  CaDiv;

  Gpll = VdecPllRate (RK3576_GPLL_CON0, RK3576_GPLL_MODE_SHIFT);
  Cpll = VdecPllRate (RK3576_CPLL_CON0, RK3576_CPLL_MODE_SHIFT);

  AclkDiv = VdecDivField (Gpll, ACLK_ROOT_HZ);     /* mux 0: gpll */
  BakDiv  = VdecDivField (Cpll, ACLK_ROOT_BAK_HZ); /* mux 0: cpll */
  CaDiv   = VdecDivField (Cpll, HEVC_CA_HZ);       /* mux 1: cpll */

  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: VDEC: gpll %u Hz, cpll %u Hz -> aclk %u, aclk_bak %u, hevc_ca %u Hz\n",
    Gpll,
    Cpll,
    Gpll / (AclkDiv + 1),
    Cpll / (BakDiv + 1),
    Cpll / (CaDiv + 1)
    ));

  /*
   * Rates first, before the gates are opened: [15:2] of CLKSEL_CON110 and
   * [6:0] of CLKSEL_CON111.  [1:0] of CON110, hclk_rkvdec_root, is not
   * written.
   */
  MmioWrite32 (CRU_CLKSEL110, (0xFFFCU << 16) | (BakDiv << 9) | (AclkDiv << 2));
  MmioWrite32 (CRU_CLKSEL111, (0x7FU << 16) | (1U << 5) | CaDiv);

  /* Out of reset (the reset default, written for certainty), then ungated. */
  MmioWrite32 (CRU_SOFTRST45, (VDEC_RESET_MASK << 16) | 0);
  MmioWrite32 (CRU_GATE45, (VDEC_GATE_MASK << 16) | 0);

  MmioWrite32 (PMU_CLK_UNGATE, (VDEC_IDLE_BIT << 16) | VDEC_IDLE_BIT);

  if ((MmioRead32 (PMU_REPAIR0) & VDEC_STATUS_BIT) == 0) {
    MmioWrite32 (PMU_PWR_CON1, (VDEC_PWR_BIT << 16) | 0);
    if (!PollBits (PMU_STATUS0, VDEC_STATUS_BIT, 0) ||
        !PollBits (PMU_REPAIR0, VDEC_STATUS_BIT, VDEC_STATUS_BIT))
    {
      DEBUG ((
        DEBUG_ERROR,
        "RK3576Dxe: VDEC power domain did not come up (STATUS0=0x%08x REPAIR0=0x%08x)\n",
        MmioRead32 (PMU_STATUS0),
        MmioRead32 (PMU_REPAIR0)
        ));
      goto Out;
    }
  } else {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: VDEC power domain was already on\n"));
  }

  MmioWrite32 (PMU_REQ0, (VDEC_IDLE_BIT << 16) | 0);
  if (!PollBits (PMU_ACK0, VDEC_IDLE_BIT, 0) || !PollBits (PMU_IDLE0, VDEC_IDLE_BIT, 0)) {
    DEBUG ((
      DEBUG_ERROR,
      "RK3576Dxe: VDEC bus did not leave idle (ACK0=0x%08x IDLE0=0x%08x)\n",
      MmioRead32 (PMU_ACK0),
      MmioRead32 (PMU_IDLE0)
      ));
    goto Out;
  }

  /*
   * Safe to touch the block only now.  Reads only: the version register and
   * the two IOMMU status words (bit 0 = paging enabled; it should be 0, the
   * reset state, which lets the decoder's DMA through untranslated).
   */
  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: VDEC powered, version=0x%08x mmu status=0x%08x/0x%08x\n",
    MmioRead32 (VDEC_REG_VERSION),
    MmioRead32 (VDEC_MMU0_STATUS),
    MmioRead32 (VDEC_MMU1_STATUS)
    ));

Out:
  MmioWrite32 (PMU_CLK_UNGATE, (VDEC_IDLE_BIT << 16) | 0);
}
