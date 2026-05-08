/** @file
 *
 *  RK3576 ACPI / Device Tree config table mode management.
 *
 *  SetupConfigTableVariables: called at driver entry — reads HII EFI variables
 *  and seeds the dynamic PCDs consumed by AcpiPlatformDxe and FdtPlatformDxe.
 *  When no saved variable exists the FixedPcd default is used.
 *
 *  ApplyConfigTableVariables: called after HII variables are applied — nothing
 *  to do here because the PCDs are read directly by the platform drivers.
 *
 *  Copyright (c) 2020, Jeremy Linton
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <VarStoreData.h>

#include "RK3576DxeFormSetGuid.h"
#include "ConfigTable.h"

typedef struct {
  UINT32    Id;
  CHAR16    *SupportedVarString;
} FDT_COMPAT_MODE_VAR;

STATIC FDT_COMPAT_MODE_VAR  mFdtCompatModeVarTable[] = {
  { FDT_COMPAT_MODE_VENDOR,   L"FdtCompatModeVendorSupported"   },
  { FDT_COMPAT_MODE_MAINLINE, L"FdtCompatModeMainlineSupported" },
};

STATIC
BOOLEAN
IsFdtCompatModeSupported (
  IN UINT32  CompatMode
  )
{
  return PlatformGetDtbFileGuid (CompatMode) != NULL;
}

STATIC
BOOLEAN
IsAcpiPcieEcamCompatModeSupported (
  IN UINT32  CompatMode
  )
{
  switch (CompatMode) {
    case ACPI_PCIE_ECAM_COMPAT_MODE_AUTO:
    case ACPI_PCIE_ECAM_COMPAT_MODE_SINGLE_DEV:
    case ACPI_PCIE_ECAM_COMPAT_MODE_NXPMX6:
    case ACPI_PCIE_ECAM_COMPAT_MODE_GRAVITON:
      return TRUE;
  }

  return FALSE;
}

VOID
EFIAPI
ApplyConfigTableVariables (
  VOID
  )
{
  /* Nothing to do here — PCDs are read directly by AcpiPlatformDxe / FdtPlatformDxe. */
}

VOID
EFIAPI
SetupConfigTableVariables (
  VOID
  )
{
  UINTN                            Size;
  UINT32                           Var32;
  UINT8                            Var8;
  EFI_STATUS                       Status;
  UINTN                            Index;
  UINT32                           FirstFdtCompatModeSupported;
  FDT_OVERRIDE_PATH_VARSTORE_DATA  FdtOverridePath;

  //
  // ConfigTableMode — seeds PcdConfigTableMode used by both platform drivers.
  //
  Size   = sizeof (UINT32);
  Status = gRT->GetVariable (
                  L"ConfigTableMode",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var32
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet32S (PcdConfigTableMode, FixedPcdGet32 (PcdConfigTableModeDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiPcieEcamCompatMode — controls PCIe ECAM quirk in ACPI mode.
  //
  Size   = sizeof (UINT32);
  Status = gRT->GetVariable (
                  L"AcpiPcieEcamCompatMode",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var32
                  );
  if (EFI_ERROR (Status) || !IsAcpiPcieEcamCompatModeSupported (Var32)) {
    Status = PcdSet32S (PcdAcpiPcieEcamCompatMode, FixedPcdGet32 (PcdAcpiPcieEcamCompatModeDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // Publish which FDT compat modes are actually supported (have an embedded DTB).
  // The HII form grays out the selector when only one mode is available.
  //
  FirstFdtCompatModeSupported = FDT_COMPAT_MODE_UNSUPPORTED;

  for (Index = 0; Index < ARRAY_SIZE (mFdtCompatModeVarTable); Index++) {
    FDT_COMPAT_MODE_VAR  CompatModeVar       = mFdtCompatModeVarTable[Index];
    UINT8                CompatModeSupported = IsFdtCompatModeSupported (CompatModeVar.Id);

    Status = gRT->SetVariable (
                    CompatModeVar.SupportedVarString,
                    &gRK3576DxeFormSetGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    sizeof (CompatModeSupported),
                    &CompatModeSupported
                    );
    ASSERT_EFI_ERROR (Status);

    if (CompatModeSupported &&
        (FirstFdtCompatModeSupported == FDT_COMPAT_MODE_UNSUPPORTED))
    {
      FirstFdtCompatModeSupported = CompatModeVar.Id;
    }
  }

  //
  // FdtCompatMode
  //
  Size   = sizeof (UINT32);
  Status = gRT->GetVariable (
                  L"FdtCompatMode",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var32
                  );
  if (EFI_ERROR (Status) || !IsFdtCompatModeSupported (Var32)) {
    UINT32  FdtCompatModeDefault = FixedPcdGet32 (PcdFdtCompatModeDefault);
    if (!IsFdtCompatModeSupported (FdtCompatModeDefault)) {
      FdtCompatModeDefault = FirstFdtCompatModeSupported;
    }

    Status = PcdSet32S (PcdFdtCompatMode, FdtCompatModeDefault);
    ASSERT_EFI_ERROR (Status);
  }

  //
  // FdtForceGop
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"FdtForceGop",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdFdtForceGop, FixedPcdGet8 (PcdFdtForceGopDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // FdtSupportOverrides
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"FdtSupportOverrides",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdFdtSupportOverrides, FixedPcdGet8 (PcdFdtSupportOverridesDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // FdtOverrideFixup
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"FdtOverrideFixup",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdFdtOverrideFixup, FixedPcdGet8 (PcdFdtOverrideFixupDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // FdtOverrideBasePath
  //
  Size   = sizeof (FDT_OVERRIDE_PATH_VARSTORE_DATA);
  Status = gRT->GetVariable (
                  L"FdtOverrideBasePath",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &FdtOverridePath
                  );
  if (EFI_ERROR (Status) || (FdtOverridePath.Path[0] == L' ')) {
    if (FixedPcdGetSize (PcdFdtOverrideBasePathDefault) <= Size) {
      Status = PcdSetPtrS (PcdFdtOverrideBasePath, &Size, FixedPcdGetPtr (PcdFdtOverrideBasePathDefault));
    } else {
      ASSERT (FALSE);
      ZeroMem (&FdtOverridePath, Size);
      Status = PcdSetPtrS (PcdFdtOverrideBasePath, &Size, &FdtOverridePath);
    }

    ASSERT_EFI_ERROR (Status);
  }

  //
  // FdtOverrideOverlayPath
  //
  Size   = sizeof (FDT_OVERRIDE_PATH_VARSTORE_DATA);
  Status = gRT->GetVariable (
                  L"FdtOverrideOverlayPath",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &FdtOverridePath
                  );
  if (EFI_ERROR (Status) || (FdtOverridePath.Path[0] == L' ')) {
    if (FixedPcdGetSize (PcdFdtOverrideOverlayPathDefault) <= Size) {
      Status = PcdSetPtrS (PcdFdtOverrideOverlayPath, &Size, FixedPcdGetPtr (PcdFdtOverrideOverlayPathDefault));
    } else {
      ASSERT (FALSE);
      ZeroMem (&FdtOverridePath, Size);
      Status = PcdSetPtrS (PcdFdtOverrideOverlayPath, &Size, &FdtOverridePath);
    }

    ASSERT_EFI_ERROR (Status);
  }
}
