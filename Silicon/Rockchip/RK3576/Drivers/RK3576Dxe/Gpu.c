/** @file
 *
 *  Power up the Mali-G52 so an OS driver that has no clock or power-domain
 *  framework (Windows) finds it ready.
 *
 *  The sequence is the one proven from Linux userspace with no GPU driver
 *  loaded (github.com/gahingwoo/wddm-mali-g52, tools/m1/m1_raw.c), which in
 *  turn follows mainline rockchip_pd_power() and clk-rk3576.c. The supply,
 *  vdd_gpu_s0 (RK806 BUCK5), is on from power-up (regulator-boot-on); nothing
 *  in this firmware configures the RK806, so this code relies on that.
 *
 *  Linux is unaffected: genpd finds PD_GPU on and Panfrost takes it from there.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include "Gpu.h"

#define PMU_BASE          0x27380000UL
#define PMU_REQ0          (PMU_BASE + 0x110)  /* bit 0: GPU bus idle request */
#define PMU_ACK0          (PMU_BASE + 0x120)
#define PMU_IDLE0         (PMU_BASE + 0x128)
#define PMU_CLK_UNGATE    (PMU_BASE + 0x140)  /* bit 0: held open during the transition */
#define PMU_PWR_CON1      (PMU_BASE + 0x214)  /* bit 9: PD_GPU off */
#define PMU_STATUS0       (PMU_BASE + 0x230)  /* bit 25: PD_GPU off */
#define PMU_REPAIR0       (PMU_BASE + 0x570)  /* bit 25: PD_GPU memory repair done */

#define CRU_BASE          0x27200000UL
#define CRU_CLKSEL165     (CRU_BASE + 0x300 + 165 * 4)  /* clk_gpu_src_pre: mux [7:5], div [4:0] */
#define CRU_GATE69        (CRU_BASE + 0x800 + 69 * 4)   /* bit 1 src_pre, 3 clk_gpu, 8 pclk_gpu_root */

#define GPU_BASE          0x27800000UL
#define GPU_ID            (GPU_BASE + 0x000)

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
RK3576GpuPowerOn (
  VOID
  )
{
  /* GPLL / 6 = 198 MHz, the rate mainline's DT assigns. The reset default
   * (0x40) selects AUPLL. */
  MmioWrite32 (CRU_CLKSEL165, (0xFFU << 16) | 0x05);

  /*
   * Every clock of the domain and the PMU clock ungate must run while the
   * domain changes state and leaves bus idle. Without PCLK_GPU_ROOT, or
   * without the supply, the idle acknowledge never clears, and any access to
   * a GPU register after that is an SError.
   */
  MmioWrite32 (CRU_GATE69, (0x10AU << 16) | 0);
  MmioWrite32 (PMU_CLK_UNGATE, (BIT0 << 16) | BIT0);

  MmioWrite32 (PMU_PWR_CON1, (BIT9 << 16) | 0);
  if (!PollBits (PMU_STATUS0, BIT25, 0) || !PollBits (PMU_REPAIR0, BIT25, BIT25)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: GPU power domain did not come up (STATUS0=0x%08x REPAIR0=0x%08x)\n",
            MmioRead32 (PMU_STATUS0), MmioRead32 (PMU_REPAIR0)));
    goto Out;
  }

  MmioWrite32 (PMU_REQ0, (BIT0 << 16) | 0);
  if (!PollBits (PMU_ACK0, BIT0, 0) || !PollBits (PMU_IDLE0, BIT0, 0)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: GPU bus did not leave idle (ACK0=0x%08x IDLE0=0x%08x); is vdd_gpu_s0 on?\n",
            MmioRead32 (PMU_ACK0), MmioRead32 (PMU_IDLE0)));
    goto Out;
  }

  /* Safe to touch the GPU only now. */
  DEBUG ((DEBUG_ERROR, "RK3576Dxe: GPU powered, GPU_ID=0x%08x\n", MmioRead32 (GPU_ID)));

Out:
  MmioWrite32 (PMU_CLK_UNGATE, (BIT0 << 16) | 0);
}
