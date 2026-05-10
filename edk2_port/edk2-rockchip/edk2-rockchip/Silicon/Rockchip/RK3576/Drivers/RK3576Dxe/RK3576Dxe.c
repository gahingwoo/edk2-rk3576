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
 * ReadyToBoot callback — dumps HDMI route registers AFTER the display
 * stack (Vop2Dxe, DwHdmiQpLib, LcdGraphicsOutputDxe) has had a chance
 * to initialize.  Compare with the early-boot dump at RK3576EntryPoint
 * to see the change in each register.
 *
 * Registers to watch:
 *   GPIO4_PC_IOMUX : should read 0x9999 (func-9 on all 4 HDMI pins)
 *   HPD_STATUS    : bit 3 = 1 means cable detected
 *   PMU1CRU gates : bit1 of CON0=0, bit0 of CON5=0  → clocks ungated
 *   HDPTX GRF     : bit2 of STATUS=1 → PHY PLL locked
 *   VOP2 IF_CTRL  : bits[1:0]=3 → interface + clkout enabled
 */
STATIC
VOID
EFIAPI
RK3576HdmiRouteDump (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DEBUG ((DEBUG_INFO, "\n"));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
  DEBUG ((DEBUG_INFO, "  RK3576 HDMI route dump (ReadyToBoot / post-display-init)\n"));
  DEBUG ((DEBUG_INFO, "  [IOC]      GPIO4_PC_IOMUX @0x2604A390 = 0x%08x  (want 0x____9999)\n",
          MmioRead32 (0x26040000 + 0xA390)));
  DEBUG ((DEBUG_INFO, "  [IOC]      HPD_STATUS     @0x2604A440 = 0x%08x  (want bit3=1 if cable)\n",
          MmioRead32 (0x26040000 + 0xA440)));
  DEBUG ((DEBUG_INFO, "  [PMU1CRU]  CLKGATE_CON0   @0x27220800 = 0x%08x  (bit1=PCLK_HDPTX_APB, bit13=CLK_PMUPHY_REF_SRC/cpll-div, SET=gated-OK PHY uses xin24m)\n",
          MmioRead32 (0x27220000 + 0x800)));
  DEBUG ((DEBUG_INFO, "  [PMU1CRU]  CLKGATE_CON5   @0x27220814 = 0x%08x  (want bit0=0 PCLK_PMUPHY_ROOT)\n",
          MmioRead32 (0x27220000 + 0x814)));
  DEBUG ((DEBUG_INFO, "  [CRU]      CLKGATE_CON64  @0x27200900 = 0x%08x  (VOP/HDMI APB clk gates)\n",
          MmioRead32 (0x27200000 + 0x900)));
  DEBUG ((DEBUG_INFO, "  [HDPTXPHY] GRF_STATUS     @0x26032080 = 0x%08x  (bit2=PHY_CLK_RDY)\n",
          MmioRead32 (0x26032000 + 0x80)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     HDMI0_IF_CTRL  @0x27D00184 = 0x%08x  (want bits[1:0]=3)\n",
          MmioRead32 (0x27D00000 + 0x184)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     VP0_DSP_CTRL   @0x27D00C00 = 0x%08x  (bit31=STANDBY, should be 0)\n",
          MmioRead32 (0x27D00000 + 0xC00)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     VP0_HTOTAL     @0x27D00C48 = 0x%08x  (want 0x0898002C=1080p)\n",
          MmioRead32 (0x27D00000 + 0xC48)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     VP0_HACT       @0x27D00C4C = 0x%08x  (want 0x00C00840=1080p)\n",
          MmioRead32 (0x27D00000 + 0xC4C)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     VP0_VTOTAL     @0x27D00C50 = 0x%08x  (want 0x04650005=1080p)\n",
          MmioRead32 (0x27D00000 + 0xC50)));
  DEBUG ((DEBUG_INFO, "  [VOP2]     VP0_VACT       @0x27D00C54 = 0x%08x  (want 0x00290461=1080p)\n",
          MmioRead32 (0x27D00000 + 0xC54)));
  /* HDMI TX controller (DesignWare HDMI QP) base = 0x27DA0000 */
  /* NOTE: VIDQPCLK_OFF=BIT(3) and LINKQPCLK_OFF=BIT(5) are bits of CMU_CONFIG0 (0xA0), NOT CMU_STATUS (0xB0) */
  DEBUG ((DEBUG_INFO, "  [HDMITX]   CMU_CONFIG0    @0x27DA00A0 = 0x%08x  (bit3=VIDQPCLK_OFF,bit5=LNKQPCLK_OFF; 0=enabled)\n",
          MmioRead32 (0x27DA0000 + 0xA0)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   CMU_STATUS     @0x27DA00B0 = 0x%08x  (raw PHY CMU status)\n",
          MmioRead32 (0x27DA0000 + 0xB0)));
  /* Also dump VP0 DCLK mux — CLKSEL_CON(147) bit11: 0=vp0_src, 1=hdmiphy_pixel0 */
  DEBUG ((DEBUG_INFO, "  [CRU]      CLKSEL_CON147  @0x2720054C = 0x%08x  (bit11=VP0 DCLK mux; want 1=hdmiphy_pixel0)\n",
          MmioRead32 (0x27200000 + 0x054C)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   MAIN_STATUS0   @0x27DA0180 = 0x%08x\n",
          MmioRead32 (0x27DA0000 + 0x180)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   VID_IF_STATUS  @0x27DA0814 = 0x%08x  (video interface status)\n",
          MmioRead32 (0x27DA0000 + 0x814)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   VID_MON_ST0    @0x27DA0884 = 0x%08x  (active frame count)\n",
          MmioRead32 (0x27DA0000 + 0x884)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   LINK_CONFIG0   @0x27DA0968 = 0x%08x  (bit0=1 means DVI mode)\n",
          MmioRead32 (0x27DA0000 + 0x968)));
  DEBUG ((DEBUG_INFO, "  [HDMITX]   PKTSCHED_PKT_EN@0x27DA0AA8 = 0x%08x  (bit3=GCP_TX_EN)\n",
          MmioRead32 (0x27DA0000 + 0xAA8)));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
}

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
  DEBUG ((DEBUG_INFO, "    IOC GPIO4_PC_IOMUX@0xA390 (pre-init)                  = 0x%08x  (want 0x9999 post-init)\n",
          MmioRead32 (0x26040000 + 0xA390)));
  DEBUG ((DEBUG_INFO, "    PMU1CRU CLKGATE_CON0@0x800 (pre-init)                 = 0x%08x  (bit1=PCLK_HDPTX_APB gate)\n",
          MmioRead32 (0x27220000 + 0x800)));
  DEBUG ((DEBUG_INFO, "    PMU1CRU CLKGATE_CON5@0x814 (pre-init)                 = 0x%08x  (bit0=PCLK_PMUPHY_ROOT gate)\n",
          MmioRead32 (0x27220000 + 0x814)));
  DEBUG ((DEBUG_INFO, "    CRU CLKGATE_CON64@0x900    (pre-init)                 = 0x%08x  (VOP/HDMI clk gates)\n",
          MmioRead32 (0x27200000 + 0x900)));
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

  /* Register a ReadyToBoot callback to dump HDMI route registers once the
   * display stack has had a chance to initialize.  Fires just before the
   * selected boot option runs (or at BDS timeout if no OS is booted).     */
  {
    EFI_EVENT   DumpEvent;
    EFI_STATUS  EvStatus;
    EvStatus = gBS->CreateEventEx (
                     EVT_NOTIFY_SIGNAL,
                     TPL_CALLBACK,
                     RK3576HdmiRouteDump,
                     NULL,
                     &gEfiEventReadyToBootGuid,
                     &DumpEvent
                     );
    if (EFI_ERROR (EvStatus)) {
      DEBUG ((DEBUG_WARN, "RK3576Dxe: ReadyToBoot dump event failed: %r\n", EvStatus));
    }
  }

  DEBUG ((DEBUG_INFO, "RK3576Dxe: Init complete\n"));
  return EFI_SUCCESS;
}
