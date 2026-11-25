/** @file
 *
 *  RK3576 non-secure watchdog (WDT_NS) as described by the WDAT
 *  (AcpiTables/Wdat.aslc) and prepared by RK3576AcpiPlatformDxe (Watchdog.c).
 *
 *  Register sources:
 *    WDT_NS          RK3576 TRM V1.2 Part 1, chapter 15 (base 0x2ACE0000,
 *                    registers 15.4); rk3576.dtsi watchdog@2ace0000.
 *    CRU gate/reset  CRU_GATE_CON16 (0x840) and CRU_SOFTRST_CON16 (0xA40),
 *                    bits 7 and 8: PCLK_WDT0 / TCLK_WDT0 and SRST_P_WDT0 /
 *                    SRST_T_WDT0 in mainline clk-rk3576.c and rst-rk3576.c,
 *                    and "cru_gate16[7:8]" for wdt0 in TF-A rk3576 pmu.c.
 *                    The TRM's bit names for these two ("pclk_wdtpmu",
 *                    "tclk_wdtpmu") are not the PMU watchdog's: that one is
 *                    in PMU1CRU (PCLK_PMU1WDT / TCLK_PMU1WDT).
 *    CRU_GLB_RST_CON 0xC10: [6] wdt_trig_glbrst_en, [11] wdt_trig_glbrst_sel
 *                    (1 = first global reset).  Not a write-masked register.
 *                    TF-A system_reset_init() writes 0xffdf.
 *    CRU_GLBRST_ST_NCLR 0xC14: [12] glbrst_wdtns_rst, "not clear by rst".
 *    SYS_GRF_SOC_CON4 0x2600A010: [8] wdtns_glb_reset_en, write-masked.
 *                    TF-A system_reset_init() sets it ("enable wdt_ns reset").
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_WDAT_H_
#define RK3576_WDAT_H_

#define RK3576_WDT_NS_BASE  0x2ACE0000

#define RK3576_WDT_CR    0x00
#define RK3576_WDT_TORR  0x04
#define RK3576_WDT_CCVR  0x08
#define RK3576_WDT_CRR   0x0C

#define RK3576_WDT_CR_EN        0x1
#define RK3576_WDT_CR_RMOD      0x2
#define RK3576_WDT_CR_PULSE_8   (0x2 << 2)    // the reset value of the field
#define RK3576_WDT_CR_RUN       (RK3576_WDT_CR_PULSE_8 | RK3576_WDT_CR_RMOD | RK3576_WDT_CR_EN)
#define RK3576_WDT_CRR_RESTART  0x76
#define RK3576_WDT_TOP          15            // 2^31 tclk cycles, 89.5 s at 24 MHz

#define RK3576_CRU_BASE              0x27200000
#define RK3576_CRU_GATE_CON16        (RK3576_CRU_BASE + 0x0840)
#define RK3576_CRU_SOFTRST_CON16     (RK3576_CRU_BASE + 0x0A40)
#define RK3576_CRU_GLB_RST_CON       (RK3576_CRU_BASE + 0x0C10)
#define RK3576_CRU_GLBRST_ST_NCLR    (RK3576_CRU_BASE + 0x0C14)

#define RK3576_WDT0_CLK_BITS         (0x3U << 7)
#define RK3576_WDT0_SRST_ASSERT      ((0x3U << (7 + 16)) | (0x3U << 7))
#define RK3576_WDT0_SRST_DEASSERT    (0x3U << (7 + 16))
#define RK3576_GLB_RST_CON_WDT_EN    (1U << 6)
#define RK3576_GLB_RST_CON_WDT_FIRST (1U << 11)
#define RK3576_GLBRST_NCLR_WDTNS     (1U << 12)

#define RK3576_SYS_GRF_SOC_CON4      0x2600A010
#define RK3576_SYS_GRF_WDTNS_GLB_EN  (1U << 8)

//
// Declared countdown: 1 s per count, 60..600 counts.  See Wdat.aslc for why
// the hardware period is fixed at about 179 s regardless.
//
#define RK3576_WDAT_TIMER_PERIOD_MS  1000
#define RK3576_WDAT_MIN_COUNT        60
#define RK3576_WDAT_MAX_COUNT        600

//
// Scratch words.  Wdat.aslc writes their addresses as
// RK3576_WDAT_SCRATCH_TAG + offset; RK3576AcpiPlatformDxe replaces the tag
// with the physical address of a page it reserves, before the table is
// installed.
//
#define RK3576_WDAT_SCRATCH_TAG        0xFFFF574400000000ULL   // "WD" marker
#define RK3576_WDAT_SCRATCH_TAG_MASK   0xFFFFFFFF00000000ULL
#define RK3576_WDAT_SCRATCH_STATUS     0x0   // bit 0: this boot was a WDT_NS reset
#define RK3576_WDAT_SCRATCH_COUNTDOWN  0x4   // the count Windows set
#define RK3576_WDAT_SCRATCH_REBOOT     0x8   // always 1
#define RK3576_WDAT_SCRATCH_SIZE       0xC

#endif // RK3576_WDAT_H_
