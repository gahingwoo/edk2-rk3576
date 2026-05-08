/** @file
  ResetSystem library for RK3576 using PSCI with MaskROM boot mode support.

  On PSCI SYSTEM_RESET, TF-A (BL31) calls rockchip_soc_soft_reset() which
  checks PMU0_GRF_OS_REG16 (0x26024040) for the BOOT_BROM_DOWNLOAD magic.
  If found, TF-A copies it to PMU1_GRF_OS_REG0 (NPOR-persistent), clears
  the SGRF reset-hold registers, then triggers CRU_GLB_SRST_FST.  The BootROM
  then enters USB download (MaskROM) mode on restart.

  Register derivation from authoritative sources:
    U-Boot mainline arch/arm/mach-rockchip/rk3576/Kconfig:
      config ROCKCHIP_BOOT_MODE_REG
        default 0x26024040    # PMU0_GRF_BASE(0x26024000) + PMU0GRF_OS_REG(16)=0x40
    TF-A plat/rockchip/rk3576/drivers/pmu/pmu.h:
      PMU0GRF_OS_REG(i)  = (i) * 4   → OS_REG16 = 0x40
      BOOT_BROM_DOWNLOAD = 0xef08a53c

  Copyright (c) 2008, Apple Inc. All rights reserved.
  Copyright (c) 2014, Linaro Ltd. All rights reserved.
  Copyright (c) 2024, Google Llc. All rights reserved.
  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
  Copyright (c) 2025, ROCK 4D RK3576 Port

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <IndustryStandard/ArmStdSmc.h>

#include <Library/ArmMonitorLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/ResetUtilityLib.h>

/*
 * PMU0_GRF_OS_REG16 — used by BootROM and TF-A as the boot mode flag.
 *   PMU0_GRF_BASE = 0x26024000  (per RK3576 TRM / TF-A rk3576_def.h)
 *   PMU0GRF_OS_REG(n) = n * 4   (per TF-A pmu.h)
 *   OS_REG16 = 16 * 4 = 0x40
 *
 * U-Boot mainline Kconfig confirms: CONFIG_ROCKCHIP_BOOT_MODE_REG = 0x26024040
 */
#define RK3576_BOOT_MODE_REG   0x26024040U

/*
 * Magic value that causes TF-A to enter MaskROM USB download mode.
 * Written to RK3576_BOOT_MODE_REG before issuing PSCI SYSTEM_RESET.
 * TF-A's rockchip_soc_soft_reset_check_rstout() reads this, copies it
 * to PMU1_GRF_OS_REG0 (NPOR-persistent), clears SGRF reset-hold, then
 * triggers CRU_GLB_SRST_FST.
 */
#define BOOT_BROM_DOWNLOAD     0xEF08A53CU

/**
  Library constructor.  No-op; required to satisfy BaseTools constructor
  resolution when other referenced libraries have non-trivial constructors.
**/
RETURN_STATUS
EFIAPI
ResetSystemLibConstructor (
  VOID
  )
{
  return EFI_SUCCESS;
}

/**
  Cold reset via PSCI SYSTEM_RESET SMC.
**/
VOID
EFIAPI
ResetCold (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET;
  ArmMonitorCall (&Args);
}

/**
  Warm reset — mapped to cold reset if PSCI SYSTEM_RESET2 is unsupported.
**/
VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64;
  ArmMonitorCall (&Args);
  if (Args.Arg0 == ARM_SMC_PSCI_RET_SUCCESS) {
    Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64;
    ArmMonitorCall (&Args);
  } else {
    DEBUG ((DEBUG_INFO, "RK3576: warm reboot not supported, issuing cold reboot\n"));
    ResetCold ();
  }
}

/**
  Power off via PSCI SYSTEM_OFF SMC.
**/
VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_OFF;
  ArmMonitorCall (&Args);
}

/**
  Platform-specific reset — handles MaskROM entry for gRockchipResetTypeMaskromGuid.

  Sequence:
    1. Write BOOT_BROM_DOWNLOAD to PMU0_GRF_OS_REG16 (0x26024040).
    2. Issue PSCI SYSTEM_RESET; TF-A detects the magic, copies it to the
       NPOR-persistent PMU1_GRF_OS_REG0, clears SGRF reset-hold bits,
       and triggers CRU_GLB_SRST_FST — BootROM then enters USB download.
**/
VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  GUID  *ResetSubtype;

  if ((DataSize == 0) || (ResetData == NULL)) {
    goto Exit;
  }

  ResetSubtype = GetResetPlatformSpecificGuid (DataSize, ResetData);

  if (CompareGuid (ResetSubtype, &gRockchipResetTypeMaskromGuid)) {
    DEBUG ((DEBUG_INFO, "RK3576: MaskROM reset — writing 0x%08X to 0x%08X\n",
            BOOT_BROM_DOWNLOAD, RK3576_BOOT_MODE_REG));
    MmioWrite32 (RK3576_BOOT_MODE_REG, BOOT_BROM_DOWNLOAD);
    /* TF-A handles SGRF reset-hold clearing in rockchip_soc_soft_reset_check_rstout() */
  } else {
    goto Exit;
  }

Exit:
  ResetCold ();
}

/**
  Top-level ResetSystem dispatcher.
**/
VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;
    case EfiResetCold:
      ResetCold ();
      break;
    case EfiResetShutdown:
      ResetShutdown ();
      return;
    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      return;
    default:
      return;
  }
}
