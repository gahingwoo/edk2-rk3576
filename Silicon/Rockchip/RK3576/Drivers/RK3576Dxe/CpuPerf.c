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

VOID
EFIAPI
RK3576SetupCpuPerf (
  VOID
  )
{
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
