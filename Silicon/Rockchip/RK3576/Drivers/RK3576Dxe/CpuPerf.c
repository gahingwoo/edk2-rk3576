/** @file
 *
 *  RK3576 CPU cluster clock setup, over SCMI.
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmSmcLib.h>
#include <Protocol/ArmScmiClock2Protocol.h>

#include <ScmiDefinitions.h>

#include "CpuPerf.h"

STATIC EFI_EVENT  mScmiNotifyEvent = NULL;
STATIC VOID       *mScmiRegistration = NULL;

STATIC
VOID
ApplyOneCluster (
  IN SCMI_CLOCK2_PROTOCOL  *Clock,
  IN UINT32                ClockId,
  IN CONST CHAR8           *Name,
  IN UINT64                TargetHz
  )
{
  EFI_STATUS  Status;
  UINT64      Rate;

  //
  // Read first and say so out loud.  Windows reported the CPU at 816 MHz and
  // there was no way to tell whether that was measured or invented, because
  // the firmware never looked.  Now it does, every boot.
  //
  Rate   = 0;
  Status = Clock->RateGet (Clock, ClockId, &Rate);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuPerf: %a (clk %u): RateGet failed: %r\n",
      Name, ClockId, Status
      ));
    return;
  }

  DEBUG ((
    DEBUG_ERROR,
    "CpuPerf: %a (clk %u): boot rate %lu Hz\n",
    Name, ClockId, Rate
    ));

  if (TargetHz == 0) {
    DEBUG ((DEBUG_ERROR, "CpuPerf: %a: leaving it alone (target 0)\n", Name));
    return;
  }

  if (Rate == TargetHz) {
    DEBUG ((DEBUG_ERROR, "CpuPerf: %a: already at target\n", Name));
    return;
  }

  Status = Clock->RateSet (Clock, ClockId, TargetHz);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuPerf: %a: RateSet %lu Hz failed: %r (staying at %lu Hz)\n",
      Name, TargetHz, Status, Rate
      ));
    return;
  }

  //
  // Read back rather than trusting the set.  SCMI may round to the nearest
  // supported operating point, and the number that ends up in the log should
  // be the one the hardware is actually running.
  //
  Rate   = 0;
  Status = Clock->RateGet (Clock, ClockId, &Rate);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuPerf: %a: set %lu Hz, read-back failed: %r\n",
      Name, TargetHz, Status
      ));
    return;
  }

  DEBUG ((DEBUG_ERROR, "CpuPerf: %a: now %lu Hz\n", Name, Rate));
}

STATIC
VOID
EFIAPI
OnScmiClockAvailable (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS            Status;
  SCMI_CLOCK2_PROTOCOL  *Clock;
  UINT32                Version;

  Status = gBS->LocateProtocol (
                  &gArmScmiClock2ProtocolGuid,
                  NULL,
                  (VOID **)&Clock
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  gBS->CloseEvent (Event);
  mScmiNotifyEvent = NULL;

  Version = 0;
  Status  = Clock->GetVersion (Clock, &Version);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CpuPerf: SCMI clock GetVersion failed: %r\n", Status));
    return;
  }

  DEBUG ((DEBUG_ERROR, "CpuPerf: SCMI clock protocol v0x%08x\n", Version));

  //
  // The rates these default to are bounded by the voltage the PMIC init table
  // puts on vdd_cpu_lit_s0 / vdd_cpu_big_s0, NOT by what the silicon can do.
  // mainline's cluster OPP tables in rk3576.dtsi pair each rate with a
  // minimum:
  //
  //           A53                       A72
  //   1416 MHz  725.0 mV        1416 MHz  712.5 mV
  //   1608 MHz  750.0 mV        1608 MHz  737.5 mV
  //   1800 MHz  825.0 mV        1800 MHz  800.0 mV
  //   2016 MHz  900.0 mV        2016 MHz  862.5 mV
  //                             2208 MHz  925.0 mV
  //
  // Going above the configured rate without raising those rails first is an
  // undervolt, and an undervolted core does not fail politely.  Raise the
  // rails in the board's RK806 init table in the same change, or not at all.
  //
  ApplyOneCluster (
    Clock,
    SCMI_ARMCLK_L,
    "ARMCLK_L (A53)",
    PcdGet64 (PcdCPULClusterClockHz)
    );
  ApplyOneCluster (
    Clock,
    SCMI_ARMCLK_B,
    "ARMCLK_B (A72)",
    PcdGet64 (PcdCPUBClusterClockHz)
    );
}


//
// PSCI, as the firmware itself can see it.
//
// Windows starts 1 of 8 CPUs on this board while Linux starts all 8 on the
// same BL31, and five separate tables have been checked against each other
// and against mainline without finding a discrepancy: MADT MPIDRs and flags,
// FADT ArmBootArchFlags, the PPTT topology, the DSDT processor devices, and
// TF-A's own plat_core_pos_by_mpidr (rk3576 uses PLAT_RK_CLST_TO_CPUID_SHIFT
// 6, so 0x100..0x103 map to positions 4..7, exactly what the MADT publishes).
//
// Reading has stopped paying.  This asks PSCI directly, on the BL31 actually
// flashed rather than the source that was read.
//
// AFFINITY_INFO only queries -- it starts nothing, needs no entry point and
// leaks no core, so it is safe to call for every CPU on every boot.
//
#define PSCI_VERSION_FID        0x84000000
#define PSCI_AFFINITY_INFO_FID  0xC4000004

STATIC CONST UINT64  mCpuMpidr[] = {
  0x000, 0x001, 0x002, 0x003,   // cluster 0, Cortex-A53
  0x100, 0x101, 0x102, 0x103    // cluster 1, Cortex-A72
};

STATIC
CONST CHAR8 *
AffinityStateName (
  IN INT64  State
  )
{
  switch (State) {
    case 0:  return "ON";
    case 1:  return "OFF";
    case 2:  return "ON_PENDING";
    case -1: return "NOT_SUPPORTED";
    case -2: return "INVALID_PARAMETERS";
    case -3: return "DENIED";
    default: return "?";
  }
}

STATIC
VOID
ProbePsci (
  VOID
  )
{
  ARM_SMC_ARGS  Args;
  UINTN         Index;
  INT64         Result;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = PSCI_VERSION_FID;
  ArmCallSmc (&Args);
  DEBUG ((
    DEBUG_ERROR,
    "CpuPerf: PSCI version %u.%u\n",
    (UINT32)((Args.Arg0 >> 16) & 0xFFFF),
    (UINT32)(Args.Arg0 & 0xFFFF)
    ));

  for (Index = 0; Index < ARRAY_SIZE (mCpuMpidr); Index++) {
    ZeroMem (&Args, sizeof (Args));
    Args.Arg0 = PSCI_AFFINITY_INFO_FID;
    Args.Arg1 = mCpuMpidr[Index];
    Args.Arg2 = 0;                    // lowest affinity level = core
    ArmCallSmc (&Args);
    Result = (INT64)Args.Arg0;
    DEBUG ((
      DEBUG_ERROR,
      "CpuPerf: MPIDR 0x%03lx -> %a (%ld)\n",
      mCpuMpidr[Index],
      AffinityStateName (Result),
      Result
      ));
  }
}

VOID
EFIAPI
RK3576SetupCpuPerf (
  VOID
  )
{
  ProbePsci ();

  //
  // ArmScmiDxe is another DXE driver, so it may or may not have run yet.
  // Register for the protocol instead of depending on the dispatch order; the
  // notify fires immediately if it is already there.  A DEPEX would also work
  // but would wedge the whole driver if SCMI ever failed to come up, and this
  // driver does the display and ComboPHY setup too.
  //
  mScmiNotifyEvent = EfiCreateProtocolNotifyEvent (
                       &gArmScmiClock2ProtocolGuid,
                       TPL_CALLBACK,
                       OnScmiClockAvailable,
                       NULL,
                       &mScmiRegistration
                       );
  if (mScmiNotifyEvent == NULL) {
    DEBUG ((DEBUG_ERROR, "CpuPerf: could not register for the SCMI clock protocol\n"));
  }
}
