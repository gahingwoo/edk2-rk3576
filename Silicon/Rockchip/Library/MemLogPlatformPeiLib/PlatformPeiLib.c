/** @file

  Rockchip copy of ArmPlatformPkg/PlatformPei/PlatformPeiLib.c.  The one
  change: PlatformPeim () also sets up the memory debug log (OvmfPkg
  MemDebugLogLib), the job MemDebugLogPei does on a platform with PEI.  It
  allocates the buffer, writes its header, and leaves its address in a
  gMemDebugLogHobGuid HOB, where every DXE module's MemLogDebugLib finds it.
  So the log holds everything printed from the DXE core on; what PeilessSec
  prints before has no buffer to go to.


  Copyright (c) 2011-2014, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>

#include <Library/ArmPlatformLib.h>
#include <Library/ArmTransferListLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemDebugLogLib.h>

/**
  Build event log hob consumed by Tcg2Dxe.

  @param[in]  TransferList  Transfer list header

  @return EFI_SUCCESS       Success to generate HOBs from passed event log.
  @return Others            Failed to create event log hob

**/
EFI_STATUS
EFIAPI
BuildPeiEventLogHobs (
  IN TRANSFER_LIST_HEADER  *TransferList
  );

/**
  The memory debug log buffer and the HOB that passes it to DXE.  A failure
  only means no log.
**/
STATIC
VOID
MemLogSetUp (
  VOID
  )
{
  UINTN                   Pages;
  VOID                    *Buffer;
  MEM_DEBUG_LOG_HOB_DATA  HobData;

  Pages = MIN (FixedPcdGet32 (PcdMemDebugLogPages), MAX_MEM_DEBUG_LOG_PAGES);
  if (Pages == 0) {
    return;
  }

  Buffer = AllocatePages (Pages);
  if (Buffer == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: no memory for a %u page debug log\n", __func__, (UINT32)Pages));
    return;
  }

  if (EFI_ERROR (MemDebugLogInit ((EFI_PHYSICAL_ADDRESS)(UINTN)Buffer, (UINT32)EFI_PAGES_TO_SIZE (Pages)))) {
    return;
  }

  HobData.MemDebugLogBufAddr = (EFI_PHYSICAL_ADDRESS)(UINTN)Buffer;
  BuildGuidDataHob (&gMemDebugLogHobGuid, &HobData, sizeof (HobData));
  DEBUG ((DEBUG_INFO, "%a: debug log in memory, %u KB at 0x%lx\n", __func__, (UINT32)(Pages * 4), HobData.MemDebugLogBufAddr));
}

EFI_STATUS
EFIAPI
PlatformPeim (
  VOID
  )
{
  EFI_STATUS            Status;
  TRANSFER_LIST_HEADER  *TransferList;

  MemLogSetUp ();

  BuildFvHob (PcdGet64 (PcdFvBaseAddress), PcdGet32 (PcdFvSize));

  Status = TransferListGetFromHobList (&TransferList);
  if (EFI_ERROR (Status)) {
    // Transfer list is not found, this means
    // early event log is not available and
    // measured boot may not be enabled.
    return EFI_SUCCESS;
  }

  if (TransferListCheckHeader (TransferList) == TRANSFER_LIST_OPS_INVALID) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid transfer list... Stop boot...\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  // Create HOBs for the early measurement Event logs.
  Status = BuildPeiEventLogHobs (TransferList);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to build event log Pei Hobs...: %r\n", __func__, Status));
    return Status;
  }

  return EFI_SUCCESS;
}
