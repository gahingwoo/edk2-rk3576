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
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
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
  // AcpiFanControl: whether the board's fan SSDT is installed.  The option is
  // only shown on a board that has one (AcpiFanControlSupported).
  //
  Var8   = FixedPcdGetBool (PcdAcpiFanControlSupported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiFanControlSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiFanControl",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiFanControl, FixedPcdGet8 (PcdAcpiFanControlDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiCpuIdleStates: which _LPI SSDT is installed, CpuIdle.asl (1, WFI
  // and core power-down) or CpuIdleWfi.asl (0, WFI only).
  // Every board that builds ACPI tables carries it, so there is no
  // "supported" variable.
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiCpuIdleStates",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiCpuIdleStates, FixedPcdGet8 (PcdAcpiCpuIdleStatesDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiNpu: whether the NPU is powered and its SSDT installed.  The option
  // is only shown on a board that can set the NPU rail (AcpiNpuSupported:
  // the board names its RK806's I2C bus, see Npu.c).
  //
  Var8   = (FixedPcdGet32 (PcdRk806I2cBase) != 0) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiNpuSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiNpu",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiNpu, FixedPcdGet8 (PcdAcpiNpuDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiVdec: whether the video decoder is powered and its SSDT installed.
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiVdec",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiVdec, FixedPcdGet8 (PcdAcpiVdecDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiSdioWifi: whether the board powers and probes its SDIO Wi-Fi module
  // and installs the Wi-Fi SSDT.  Shown only on a board that has both
  // (AcpiSdioWifiSupported).
  //
  Var8   = FixedPcdGetBool (PcdAcpiSdioWifiSupported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiSdioWifiSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiSdioWifi",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiSdioWifi, FixedPcdGet8 (PcdAcpiSdioWifiDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // Pin40Uart / Pin40RhProxy: the 40-pin header's UARTs, and its GPIO, I2C
  // and SPI for applications.  Shown only on a board whose library and
  // AcpiTables describe its header (Pin40Supported).
  //
  Var8   = FixedPcdGetBool (PcdPin40Supported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"Pin40Supported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"Pin40Uart",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdPin40Uart, FixedPcdGet8 (PcdPin40UartDefault));
    ASSERT_EFI_ERROR (Status);
  }

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"Pin40RhProxy",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdPin40RhProxy, FixedPcdGet8 (PcdPin40RhProxyDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // HwWatchdog: whether RK3576AcpiPlatformDxe prepares WDT_NS and installs
  // the WDAT for an ACPI OS (Wdat.aslc).
  //
  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"HwWatchdog",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdHwWatchdog, FixedPcdGet8 (PcdHwWatchdogDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiPwrKey: the RK806 power key as the OS's power button.  Shown only on
  // a board with a button on the RK806's PWRON pin whose RK806 the firmware
  // reaches (AcpiPwrKeySupported).
  //
  Var8   = (FixedPcdGetBool (PcdAcpiPwrKeySupported) && (FixedPcdGet32 (PcdRk806I2cBase) != 0)) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiPwrKeySupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiPwrKey",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiPwrKey, FixedPcdGet8 (PcdAcpiPwrKeyDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiLeds: the board's LEDs for the OS.  Shown only on a board whose
  // AcpiTables carry an LED SSDT (AcpiLedsSupported).
  //
  Var8   = FixedPcdGetBool (PcdAcpiLedsSupported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiLedsSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiLeds",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiLeds, FixedPcdGet8 (PcdAcpiLedsDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // AcpiSaradc: the SARADC and the board's keys on it for the OS.  Shown
  // only on a board whose AcpiTables carry a SARADC SSDT
  // (AcpiSaradcSupported).
  //
  Var8   = FixedPcdGetBool (PcdAcpiSaradcSupported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"AcpiSaradcSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (
                  L"AcpiSaradc",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var8
                  );
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdAcpiSaradc, FixedPcdGet8 (PcdAcpiSaradcDefault));
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
