/** @file
 *
 *  RK3576 CPU cluster clock setup.
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_CPU_PERF_H_
#define RK3576_CPU_PERF_H_

/**
  Read the cluster clocks the firmware was handed, log them, and apply the
  configured rates.  Never fails the boot: every error path logs and returns.
**/
VOID
EFIAPI
RK3576SetupCpuPerf (
  VOID
  );

/**
  Read the current rates of the two CPU clusters over SCMI.

  @param[out] LittleHz  ARMCLK_L, the Cortex-A53 cluster.
  @param[out] BigHz     ARMCLK_B, the Cortex-A72 cluster.

  @retval EFI_SUCCESS    Both were read.
  @retval Other          The SCMI clock protocol is not there or a read failed.
**/
EFI_STATUS
RK3576GetCpuClusterRates (
  OUT UINT64  *LittleHz,
  OUT UINT64  *BigHz
  );

#endif // RK3576_CPU_PERF_H_
