/** @file
 *
 *  RK3576 SoC DXE init driver — Radxa ROCK 4D
 *
 *  Initializes: ComboPHY, LED, early platform config
 *  More sub-modules (CPU perf menu, display) to be added in future iterations.
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/GpioLib.h>
#include <VarStoreData.h>
#include <Soc.h>

STATIC EFI_HANDLE  mRK3576DxeHandle = NULL;

STATIC
VOID
RK3576InitComboPhy (
  VOID
  )
{
  UINT32 Phy0Mode = PcdGet32 (PcdComboPhy0ModeDefault);
  UINT32 Phy1Mode = PcdGet32 (PcdComboPhy1ModeDefault);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: ComboPHY0 mode=%u, PHY1 mode=%u\n",
          Phy0Mode, Phy1Mode));

  /* PHY init is handled by TF-A BL31 SCMI / rkbin DDR init.
   * UEFI only needs to assert PCIe reset and power enable (done in
   * RockchipPlatformLib PcieIoInit/PciePowerEn/PciePeReset).
   * Full ComboPHY register programming TBD when RK3576 TRM is available.
   */
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
  DEBUG ((DEBUG_INFO, "  Display PCDs: ConnectorsMask=0x%08x SignalingMode=%u\n",
          PcdGet32 (PcdDisplayConnectorsMask), PcdGet8 (PcdHdmiSignalingMode)));
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

  /* Early board platform init (WiFi power, PCIe power) */
  PlatformEarlyInit ();

  /* Initialize ComboPHY mode */
  RK3576InitComboPhy ();

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
