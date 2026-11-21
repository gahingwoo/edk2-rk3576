/** @file
 *
 *  RK3576 "CPU Maximum Frequency" setting.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_CPU_MAX_FREQ_H_
#define RK3576_CPU_MAX_FREQ_H_

//
// Values of the CpuMaxFreq variable (PcdCpuMaxFreq).
//
#define CPU_MAX_FREQ_DEFAULT  0   // PcdCPU{L,B}ClusterClockHz, 1608 MHz
#define CPU_MAX_FREQ_MAX      1   // mainline's top OPPs, 2016 / 2208 MHz

//
// Don't declare these in the VFR file.
//
#ifndef VFR_FILE_INCLUDE
#include <Protocol/ArmScmiClock2Protocol.h>

/**
  Publish CpuMaxFreqSupported and seed PcdCpuMaxFreq.
**/
VOID
RK3576CpuMaxFreqSetupVariables (
  VOID
  );

/**
  With "Max" selected on a board that supports it, raise the CPU rails and
  then the cluster clocks once the PMIC's I2C bus is up.  Called after the
  default rates are applied; every failure leaves them in place.
**/
VOID
RK3576CpuMaxFreqStart (
  IN SCMI_CLOCK2_PROTOCOL  *Clock
  );

#endif // VFR_FILE_INCLUDE

#endif // RK3576_CPU_MAX_FREQ_H_
