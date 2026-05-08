/** @file
 *
 *  RK3576 SoC DXE init driver — Radxa ROCK 4D
 *
 *  Initializes: Display, ComboPHY, LED, early platform config.
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/GpioLib.h>
#include <Protocol/DevicePath.h>
#include <VarStoreData.h>
/*
 * Force RK3576's own Soc.h via relative path so the shared __SOC_H__ guard
 * fires here first — preventing RK3588/Include/Soc.h (which appears earlier
 * on the compiler search path due to RK3588.dec in [Packages]) from shadowing
 * the RK3576-specific register definitions.
 */
#include "../../Include/Soc.h"
#include "RK3576DxeFormSetGuid.h"
#include "ConfigTable.h"
#include "Display.h"
#include "ComboPhy.h"

extern UINT8  RK3576DxeHiiBin[];
extern UINT8  RK3576DxeStrings[];

STATIC EFI_HANDLE  mRK3576DxeHandle = NULL;

/*
 * HII vendor device path for attaching HII package lists.
 */
typedef struct {
  VENDOR_DEVICE_PATH        VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH  mVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    RK3576DXE_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

/*
 * Register the HII package list (VFR form + UNI strings) so the UEFI
 * setup browser shows the ROCK 4D / RK3576 Configuration page.
 */
STATIC
EFI_STATUS
RK3576InstallHiiPages (
  VOID
  )
{
  EFI_STATUS      Status;
  EFI_HII_HANDLE  HiiHandle;
  EFI_HANDLE      DriverHandle;

  DriverHandle = NULL;
  Status       = gBS->InstallMultipleProtocolInterfaces (
                        &DriverHandle,
                        &gEfiDevicePathProtocolGuid,
                        &mVendorDevicePath,
                        NULL
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (
                &gRK3576DxeFormSetGuid,
                DriverHandle,
                RK3576DxeStrings,
                RK3576DxeHiiBin,
                NULL
                );
  if (HiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           DriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mVendorDevicePath,
           NULL
           );
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}


STATIC
VOID
RK3576InitStatusLed (
  VOID
  )
{
  UINT8   LedGpio    = PcdGet8  (PcdStatusLedGpio);
  BOOLEAN LedPolarity = PcdGetBool (PcdStatusLedGpioPolarity);

  if (LedGpio == 0xFF) {
    return;  /* No status LED configured */
  }

  /* Power LED on (active high = TRUE means ON) */
  PlatformSetStatusLed (LedPolarity);
  DEBUG ((DEBUG_INFO, "RK3576Dxe: Status LED GPIO%u set to %u\n",
          LedGpio, LedPolarity));
}

EFI_STATUS
EFIAPI
RK3576EntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "\n"));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
  DEBUG ((DEBUG_INFO, "  ROCK 4D / RK3576 SoC DXE Init\n"));
  DEBUG ((DEBUG_INFO, "  GRF bases (remapped):\n"));
  DEBUG ((DEBUG_INFO, "    SYS_GRF (IOC)  = 0x26040000  IOC_HDMI_HPD_STATUS@0xA440 = 0x%08x\n",
          MmioRead32 (0x26040000 + 0xA440)));
  DEBUG ((DEBUG_INFO, "    VO1_GRF (VO0)  = 0x2601A000  VO0_GRF_SOC_CON14@0x0038 = 0x%08x\n",
          MmioRead32 (0x2601A000 + 0x0038)));
  DEBUG ((DEBUG_INFO, "    HDPTXPHY GRF   = 0x26032000  GRF_HDPTX_STATUS@0x80    = 0x%08x\n",
          MmioRead32 (0x26032000 + 0x80)));
  DEBUG ((DEBUG_INFO, "    PMU1CRU        = 0x27220000  SOFTRST_CON01@0xA04      = 0x%08x\n",
          MmioRead32 (0x27220000 + 0xA04)));
  DEBUG ((DEBUG_INFO, "    BUSCRU         = 0x27200000\n"));
  DEBUG ((DEBUG_INFO, "    VOP2 base      = 0x27D00000  REG_CFG_DONE             = 0x%08x\n",
          MmioRead32 (0x27D00000 + 0x0)));
  DEBUG ((DEBUG_INFO, "    VOP2 HDMI0_IF_CTRL@0x184                              = 0x%08x\n",
          MmioRead32 (0x27D00000 + 0x184)));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
  DEBUG ((DEBUG_INFO, "RK3576Dxe: Entry (ROCK 4D, RK3576)\n"));

  /* Register HII form (ACPI/DT mode switcher) in the UEFI setup browser */
  Status = RK3576InstallHiiPages ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "RK3576Dxe: HII install failed: %r (non-fatal)\n", Status));
  }

  /* Early board platform init (WiFi power, PCIe power) */
  PlatformEarlyInit ();

  /* Seed display PCDs from NVRAM variables (or factory defaults) */
  SetupDisplayVariables ();

  /* Seed ConfigTable/FDT mode PCDs from HII NVRAM variables (or defaults) */
  SetupConfigTableVariables ();

  /* Seed ComboPHY mode PCDs from NVRAM, then initialize hardware */
  SetupComboPhyVariables ();
  ApplyComboPhyVariables ();

  /* Initialize status LED */
  RK3576InitStatusLed ();

  /* Install protocol to signal platform config is applied */
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mRK3576DxeHandle,
                  &gRockchipPlatformConfigAppliedProtocolGuid, NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: Failed to install protocol: %r\n", Status));
  }

  DEBUG ((DEBUG_INFO, "RK3576Dxe: Init complete\n"));
  return EFI_SUCCESS;
}
