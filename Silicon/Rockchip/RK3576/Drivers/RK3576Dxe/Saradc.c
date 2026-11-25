/** @file
 *
 *  RK3576 SARADC clock for an ACPI OS (PcdRk3576SaradcClockInit).
 *
 *  The woa-rk3576 rk3xsaradc driver reads the SARADC's own registers only;
 *  it has no clock framework.  This sets the converter clock the way Linux
 *  does (rockchip_saradc.c clk_rate 1000000) and makes sure both gates are
 *  open, so that the block the DSDT describes (Saradc.asl) is usable:
 *
 *    CRU_CLKSEL_CON58 (0x27200000 + 0x3E8), TRM V1.2 Part 1 and mainline
 *    clk-rk3576.c COMPOSITE(CLK_SARADC, gpll_24m_p, CLKSEL_CON(58), 12, 1,
 *    4, 8):
 *      [12]   clk_saradc_sel   0 clk_gpll_mux, 1 xin_osc0_func
 *      [11:4] clk_saradc_div   divide by (div_con + 1)
 *      reset value 0x3B0: GPLL / 60, 19.8 MHz with GPLL at 1188 MHz.
 *    Written here: xin24m / 24 = 1 MHz, independent of GPLL.
 *
 *    CRU_GATE_CON13 (0x27200000 + 0x834): bit 6 pclk_saradc_en, bit 7
 *    clk_saradc_en, "when high, disable clock", reset value 0 (open).
 *    Cleared again here only if something closed them.
 *
 *  Both registers take a write-enable mask in [31:16], so only the named
 *  bits move.  The SARADC resets (SOFTRST_CON13 bits 6 and 7) are not
 *  touched: RockchipIpRegs.h's SRST_P_SARADC (190) is RK3588's and on
 *  RK3576 decodes to hresetn_bus_biu (see Saradc.asl, and the history of
 *  PlatformLib/Rk3576.c).  The reset the OS driver needs is in AML
 *  (\_SB.ADC0.RSTC), against the right bit.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>

#include "../../Include/Soc.h"
#include "Saradc.h"

#define SARADC_CLKSEL_CON        58
#define CLK_SARADC_SEL_XIN24M    BIT12
#define CLK_SARADC_DIV_SHIFT     4
#define CLK_SARADC_DIV_MASK      (0xFFU << CLK_SARADC_DIV_SHIFT)
#define CLK_SARADC_DIV_1MHZ      23U                 /* 24 MHz / (23 + 1) */

#define SARADC_CLKGATE_CON       13
#define PCLK_SARADC_GATE_BIT     6
#define CLK_SARADC_GATE_BIT      7
#define SARADC_GATE_BITS         ((1U << PCLK_SARADC_GATE_BIT) | (1U << CLK_SARADC_GATE_BIT))

VOID
RK3576SaradcClockInit (
  VOID
  )
{
  UINT32  SelMask;
  UINT32  Sel;
  UINT32  Gate;

  if (!FixedPcdGetBool (PcdRk3576SaradcClockInit)) {
    return;
  }

  Gate = MmioRead32 (CRU_CLKGATE_CON (SARADC_CLKGATE_CON));
  if ((Gate & SARADC_GATE_BITS) != 0) {
    MmioWrite32 (CRU_CLKGATE_CON (SARADC_CLKGATE_CON), SARADC_GATE_BITS << 16);
  }

  SelMask = CLK_SARADC_SEL_XIN24M | CLK_SARADC_DIV_MASK;
  Sel     = CLK_SARADC_SEL_XIN24M | (CLK_SARADC_DIV_1MHZ << CLK_SARADC_DIV_SHIFT);
  MmioWrite32 (CRU_CLKSEL_CON (SARADC_CLKSEL_CON), (SelMask << 16) | Sel);

  DEBUG ((
    DEBUG_INFO,
    "RK3576Dxe: SARADC clock xin24m/24 (CLKSEL_CON58 0x%08x, was gates 0x%x)\n",
    MmioRead32 (CRU_CLKSEL_CON (SARADC_CLKSEL_CON)),
    Gate & SARADC_GATE_BITS
    ));
}
