/** @file
 *
 *  RK3576 PWM ("PWM v4") output library.
 *
 *  Sources, cited below as [V], [M], [D], [C]:
 *
 *    [V] LCKFB/Rockchip vendor kernel, ~/Desktop/linux-rockchip-armbian,
 *        drivers/pwm/pwm-rockchip.c (pwm_data_v4_rk3576, the *_v4 paths).
 *    [M] The mainline-bound PWMv4 driver Armbian carries, Armbian build tree
 *        branch lckfb-taishanpi-3m, patch/kernel/archive/rockchip64-7.3/
 *        rk3576-pwm-v5-with-fix-for-v7.2.patch (line numbers are of that
 *        patch file).  Its register comments are taken from the RK3576 TRM.
 *    [D] Vendor kernel arch/arm64/boot/dts/rockchip/rk3576.dtsi.
 *    [C] Vendor kernel drivers/clk/rockchip/clk-rk3576.c and clk.h.
 *
 *  The two drivers agree on every register and bit used here.  Where their
 *  write sequences differ, the mainline-bound one is followed, because that is
 *  the driver the board's Armbian image runs the fan with.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rk3576PwmLib.h>
#include <Soc.h>

/*
 * PWM2 channel n lives at 0x2ADE0000 + n * 0x1000, one 4 KiB instance per
 * channel: [D] pwm2_8ch_0: pwm@2ade0000 (rk3576.dtsi:6144) through
 * pwm2_8ch_7: pwm@2ade7000 (:6242); [M]:2110-2112 has the same addresses.
 */
#define PWM2_BASE           0x2ADE0000UL
#define PWM2_CHANNEL_COUNT  8
#define PWM_CHANNEL_STRIDE  0x1000UL

/*
 * PWM2 clocks, all in the main CRU at 0x27200000 ([D] cru: clock-controller@
 * 27200000, rk3576.dtsi:2265).  CLKSEL_CON(x) = 0x300 + 4x and
 * CLKGATE_CON(x) = 0x800 + 4x ([C] clk.h:546-547), as Soc.h defines them.
 *
 *   PCLK_PWM2  gate CLKGATE_CON(20) bit 4                  [C] clk-rk3576.c:825-826
 *   CLK_PWM2   mux  CLKSEL_CON(74) bits [7:6], parents
 *              { clk_cpll_div10, clk_cpll_div20, xin24m }  [C] :827-829, :318
 *              gate CLKGATE_CON(20) bit 5
 *
 * The mux is set to xin24m (2) so the counter rate is 24 MHz exactly and does
 * not depend on what the SPL left CPLL at.  Both kernels read the rate back
 * from the CRU, so this is not hidden from them.  Rockchip CRU registers take
 * a write mask in the upper 16 bits, and a gate bit of 1 means gated.
 */
#define PWM2_CLKSEL_CON      74
#define PWM2_CLK_MUX_SHIFT   6
#define PWM2_CLK_MUX_XIN24M  2U
#define PWM2_CLKGATE_CON     20
#define PCLK_PWM2_GATE_BIT   4
#define CLK_PWM2_GATE_BIT    5
#define PWM2_CLK_HZ          24000000ULL

/*
 * Per-channel registers.  All of them take a write mask in bits [31:16]
 * ([M]:637-641, "Set bit n here to 1 if you wish to modify bit n >> 16"),
 * except PERIOD, DUTY and OFFSET, which are plain 32-bit values.
 */

/* VERSION: [31:24] major version, [7:4] channel index.  [M]:619-634, [V]:87-103 */
#define PWM_VERSION                0x000
#define PWM_VERSION_MAJOR(v)       (((v) >> 24) & 0xFF)
#define PWM_VERSION_CHANNEL(v)     (((v) >> 4) & 0xF)
/* [V] pwm_data_v4_rk3576.main_version = 0x04 (pwm-rockchip.c:2410) */
#define PWM_VERSION_MAJOR_V4       0x04

/*
 * ENABLE: bit 0 PWM_CLK_EN, bit 1 PWM_EN, bit 2 CTRL_UPDATE_EN (write 1 to
 * commit CTRL/PERIOD/DUTY/OFFSET).  [M]:636-678, [V]:104-109
 */
#define PWM_ENABLE                 0x004
#define PWM_ENABLE_CLK_EN          BIT0
#define PWM_ENABLE_EN              BIT1
#define PWM_ENABLE_CTRL_UPDATE     BIT2

/*
 * CLK_CTRL: [15] global clock select, [14:13] source (0 = the CRU clock,
 * here CLK_PWM2), [12:4] scale, [2:0] prescale (divide by 2^v); bit 3 is
 * reserved.  [M]:684-721, [V]:111-119.  Both drivers compute the period from
 * the source clock with no scaling, i.e. they assume all of these are 0.
 */
#define PWM_CLK_CTRL               0x008
#define PWM_CLK_CTRL_WRITABLE      (0xFFF7U)

/*
 * CTRL: [1:0] mode (1 = continuous), bit 2 duty polarity (1 = positive),
 * bit 3 inactive polarity (1 = positive), bit 4 output mode (0 = left
 * aligned), bit 5 "aligned mode invalid".  [M]:723-768, [V]:120-131.
 * PWM_POLARITY_NORMAL is duty positive / inactive negative in both drivers
 * ([V] rockchip_pwm_config_v4, pwm-rockchip.c:845-852; [M]:769-772).
 * [M] sets bit 5 in continuous mode (PWMV4_CTRL_COM_FLAGS, :769-772); [V]
 * clears it (:887); for a left-aligned continuous output it should make
 * no difference, and [M] is what runs on this board under Linux.
 */
#define PWM_CTRL                   0x00C
#define PWM_CTRL_MODE_CONTINUOUS   0x1U
#define PWM_CTRL_DUTY_POSITIVE     BIT2
#define PWM_CTRL_INACTIVE_POSITIVE BIT3
#define PWM_CTRL_UNALIGNED         BIT5
#define PWM_CTRL_WRITABLE          (0x3FU)

/* [M]:778-797, [V]:134-138 */
#define PWM_PERIOD                 0x010
#define PWM_DUTY                   0x014
#define PWM_OFFSET                 0x018

#define HIWORD(Mask, Value)        (((UINT32)(Mask) << 16) | ((UINT32)(Value) & (Mask)))

EFI_STATUS
EFIAPI
Rk3576PwmStart (
  IN UINT32   Controller,
  IN UINT32   Channel,
  IN UINT32   PeriodNs,
  IN UINT32   DutyNs,
  IN BOOLEAN  Inverted
  )
{
  UINTN   Base;
  UINT32  Version;
  UINT64  PeriodCycles;
  UINT64  DutyCycles;
  UINT32  Ctrl;

  if ((Controller != 2) || (Channel >= PWM2_CHANNEL_COUNT)) {
    DEBUG ((DEBUG_ERROR, "Rk3576PwmStart: PWM%u channel %u is not implemented\n", Controller, Channel));
    return EFI_UNSUPPORTED;
  }

  if ((PeriodNs == 0) || (DutyNs > PeriodNs)) {
    return EFI_INVALID_PARAMETER;
  }

  PeriodCycles = (PWM2_CLK_HZ * PeriodNs + 500000000ULL) / 1000000000ULL;
  DutyCycles   = (PWM2_CLK_HZ * DutyNs + 500000000ULL) / 1000000000ULL;
  if ((PeriodCycles == 0) || (PeriodCycles > MAX_UINT32)) {
    return EFI_INVALID_PARAMETER;
  }

  Base = PWM2_BASE + Channel * PWM_CHANNEL_STRIDE;

  /* CLK_PWM2 from xin24m, then open PCLK_PWM2 and CLK_PWM2. */
  MmioWrite32 (
    CRU_CLKSEL_CON (PWM2_CLKSEL_CON),
    HIWORD (0x3U << PWM2_CLK_MUX_SHIFT, PWM2_CLK_MUX_XIN24M << PWM2_CLK_MUX_SHIFT)
    );
  MmioWrite32 (
    CRU_CLKGATE_CON (PWM2_CLKGATE_CON),
    HIWORD ((1U << PCLK_PWM2_GATE_BIT) | (1U << CLK_PWM2_GATE_BIT), 0)
    );

  /*
   * Read only after PCLK_PWM2 is open.  A wrong base would show up here as a
   * major version other than 4 or a channel index other than ours.
   */
  Version = MmioRead32 (Base + PWM_VERSION);
  DEBUG ((
    DEBUG_INFO,
    "Rk3576PwmStart: PWM2 ch%u @ 0x%lx VERSION 0x%08x (major %u, channel %u)\n",
    Channel,
    (UINT64)Base,
    Version,
    PWM_VERSION_MAJOR (Version),
    PWM_VERSION_CHANNEL (Version)
    ));
  if ((PWM_VERSION_MAJOR (Version) != PWM_VERSION_MAJOR_V4) ||
      (PWM_VERSION_CHANNEL (Version) != Channel))
  {
    DEBUG ((DEBUG_ERROR, "Rk3576PwmStart: unexpected VERSION, not touching the channel\n"));
    return EFI_UNSUPPORTED;
  }

  /* Counter clock = CLK_PWM2, unscaled. */
  MmioWrite32 (Base + PWM_CLK_CTRL, HIWORD (PWM_CLK_CTRL_WRITABLE, 0));

  /*
   * Order from [M] rockchip_pwm_v4_write_wf (:1290-1333), which quotes RK3576
   * TRM Part 1 section 34.6.3: enable first, then the parameters, then
   * CTRL_UPDATE_EN to commit them.
   */
  MmioWrite32 (
    Base + PWM_ENABLE,
    HIWORD (PWM_ENABLE_EN | PWM_ENABLE_CLK_EN, PWM_ENABLE_EN | PWM_ENABLE_CLK_EN)
    );
  MmioWrite32 (Base + PWM_PERIOD, (UINT32)PeriodCycles);
  MmioWrite32 (Base + PWM_DUTY, (UINT32)DutyCycles);
  MmioWrite32 (Base + PWM_OFFSET, 0);

  Ctrl = PWM_CTRL_MODE_CONTINUOUS | PWM_CTRL_UNALIGNED;
  Ctrl |= Inverted ? PWM_CTRL_INACTIVE_POSITIVE : PWM_CTRL_DUTY_POSITIVE;
  MmioWrite32 (Base + PWM_CTRL, HIWORD (PWM_CTRL_WRITABLE, Ctrl));

  MmioWrite32 (Base + PWM_ENABLE, HIWORD (PWM_ENABLE_CTRL_UPDATE, PWM_ENABLE_CTRL_UPDATE));

  DEBUG ((
    DEBUG_INFO,
    "Rk3576PwmStart: PWM2 ch%u running, period %lu duty %lu cycles at 24 MHz%a\n",
    Channel,
    PeriodCycles,
    DutyCycles,
    Inverted ? ", inverted" : ""
    ));

  return EFI_SUCCESS;
}
