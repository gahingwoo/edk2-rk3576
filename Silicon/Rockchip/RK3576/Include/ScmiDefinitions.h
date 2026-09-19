/** @file
 *
 *  RK3576 SCMI Clock Definitions
 *
 *  Clock IDs as mainline's dt-bindings publishes them; see below.
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RK3576_SCMI_DEFINITIONS_H__
#define __RK3576_SCMI_DEFINITIONS_H__

/*
 * RK3576 SCMI clock IDs.
 *
 * These are the numbers passed to the SCMI clock protocol, and they come from
 * mainline's include/dt-bindings/clock/rockchip,rk3576-cru.h, where they sit
 * under the comment "SCMI clocks, use these when changing clocks through
 * SCMI".  rk3576.dtsi uses them directly:
 *
 *     cpu_l0: cpu@0   { clocks = <&scmi_clk SCMI_ARMCLK_L>; ... };
 *     cpu_b0: cpu@100 { clocks = <&scmi_clk SCMI_ARMCLK_B>; ... };
 *
 * They are NOT a dense 0,1,2,... table.  This header previously carried
 * RK3588's numbering (CPUL=0, CPUB=1, DDR=2, GPU=3, ...) while claiming to
 * come from RK3576's TF-A platform_def.h.  Nothing consumed it, so nothing
 * broke, but sending clock id 1 to this SoC would not have set the big
 * cluster.
 */
#define SCMI_ARMCLK_L  10   /* A53 little cluster */
#define SCMI_ARMCLK_B  11   /* A72 big cluster */
#define SCMI_CLK_GPU   456

#endif // __RK3576_SCMI_DEFINITIONS_H__
