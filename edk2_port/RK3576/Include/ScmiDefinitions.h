/** @file
 *
 *  RK3576 SCMI Clock Definitions
 *
 *  Clock IDs from TF-A: plat/rockchip/rk3576/include/platform_def.h
 *  These are the SCMI clock indices used by BL31 for clock management.
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RK3576_SCMI_DEFINITIONS_H__
#define __RK3576_SCMI_DEFINITIONS_H__

/*
 * RK3576 SCMI clock IDs
 * From TF-A plat/rockchip/rk3576/include/platform_def.h
 *
 * Note: These differ from RK3588. RK3576 has only 2 CPU clusters
 * (ARMCLK_L for A53, ARMCLK_B for A72) instead of RK3588's 3.
 */
#define SCMI_CLK_CPUL        0   /* A53 little cluster */
#define SCMI_CLK_CPUB        1   /* A72 big cluster */
#define SCMI_CLK_DDR         2
#define SCMI_CLK_GPU         3
#define SCMI_CLK_NPU         4
#define SCMI_CLK_SBUS        5
#define SCMI_PCLK_SBUS       6
#define SCMI_CCLK_SD         7
#define SCMI_DCLK_SD         8
#define SCMI_ACLK_SECURE_NS  9
#define SCMI_HCLK_SECURE_NS  10
#define SCMI_TCLK_WDT        11
#define SCMI_CCLK_EMMC       12
#define SCMI_HCLK_SD         13
#define SCMI_SPLL            14

#endif // __RK3576_SCMI_DEFINITIONS_H__
