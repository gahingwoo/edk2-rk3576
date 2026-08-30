/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2014-2016, Linaro Limited. All rights reserved.
 *  Copyright (c) 2014, Red Hat, Inc.
 *  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/IoLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseVariableLib.h>
#include <Library/ResetUtilityLib.h>
#include <Library/SaradcLib.h>
#include <Library/SerialPortLib.h>
#include <Pi/PiBootMode.h>

#include <Ppi/ArmMpCoreInfo.h>

#define RECOVERY_KEY_SARADC_CHANNEL       1
#define RECOVERY_KEY_PRESS_MAX_THRESHOLD  100

/**
  Return the current Boot Mode

  This function returns the boot reason on the platform

  @return   Return the current Boot Mode of the platform

**/
EFI_BOOT_MODE
ArmPlatformGetBootMode (
  VOID
  )
{
  return BOOT_WITH_FULL_CONFIGURATION;
}

/**
  This function is called by PrePeiCore, in the SEC phase.
**/
RETURN_STATUS
ArmPlatformInitialize (
  IN  UINTN  MpId
  )
{
  EFI_STATUS     Status;
  UINTN          Size;
  UINT64         BaudRate;

  //
  // The recovery-key probe that used to be here is removed.  It ran in SEC,
  // with the MMU off, and every part of it was aimed at an RK3588 SoC:
  //
  //   * SaradcLib.c defines SARADC_BASE as 0xFEC10000, which is RK3588's.
  //     RK3576's SARADC is at 0x2AE00000 (rk3576.dtsi adc@2ae00000, and the
  //     TRM agrees).  On RK3576, 0xFEC10000 is ordinary DRAM -- DRAM here is
  //     [0x40000000, 0x140000000) -- so the probe wrote five words into
  //     uninitialised memory and then polled it.
  //
  //   * Worse, SaradcStartChannel resets the block first, and our
  //     SRST_P_SARADC is 190, taken from RockchipIpRegs.h, which is the
  //     RK3588 register header.  Decoded with RK3576's reg*16+bit formula
  //     that is SOFTRST_CON11 bit 14 -- which the RK3576 TRM documents as
  //     hresetn_bus_biu, the AHB interconnect reset for the BUS domain
  //     (UART0, I2C, SPI, GPIO1-4, WDT, TSADC).  Mainline puts this reset at
  //     CON13 bit 6 (rst-rk3576.c:114).  So every boot pulsed the bus
  //     interconnect for 10 us before anything else ran.
  //
  //   * And when the DRAM garbage happened to have bit 0 set at +0x110 with
  //     a value under the threshold at +0x120, it called
  //     ResetPlatformSpecificGuid (gRockchipResetTypeMaskromGuid) -- a silent
  //     reboot into USB download mode, decided by uninitialised memory.
  //
  // SaradcLibConstructor in the same library was already emptied for exactly
  // this reason, with the note "Touching them in SEC hangs the SoC ... SARADC
  // is not used during SEC/PEI on this port".  That was true of the
  // constructor and false of this call site, which was left behind.
  //
  // To bring the recovery key back, port SaradcLib first: SARADC_BASE
  // 0x2AE00000, and the APB reset at CRU_SOFTRST_CON13 bit 6 (0x27200A34,
  // assert 0x00400040, deassert 0x00400000).  Then call it after the MMU is
  // up, not from here.
  //

  Size   = sizeof (UINT64);
  Status = BaseGetVariable (
             L"DebugSerialPortBaudRate",
             &gRK3588DxeFormSetGuid,
             NULL,
             &Size,
             &BaudRate
             );
  if (EFI_ERROR (Status) || (BaudRate == 0)) {
    return RETURN_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "%a: Setting baud rate to %lu\n", __func__, BaudRate));

  PatchPcdSet64 (PcdUartDefaultBaudRate, BaudRate);
  SerialPortInitialize ();

  return RETURN_SUCCESS;
}

VOID
ArmPlatformInitializeSystemMemory (
  VOID
  )
{
}

STATIC ARM_CORE_INFO  mRk3576InfoTable[] = {
  { 0x0, 0x000 },             // Cluster 0, Core 0
  { 0x0, 0x100 },             // Cluster 0, Core 1
  { 0x0, 0x200 },             // Cluster 0, Core 2
  { 0x0, 0x300 },             // Cluster 0, Core 3
  { 0x0, 0x400 },             // Cluster 0, Core 4
  { 0x0, 0x500 },             // Cluster 0, Core 5
  { 0x0, 0x600 },             // Cluster 0, Core 6
  { 0x0, 0x700 },             // Cluster 0, Core 7
};

STATIC
EFI_STATUS
PrePeiCoreGetMpCoreInfo (
  OUT UINTN          *CoreCount,
  OUT ARM_CORE_INFO  **ArmCoreTable
  )
{
  // Only support one cluster
  *CoreCount    = sizeof (mRk3576InfoTable) / sizeof (ARM_CORE_INFO);
  *ArmCoreTable = mRk3576InfoTable;

  return EFI_SUCCESS;
}

STATIC ARM_MP_CORE_INFO_PPI    mMpCoreInfoPpi = {
  PrePeiCoreGetMpCoreInfo
};
STATIC EFI_PEI_PPI_DESCRIPTOR  mPlatformPpiTable[] = {
  {
    EFI_PEI_PPI_DESCRIPTOR_PPI,
    &gArmMpCoreInfoPpiGuid,
    &mMpCoreInfoPpi
  }
};

VOID
ArmPlatformGetPlatformPpiList (
  OUT UINTN                   *PpiListSize,
  OUT EFI_PEI_PPI_DESCRIPTOR  **PpiList
  )
{
  *PpiListSize = sizeof (mPlatformPpiTable);
  *PpiList     = mPlatformPpiTable;
}
