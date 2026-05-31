/** @file
 *
 *  Synopsys DesignWare Cores SDHCI eMMC driver
 *
 *  Copyright (c) 2017, Linaro, Ltd. All rights reserved.<BR>
 *  Copyright (c) 2022, Patrick Wildt <patrick@blueri.se>
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DwcSdhciPlatformLib.h>

#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/SdMmcOverride.h>

#include "DwcSdhciDxe.h"

#define EMMC_FORCE_HIGH_SPEED      FixedPcdGetBool(PcdDwcSdhciForceHighSpeed)
#define EMMC_DISABLE_HS400         FixedPcdGetBool(PcdDwcSdhciDisableHs400)
#define EMMC_NONDLL_STRBIN_DELAY   FixedPcdGet32(PcdDwcSdhciNonDllStrbinDelay)

STATIC EFI_HANDLE  mSdMmcControllerHandle;

/**
  Override function for SDHCI capability bits

  @param[in]      ControllerHandle      The EFI_HANDLE of the controller.
  @param[in]      Slot                  The 0 based slot index.
  @param[in,out]  SdMmcHcSlotCapability The SDHCI capability structure.
  @param[in,out]  BaseClkFreq           The base clock frequency value that
                                        optionally can be updated.

  @retval EFI_SUCCESS           The override function completed successfully.
  @retval EFI_NOT_FOUND         The specified controller or slot does not exist.
  @retval EFI_INVALID_PARAMETER SdMmcHcSlotCapability is NULL

**/
STATIC
EFI_STATUS
EFIAPI
EmmcSdMmcCapability (
  IN      EFI_HANDLE  ControllerHandle,
  IN      UINT8       Slot,
  IN OUT  VOID        *SdMmcHcSlotCapability,
  IN OUT  UINT32      *BaseClkFreq
  )
{
  SD_MMC_HC_SLOT_CAP  *Capability = SdMmcHcSlotCapability;

  if (SdMmcHcSlotCapability == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (ControllerHandle != mSdMmcControllerHandle) {
    return EFI_NOT_FOUND;
  }

  //
  // Disable ADMA2 to avoid data corruption.
  // This controller has the limitation that a single descriptor
  // cannot cross 128 MB boundaries and must be split.
  // This would require a patch in SdMmcPciHcDxe, but SDMA works
  // fine for the time being.
  //
  Capability->Adma2 = 0;

  //
  // Override slot type to Embedded Slot (0x1) so EmmcDxe binds
  // instead of SdDxe.  The hardware reports RemovableSlot (0x0)
  // but the eMMC is soldered on-board.
  //
  Capability->SlotType = 1;

  Capability->Hs400 = !EMMC_DISABLE_HS400;

  if (EMMC_FORCE_HIGH_SPEED) {
    Capability->BaseClkFreq = 52;
    *BaseClkFreq            = 52;   // keep Private->BaseClkFreq[Slot] in sync
    Capability->Sdr50       = 0;
    Capability->Ddr50       = 0;
    Capability->Sdr104      = 0;
    Capability->Hs400       = 0;
  }

  return EFI_SUCCESS;
}

/**

  Override function for SDHCI controller operations

  @param[in]      ControllerHandle      The EFI_HANDLE of the controller.
  @param[in]      Slot                  The 0 based slot index.
  @param[in]      PhaseType             The type of operation and whether the
                                        hook is invoked right before (pre) or
                                        right after (post)
  @param[in,out]  PhaseData             The pointer to a phase-specific data.

  @retval EFI_SUCCESS           The override function completed successfully.
  @retval EFI_NOT_FOUND         The specified controller or slot does not exist.
  @retval EFI_INVALID_PARAMETER PhaseType is invalid

**/
STATIC
EFI_STATUS
EFIAPI
EmmcSdMmcNotifyPhase (
  IN      EFI_HANDLE               ControllerHandle,
  IN      UINT8                    Slot,
  IN      EDKII_SD_MMC_PHASE_TYPE  PhaseType,
  IN OUT  VOID                     *PhaseData
  )
{
  SD_MMC_BUS_MODE  *Timing;
  UINTN            MaxClockFreq;
  UINT32           Value, i;
  UINT32           TxClkTapNum;

  DEBUG ((DEBUG_INFO, "%a\n", __FUNCTION__));

  if (ControllerHandle != mSdMmcControllerHandle) {
    return EFI_SUCCESS;
  }

  ASSERT (Slot == 0);

  switch (PhaseType) {
    case EdkiiSdMmcResetPost:
      /* SW_RST_ALL clears vendor registers on RK3576, including:
       *   EMMC_MISC_CON[1]  (MISC_INTCLK_EN) → internal clock disabled → all cmds timeout
       *   EMMC_CTRL[2]      (EMMC_RST_N)     → eMMC held in hardware reset → CMD0 timeout
       * Matches rk35xx_sdhci_reset() in Linux sdhci-of-dwcmshc.c. */
      MmioOr32 (EMMC_MISC_CON,  EMMC_MISC_INTCLK_EN);
      MmioOr32 (EMMC_EMMC_CTRL, BIT2);
      gBS->Stall (200);
      break;

    case EdkiiSdMmcInitHostPost:
      /*
       * Just before this Notification POWER_CTRL is toggled to power off
       * and on the card.  On this controller implementation, toggling
       * power off also removes SDCLK_ENABLE (BIT2) from from CLOCK_CTRL.
       * Since the clock has already been set up prior to the power toggle,
       * re-add the SDCLK_ENABLE bit to start the clock.
       */
      MmioOr16 ((UINT32)SD_MMC_HC_CLOCK_CTRL, CLOCK_CTRL_SDCLK_ENABLE);
      break;

    case EdkiiSdMmcUhsSignaling:
      if (PhaseData == NULL) {
        return EFI_INVALID_PARAMETER;
      }

      Timing = (SD_MMC_BUS_MODE *)PhaseData;
      if (*Timing == SdMmcMmcHs400) {
        /* HS400: set CARD_IS_EMMC to enable Data Strobe, and non-standard HOST_CTRL2 bits.
         * Matches dwcmshci_set_uhs_signaling() in Linux sdhci-of-dwcmshc.c. */
        MmioOr32 (EMMC_EMMC_CTRL, EMMC_CTRL_CARD_IS_EMMC);
        MmioOr16 ((UINT32)SD_MMC_HC_HOST_CTRL2, HOST_CTRL2_HS400);
      }

      break;

    case EdkiiSdMmcSwitchClockFreqPost:
      if (PhaseData == NULL) {
        return EFI_INVALID_PARAMETER;
      }

      Timing = (SD_MMC_BUS_MODE *)PhaseData;
      switch (*Timing) {
        case SdMmcMmcHs400:
        case SdMmcMmcHs200:
          MaxClockFreq = 200000000UL;
          break;
        case SdMmcMmcHsSdr:
        case SdMmcMmcHsDdr:
          MaxClockFreq = 52000000UL;
          break;
        default:
          MaxClockFreq = 26000000UL;
          break;
      }

      DwcSdhciSetClockRate (MaxClockFreq);

      if (MaxClockFreq <= 52000000UL) {
        /* Non-DLL path: set bypass + start so the DLL does not produce
         * spurious output, gate RXCLK, zero TX/CMD delay taps.
         * STRBIN delay_num is platform-specific (RK3576: 0xa, RK3588: 0x10). */
        MmioWrite32 (EMMC_DLL_CTRL,   EMMC_DLL_CTRL_BYPASS | EMMC_DLL_CTRL_START);
        MmioWrite32 (EMMC_DLL_RXCLK,  EMMC_DLL_RXCLK_ORI_GATE);
        MmioWrite32 (EMMC_DLL_TXCLK,  0);
        MmioWrite32 (EMMC_DLL_CMDOUT, 0);
        MmioWrite32 (
          EMMC_DLL_STRBIN,
          EMMC_DLL_DLYENA |
          EMMC_DLL_STRBIN_DELAY_NUM_SEL |
          EMMC_NONDLL_STRBIN_DELAY << EMMC_DLL_STRBIN_DELAY_NUM_OFFSET
          );
        break;
      }

      MmioWrite32 (EMMC_DLL_CTRL, EMMC_DLL_CTRL_SRST);
      gBS->Stall (1);
      MmioWrite32 (EMMC_DLL_CTRL, 0);

      MmioWrite32 (
        EMMC_DLL_CTRL,
        EMMC_DLL_CTRL_START_POINT_DEFAULT |
        EMMC_DLL_CTRL_INCREMENT_DEFAULT | EMMC_DLL_CTRL_START
        );

      for (i = 0; i < 500; i++) {
        Value = MmioRead32 (EMMC_DLL_STATUS0);
        if (Value & EMMC_DLL_STATUS0_DLL_LOCK &&
            !(Value & EMMC_DLL_STATUS0_DLL_TIMEOUT))
        {
          break;
        }

        gBS->Stall (1);
      }

      TxClkTapNum = EMMC_DLL_TXCLK_TAPNUM_DEFAULT;

      if (*Timing == SdMmcMmcHs400) {
        TxClkTapNum = EMMC_DLL_TXCLK_TAPNUM_90_DEGREES;

        MmioWrite32 (
          EMMC_DLL_CMDOUT,
          EMMC_DLL_CMDOUT_SRC_CLK_NEG |
          EMMC_DLL_CMDOUT_EN_SRC_CLK_NEG |
          EMMC_DLL_DLYENA |
          EMMC_DLL_CMDOUT_TAPNUM_90_DEGREES |
          EMMC_DLL_TAPNUM_FROM_SW
          );
      }

      MmioWrite32 (EMMC_DLL_RXCLK, EMMC_DLL_DLYENA);

      MmioWrite32 (
        EMMC_DLL_TXCLK,
        EMMC_DLL_DLYENA |
        TxClkTapNum | EMMC_DLL_TAPNUM_FROM_SW |
        EMMC_DLL_NO_INVERTER
        );

      MmioWrite32 (
        EMMC_DLL_STRBIN,
        EMMC_DLL_DLYENA |
        EMMC_DLL_STRBIN_TAPNUM_DEFAULT | EMMC_DLL_TAPNUM_FROM_SW
        );
      break;

    default:
      break;
  }

  return EFI_SUCCESS;
}

STATIC EDKII_SD_MMC_OVERRIDE  mSdMmcOverride = {
  EDKII_SD_MMC_OVERRIDE_PROTOCOL_VERSION,
  EmmcSdMmcCapability,
  EmmcSdMmcNotifyPhase,
};

EFI_STATUS
EFIAPI
DwcSdhciDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  DEBUG ((DEBUG_BLKIO, "%a\n", __FUNCTION__));

  /* Identification clock: 375 kHz (xin_24m / 64).
   * On RK3576, SDHCI CLOCK_CONTROL frequency-select bits are NOT functional —
   * the actual eMMC clock is fully controlled by the CRU (DwcSdhciSetClockRate).
   * Matches dwcmshc_rk3568_set_clock(): "clock <= 400000 → use 375000". */
  DwcSdhciSetClockRate (375000UL);

  /* Configure pins */
  DwcSdhciSetIoMux ();

  /* Enable SDHCI internal clock (MISC_INTCLK_EN) and disable Command Conflict Check. */
  MmioOr32  (EMMC_MISC_CON,  EMMC_MISC_INTCLK_EN);
  MmioWrite32 (EMMC_HOST_CTRL3, 0);

  /* Deassert eMMC hardware reset (EMMC_CTRL[2] = EMMC_RST_N).
   * Power-on default is 0 (RST_N driven low = eMMC in hardware reset).
   * When SPL boots from SD/SPI it never initialises the eMMC controller,
   * so RST_N stays asserted and CMD0 times out.  Setting bit 2 releases
   * reset; eMMC spec requires ≥200 µs before the first command. */
  MmioOr32 (EMMC_EMMC_CTRL, BIT2);
  gBS->Stall (200);

  /* Initialise DLL in bypass mode (non-DLL path, same as ≤52 MHz runtime). */
  MmioWrite32 (EMMC_DLL_CTRL,   EMMC_DLL_CTRL_BYPASS | EMMC_DLL_CTRL_START);
  MmioWrite32 (EMMC_DLL_RXCLK,  EMMC_DLL_RXCLK_ORI_GATE);
  MmioWrite32 (EMMC_DLL_TXCLK,  0);
  MmioWrite32 (EMMC_DLL_STRBIN, 0);

  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeSdhci,
             NonDiscoverableDeviceDmaTypeNonCoherent,
             NULL,
             &mSdMmcControllerHandle,
             1,
             DWC_SDHCI_BASE,
             0x10000
             );
  ASSERT_EFI_ERROR (Status);

  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEdkiiSdMmcOverrideProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  (VOID **)&mSdMmcOverride
                  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
