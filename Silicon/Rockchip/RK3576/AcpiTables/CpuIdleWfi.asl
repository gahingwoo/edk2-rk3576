/** @file
 *
 *  Secondary System Description Table (SSDT): a WFI-only idle state (_LPI)
 *  for the eight RK3576 cores.
 *
 *  Installed when "CPU Idle States" is disabled in setup
 *  (PcdAcpiCpuIdleStates == 0).  CpuIdle.asl (OEM table ID "RKLPI") is
 *  installed instead when the option is enabled, the default since
 *  2026-11-26; it has this same WFI state plus core power-down.
 *  RK3576AcpiPlatformDxe installs
 *  exactly one of the two, so the _LPI methods and \_SB._OSC below never
 *  collide with CpuIdle.asl's.
 *
 *  Why a table that only says "WFI"
 *  --------------------------------
 *  The cores idle with WFI either way.  What this changes is how Windows
 *  accounts for the idle time, and that is what Task Manager reads.
 *
 *  Windows 11 23H2 arm64 (ntoskrnl 10.0.22621, checked against the public
 *  symbols):
 *
 *    - PoIdle() looks at the PRCB's idle-state table (PRCB + 0x1200).  When
 *      the processor driver registered none, it calls HalProcessorIdle()
 *      directly and returns.  Only the other path,
 *      PpmIdleExecuteTransition(), calls PpmEndActiveTimeAccumulation()
 *      before the WFI and PpmBeginActiveTimeAccumulation() after it.
 *
 *    - Those two are what keep idle time out of the processor's performance
 *      feedback accumulator (_PROC_FEEDBACK.UnscaledTime).  Without them
 *      every PpmSnapPerformanceAccumulation() adds the whole wall-clock
 *      interval since the previous snap, idle or not.
 *
 *    - With no feedback counters registered (no _CPC, no PEP),
 *      PpmSnapPerformanceAccumulation() reports the performance-scaled time
 *      as UnscaledTime * 100, and that is what the "Processor Information"
 *      counter set turns into "% Processor Utility"
 *      (ExpQueryProcessorInformationCounters -> PoGetPerfStateAndParkingInfo
 *      -> PpmGetThroughputInfoCallback).
 *
 *  So with no idle states, % Processor Utility is wall time over wall time:
 *  exactly 100 % on every core whatever the load, while % Processor Time
 *  (which comes from the idle thread's run time, a different path) is
 *  correct.  Task Manager's CPU graph uses % Processor Utility.
 *
 *  The inbox processor driver fxppm.sys (cpu.inf, ACPI\Processor) builds
 *  the idle-state table from _LPI: InitAcpiLpiStates() -> AcpiParseLpiObject()
 *  -> DecodeArmIdleState(), which maps an FFH entry of address 0xFFFFFFFF,
 *  bit width 32, offset 0, access size 3 to its WfiIdle routine.  It does
 *  that only when the HAL reports PSCI CPU_SUSPEND (InitPsci), which this
 *  BL31 implements; nothing here calls CPU_SUSPEND.
 *
 *  This state is the same one Windows uses today (a plain WFI in the idle
 *  loop): nothing is powered down and no timer stops, so the GTDT caveat in
 *  CpuIdle.asl does not apply.
 *
 *  _OSC
 *  ----
 *  Same as CpuIdle.asl: Linux uses _LPI only when \_SB._OSC grants
 *  "Platform Coordinated LPI" (bit 7), and the DSDT has no \_SB._OSC.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("CpuIdleWfi.aml", "SSDT", 2, "RKCP  ", "RKWFI   ", 1)
{
  External (\_SB.PKG0, DeviceObj)
  External (\_SB.PKG0.CLU0.CPU0, DeviceObj)
  External (\_SB.PKG0.CLU0.CPU1, DeviceObj)
  External (\_SB.PKG0.CLU0.CPU2, DeviceObj)
  External (\_SB.PKG0.CLU0.CPU3, DeviceObj)
  External (\_SB.PKG0.CLU1.CPU4, DeviceObj)
  External (\_SB.PKG0.CLU1.CPU5, DeviceObj)
  External (\_SB.PKG0.CLU1.CPU6, DeviceObj)
  External (\_SB.PKG0.CLU1.CPU7, DeviceObj)

  Scope (\_SB)
  {
    //
    // Platform-wide _OSC (ACPI 6.5, 6.2.11.2).  Arg3 is the capabilities
    // buffer: DWORD 0 status, DWORD 1 the OS's support bits.  Grant bit 7
    // (_LPI, platform coordinated) and clear the rest.
    //
    Method (_OSC, 4, NotSerialized)
    {
      CreateDWordField (Arg3, 0x00, STS0)
      If (Arg0 == ToUUID ("0811b06e-4a27-44f9-8d60-3cbbc22e7b48"))
      {
        If (Arg1 != One)
        {
          STS0 |= 0x0A          // unrecognized revision
          Return (Arg3)
        }

        If (SizeOf (Arg3) < 8)
        {
          STS0 |= 0x02          // _OSC failure
          Return (Arg3)
        }

        CreateDWordField (Arg3, 0x04, CAP0)
        If ((CAP0 & ~0x80) != Zero)
        {
          STS0 |= 0x10          // capabilities masked
        }

        CAP0 &= 0x80
        Return (Arg3)
      }

      STS0 |= 0x06              // unrecognized UUID
      Return (Arg3)
    }

    Scope (PKG0)
    {
      Name (PLPI, Package () {
        0,                      // Version
        0,                      // Level Index
        1,                      // Count
        Package () {            // State 0: WFI
          1,                    // Min Residency (us)
          1,                    // Worst Case Wakeup Latency (us)
          1,                    // Flags: enabled
          0,                    // Arch Context Lost Flags: none
          0,                    // Residency Counter Frequency
          0,                    // Enabled Parent State: none
          ResourceTemplate () { // Entry Method: FFH, WFI
            Register (FFixedHW, 0x20, 0x00, 0xFFFFFFFF, 0x03)
          },
          ResourceTemplate () { // Residency Counter: none
            Register (SystemMemory, 0, 0, 0, 0)
          },
          ResourceTemplate () { // Usage Counter: none
            Register (SystemMemory, 0, 0, 0, 0)
          },
          "WFI"
        }
      })
    }
  }

  Scope (\_SB.PKG0.CLU0.CPU0) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU0.CPU1) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU0.CPU2) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU0.CPU3) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU1.CPU4) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU1.CPU5) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU1.CPU6) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
  Scope (\_SB.PKG0.CLU1.CPU7) { Method (_LPI, 0, NotSerialized) { Return (\_SB.PKG0.PLPI) } }
}
