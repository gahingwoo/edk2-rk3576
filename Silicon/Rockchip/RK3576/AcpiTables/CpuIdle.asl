/** @file
 *
 *  Secondary System Description Table (SSDT): CPU idle states (_LPI) for the
 *  eight RK3576 cores, entered through PSCI CPU_SUSPEND.
 *
 *  Installed only when "CPU Idle States" is enabled in setup
 *  (PcdAcpiCpuIdleStates, off by default): RK3576AcpiPlatformDxe skips any
 *  SSDT with OEM table ID "RKLPI" otherwise and installs CpuIdleWfi.asl
 *  ("RKWFI") in its place, which has state 0 below and nothing else.
 *
 *  Windows: bugcheck 0x5C
 *  ----------------------
 *  With this table Windows 11 23H2 does not boot.  2026-11-26, TaishanPi 3M
 *  (option set to Enabled by hand) and ROCK 4D (empty variable store, when
 *  the default was briefly on): HAL_INITIALIZATION_FAILED (0x5C), parameters
 *  0x113, 0x21, 0xFFFFF79C0001C1F8, 0.  The same boot with CpuIdleWfi.asl
 *  (state 0 only) works, so it is state 1.
 *
 *  Microsoft does not document the 0x5C parameters.  What state 1 has that
 *  state 0 does not, and what Windows could object to:
 *
 *    - Arch Context Lost Flags bit 0 (core context lost).  Windows has to
 *      assume that the core's generic timer stops in such a state (it does
 *      here, mainline's local-timer-stop), and then needs a timer that keeps
 *      running to wake the core.  This firmware's GTDT has none: no platform
 *      (memory-mapped) GT block, PlatformTimerCount 0, and no Always-On flag
 *      on the per-core timers.  The HAL selects its timers early, before any
 *      driver; a state it cannot serve with any timer is a plausible reason
 *      for a 0x5C.  This is the likeliest cause, not a confirmed one, and
 *      not something an _LPI value can fix: the timer really stops.
 *    - The FFH address 0x00010000: the PSCI power_state in the original
 *      format, power down at level 0, state ID 0, which is what this BL31
 *      accepts (rockchip_validate_power_state wants state ID 0) and what
 *      Linux passes.  A format problem would fail the PSCI call, not HAL
 *      init.
 *    - Latency and residency are ordinary values, and the missing residency
 *      and usage counters, the _OSC and the missing processor container
 *      _LPI are the same in CpuIdleWfi.asl, which boots.
 *
 *  The untested experiment that follows from the first point is in
 *  RK3576AcpiPlatformDxe.c (CpuIdleGtdtAlwaysOn): with this table only,
 *  mark the per-core timers Always-On in the GTDT.  If 0x5C goes away, that
 *  was the check; but the flag is then not true, and a core sleeping with a
 *  pending timer would wake late.  A real fix needs a timer that runs
 *  through core power-down and that Windows can use; RK3576's Rockchip
 *  timers (0x2acc0000) are not a GT block.
 *
 *  The states
 *  ----------
 *
 *  Two per core, the same on both clusters:
 *
 *    0  WFI         FFH address 0xFFFFFFFF: plain WFI (Arm FFH specification,
 *                   DEN0048, _LPI entry method).  OSPM wants it first.
 *    1  CorePwrDn   FFH address 0x00010000: the PSCI power_state for
 *                   CPU_SUSPEND, power level 0 (core), state type 1 (power
 *                   down), state ID 0.  This is mainline's
 *                     CPU_SLEEP: cpu-sleep {
 *                       arm,psci-suspend-param = <0x0010000>;
 *                       entry-latency-us = <120>;
 *                       exit-latency-us = <250>;
 *                       min-residency-us = <900>;
 *                       local-timer-stop; };
 *                   (rk3576.dtsi), which every cpu@ node lists in
 *                   cpu-idle-states.  Wake latency is entry + exit, the way
 *                   Linux's dt_idle_states.c turns the two into one.
 *
 *  What BL31 does with them (TF-A v2.14, plat/rockchip):
 *    - common/plat_pm.c rockchip_validate_power_state() takes the original
 *      (not extended) power_state format and wants state ID 0.  Power down
 *      at level 0 asks for the core OFF and the cluster and system in
 *      retention.
 *    - rockchip_pwr_domain_suspend() disables this core's GIC CPU interface
 *      and calls rk3576/drivers/pmu/pmu.c rockchip_soc_cores_pwr_dm_suspend(),
 *      which arms the PMU's automatic power-down on WFI with wake-up on an
 *      interrupt (core_pwr_wfi_int).  The core comes back through BL31's
 *      warm entry point and rockchip_pwr_domain_suspend_finish() enables the
 *      CPU interface again.
 *    - rk3576 implements no cluster-level suspend (it does not override the
 *      weak rockchip_soc_hlvl_pwr_dm_suspend), so there is no cluster state
 *      here and the processor containers get no _LPI.
 *
 *  GICv2
 *  -----
 *  RK3576 has a GIC-400 (GICv2), not RK3588's GICv3.  There is no
 *  redistributor to lose, and the distributor stays powered, so the
 *  architectural context lost flags say "core context" only (bit 0) and
 *  nothing about GICR (bit 2) or GICD (bit 3).  Each core's banked CPU
 *  interface is saved and restored by BL31 around the power down, not by the
 *  OS.  The wake-up is the PMU's, on the core's interrupt line from the GIC,
 *  which is the same path Linux uses on this SoC with the DT state above.
 *
 *  The timer
 *  ---------
 *  The core's generic timer stops while the core is powered down
 *  (local-timer-stop above; Linux on arm64 treats core-context-lost the same
 *  way, arch_get_idle_state_flags()).  The GTDT does not mark the per-core
 *  timers always-on and RK3576 has no memory-mapped generic timer frame an
 *  OS could use instead (Linux uses the Rockchip timer at 0x2acc0000 for
 *  that, which ACPI cannot describe).  How Windows handles a timer-stopping
 *  state without one has not been observed on this hardware: that is the
 *  first thing to look at if a boot with this table hangs or its clock runs
 *  slow.
 *
 *  _OSC
 *  ----
 *  Linux uses _LPI only when the platform-wide \_SB._OSC grants
 *  "Platform Coordinated LPI" (bit 7; OSC_SB_PCLPI_SUPPORT,
 *  drivers/acpi/bus.c).  The DSDT has no \_SB._OSC, so this table brings
 *  one, granting that bit and nothing else.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("CpuIdle.aml", "SSDT", 2, "RKCP  ", "RKLPI   ", 1)
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
        2,                      // Count
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
        },
        Package () {            // State 1: core power down
          900,                  // Min Residency (us): min-residency-us
          370,                  // Worst Case Wakeup Latency (us): entry 120 + exit 250
          1,                    // Flags: enabled
          1,                    // Arch Context Lost Flags: core context only (GICv2)
          0,                    // Residency Counter Frequency
          0,                    // Enabled Parent State: none
          ResourceTemplate () { // Entry Method: FFH, PSCI CPU_SUSPEND power_state
            Register (FFixedHW, 0x20, 0x00, 0x00010000, 0x03)
          },
          ResourceTemplate () { // Residency Counter: none
            Register (SystemMemory, 0, 0, 0, 0)
          },
          ResourceTemplate () { // Usage Counter: none
            Register (SystemMemory, 0, 0, 0, 0)
          },
          "CorePwrDn"
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
