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

#endif // RK3576_CPU_PERF_H_
