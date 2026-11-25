/** @file
 *
 *  Armbian (U-Boot): boot Armbian's own U-Boot once, then come back here.
 *
 *  This firmware is loaded by a U-Boot SPL.  With the SPL patch in
 *  uboot/v2026.10, that SPL boots a different U-Boot FIT (its own TF-A,
 *  U-Boot and U-Boot device tree) for exactly one boot when it finds a
 *  request record on the eMMC.  It erases the record before it loads
 *  anything, so the boot after that is ours again.  This application:
 *
 *    1. checks that the SPL on the board knows about requests,
 *    2. finds Armbian's U-Boot FIT:
 *         - on the SD card, at sector 0x4000 where Armbian writes it: the
 *           SPL can load it from there directly;
 *         - at the same sector of the eMMC, when this firmware runs from
 *           the SD card and Armbian is on the eMMC: also loaded directly;
 *         - otherwise on another disk (USB, NVMe) at the same sector, or in
 *           an Armbian root file system on any disk, as
 *           /usr/lib/linux-u-boot-* /u-boot-rockchip.bin: it is copied into
 *           a slot on the eMMC (sector 0x10000, 16 MiB), because the SPL
 *           cannot read USB or NVMe, and the eMMC's own sector 0x4000 is
 *           this firmware;
 *    3. writes the request to eMMC sector 0x3FFF and restarts.
 *
 *  On a board whose SPL and firmware are on SPI NOR and that has no eMMC
 *  (PcdArmbianOnceRequestOnSd, the Radxa ROCK 4D), the SPL is looked for in
 *  the SPI NOR, the only U-Boot it can boot is the FIT at sector 0x4000 of
 *  the SD card, and the request goes to sector 0x3FFF of that card.
 *
 *  Nothing on the chip survives a reset on these boards (DRAM, the PMU GRF
 *  OS registers and a battery-less RTC all come back cleared), so the
 *  request has to be on a disk.  docs/ARMBIAN-CHAINLOAD.md has the details.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/ArmbianOnceLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

#define SECTOR          ARMBIAN_ONCE_SECTOR
#define COPY_CHUNK      SIZE_256KB

STATIC_ASSERT (sizeof (ARMBIAN_ONCE_REQUEST) == SECTOR, "the request is one sector");

//
// Armbian writes u-boot-rockchip.bin at sector 64, so the FIT inside it,
// which lands on sector 0x4000, starts this far into the file.
//
#define UBOOT_ROCKCHIP_BIN_FIT_OFFSET  ((ARMBIAN_ONCE_FIT_LBA - 64) * SECTOR)

STATIC
VOID
WaitForKey (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          Index;

  Print (L"\nPress any key to go back.\n");
  gST->ConIn->Reset (gST->ConIn, FALSE);
  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
}


STATIC
EFI_STATUS
DiskWriteVerify (
  IN ARMBIAN_ONCE_DISK  *Disk,
  IN UINT64    Offset,
  IN UINTN     Size,
  IN VOID      *Buffer
  )
{
  EFI_STATUS  Status;
  VOID        *Check;

  Check = AllocatePool (Size);
  if (Check == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = Disk->DiskIo->WriteDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, Size, Buffer);
  if (!EFI_ERROR (Status)) {
    Status = Disk->BlockIo->FlushBlocks (Disk->BlockIo);
  }

  if (!EFI_ERROR (Status)) {
    Status = ArmbianOnceDiskRead (Disk, Offset, Size, Check);
  }

  if (!EFI_ERROR (Status) && (CompareMem (Check, Buffer, Size) != 0)) {
    Status = EFI_DEVICE_ERROR;
  }

  FreePool (Check);
  return Status;
}

/**
  ArmbianOnceSlotIsFree, saying why not.
**/
STATIC
BOOLEAN
SlotIsFree (
  IN ARMBIAN_ONCE_DISK  *Emmc
  )
{
  UINT64  Start;
  UINT64  End;

  if (ArmbianOnceSlotIsFree (Emmc, &Start, &End)) {
    return TRUE;
  }

  if (End == 0) {
    Print (L"The eMMC is too small for the slot at sector 0x%x.\n", ARMBIAN_ONCE_SLOT_LBA);
  } else {
    Print (L"The eMMC slot (sectors 0x%x..0x%x) is inside the partition at %lu..%lu.\n",
      ARMBIAN_ONCE_SLOT_LBA, ARMBIAN_ONCE_SLOT_LBA + ARMBIAN_ONCE_SLOT_SECTORS - 1, Start, End);
  }

  return FALSE;
}

/**
  Put the FIT in Buffer into the eMMC slot, unless it is already there.
**/
STATIC
EFI_STATUS
WriteSlot (
  IN ARMBIAN_ONCE_DISK  *Emmc,
  IN UINT8     *Buffer,
  IN UINTN     Size
  )
{
  EFI_STATUS  Status;
  UINT8       *Current;
  UINTN       Done;
  UINTN       Chunk;
  UINT64      Offset = MultU64x32 (ARMBIAN_ONCE_SLOT_LBA, SECTOR);

  if (Size > ARMBIAN_ONCE_SLOT_SECTORS * SECTOR) {
    Print (L"That U-Boot FIT is %lu bytes, the eMMC slot holds %u.\n", (UINT64)Size, ARMBIAN_ONCE_SLOT_SECTORS * SECTOR);
    return EFI_BAD_BUFFER_SIZE;
  }

  if (!SlotIsFree (Emmc)) {
    return EFI_ACCESS_DENIED;
  }

  Current = AllocatePool (Size);
  if (Current == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ArmbianOnceDiskRead (Emmc, Offset, Size, Current);
  if (!EFI_ERROR (Status) && (CompareMem (Current, Buffer, Size) == 0)) {
    Print (L"The eMMC slot already holds that U-Boot.\n");
    FreePool (Current);
    return EFI_SUCCESS;
  }

  FreePool (Current);

  Print (L"Copying it to the eMMC slot at sector 0x%x (%lu KiB)...\n", ARMBIAN_ONCE_SLOT_LBA, (UINT64)(Size / SIZE_1KB));
  for (Done = 0; Done < Size; Done += Chunk) {
    Chunk  = MIN (Size - Done, COPY_CHUNK);
    Status = DiskWriteVerify (Emmc, Offset + Done, Chunk, Buffer + Done);
    if (EFI_ERROR (Status)) {
      Print (L"Writing the eMMC at byte 0x%lx failed: %r\n", Offset + Done, Status);
      return Status;
    }
  }

  return EFI_SUCCESS;
}

/**
  Armbian's image layout on a disk the SPL cannot read (USB, NVMe): its
  U-Boot FIT at sector 0x4000.
**/
STATIC
EFI_STATUS
CopyFromOtherDisk (
  IN ARMBIAN_ONCE_DISK  *Emmc
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  ARMBIAN_ONCE_DISK    Disk;
  UINTN       Extent;
  UINT8       *Buffer;
  CHAR16      *Text;
  EFI_STATUS  Status = EFI_NOT_FOUND;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < Count && Status == EFI_NOT_FOUND; Index++) {
    if (  ArmbianOnceIsOnController (Handles[Index], FixedPcdGet32 (PcdDwcSdhciBaseAddress))
       || ArmbianOnceIsOnController (Handles[Index], FixedPcdGet32 (PcdRkSdmmcBaseAddress))
       || !ArmbianOnceOpenDisk (Handles[Index], &Disk)
       || (ArmbianOnceClassifyFitOnDisk (&Disk, ARMBIAN_ONCE_FIT_LBA, &Extent) != ArmbianOnceFitUBoot))
    {
      continue;
    }

    Text = ConvertDevicePathToText (Disk.DevicePath, TRUE, TRUE);
    Print (L"Found a U-Boot FIT on %s\n", Text != NULL ? Text : L"a disk");
    if (Text != NULL) {
      FreePool (Text);
    }

    Buffer = AllocatePool (Extent);
    if (Buffer == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    Status = ArmbianOnceDiskRead (&Disk, MultU64x32 (ARMBIAN_ONCE_FIT_LBA, SECTOR), Extent, Buffer);
    if (!EFI_ERROR (Status)) {
      Status = WriteSlot (Emmc, Buffer, Extent);
    }

    FreePool (Buffer);
  }

  FreePool (Handles);
  return Status;
}

STATIC
UINT64
TimeKey (
  IN EFI_TIME  *Time
  )
{
  return LShiftU64 (Time->Year, 40) | LShiftU64 (Time->Month, 32) | LShiftU64 (Time->Day, 24) |
         LShiftU64 (Time->Hour, 16) | LShiftU64 (Time->Minute, 8) | Time->Second;
}

STATIC
EFI_FILE_INFO *
GetInfo (
  IN EFI_FILE_PROTOCOL  *File
  )
{
  EFI_FILE_INFO  *Info = NULL;
  UINTN          Size  = 0;

  if (File->GetInfo (File, &gEfiFileInfoGuid, &Size, NULL) != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }

  Info = AllocatePool (Size);
  if ((Info != NULL) && EFI_ERROR (File->GetInfo (File, &gEfiFileInfoGuid, &Size, Info))) {
    FreePool (Info);
    Info = NULL;
  }

  return Info;
}

/**
  Armbian's U-Boot package installs u-boot-rockchip.bin (idbloader plus
  FIT, as written to sector 64) under /usr/lib/linux-u-boot-<branch>-<board>.
  Every Armbian upgrade of U-Boot replaces it, so copying it on every use is
  what keeps the slot in step.  The newest one on any volume wins.
**/
STATIC
EFI_STATUS
CopyFromRootFs (
  IN ARMBIAN_ONCE_DISK  *Emmc
  )
{
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *Dir;
  EFI_FILE_PROTOCOL                *File;
  EFI_FILE_INFO                    *Entry;
  EFI_FILE_INFO                    *Info;
  UINTN                            EntrySize;
  CHAR16                           Path[128];
  UINT8                            *Best     = NULL;
  UINTN                            BestSize  = 0;
  UINT64                           BestTime  = 0;
  CHAR16                           BestPath[128];
  UINT8                            *Data;
  UINTN                            Size;
  UINTN                            Extent;
  EFI_STATUS                       Status;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles))) {
    return EFI_NOT_FOUND;
  }

  Entry = AllocatePool (SIZE_4KB);
  if (Entry == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < Count; Index++) {
    if (  EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))
       || EFI_ERROR (Fs->OpenVolume (Fs, &Root)))
    {
      continue;
    }

    if (EFI_ERROR (Root->Open (Root, &Dir, L"\\usr\\lib", EFI_FILE_MODE_READ, 0))) {
      Root->Close (Root);
      continue;
    }

    for ( ; ;) {
      EntrySize = SIZE_4KB;
      if (EFI_ERROR (Dir->Read (Dir, &EntrySize, Entry)) || (EntrySize == 0)) {
        break;
      }

      if (  ((Entry->Attribute & EFI_FILE_DIRECTORY) == 0)
         || (StrnCmp (Entry->FileName, L"linux-u-boot-", 13) != 0)
         || (StrLen (Entry->FileName) > 80))
      {
        continue;
      }

      UnicodeSPrint (Path, sizeof (Path), L"\\usr\\lib\\%s\\u-boot-rockchip.bin", Entry->FileName);
      if (EFI_ERROR (Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0))) {
        continue;
      }

      Info = GetInfo (File);
      if (  (Info != NULL)
         && (Info->FileSize > UBOOT_ROCKCHIP_BIN_FIT_OFFSET)
         && (Info->FileSize <= UBOOT_ROCKCHIP_BIN_FIT_OFFSET + ARMBIAN_ONCE_SLOT_SECTORS * SECTOR)
         && ((Best == NULL) || (TimeKey (&Info->ModificationTime) > BestTime)))
      {
        Size = (UINTN)Info->FileSize;
        Data = AllocatePool (Size);
        if (  (Data != NULL)
           && !EFI_ERROR (File->Read (File, &Size, Data))
           && (Size == Info->FileSize)
           && (ArmbianOnceClassifyFit (Data + UBOOT_ROCKCHIP_BIN_FIT_OFFSET, Size - UBOOT_ROCKCHIP_BIN_FIT_OFFSET, &Extent) == ArmbianOnceFitUBoot)
           && (UBOOT_ROCKCHIP_BIN_FIT_OFFSET + Extent <= ALIGN_VALUE (Size, SECTOR)))
        {
          if (Best != NULL) {
            FreePool (Best);
          }

          //
          // The file need not end on a sector; the FIT's last image may.
          //
          Best = AllocateZeroPool (Extent);
          if (Best != NULL) {
            CopyMem (Best, Data + UBOOT_ROCKCHIP_BIN_FIT_OFFSET, MIN (Extent, Size - UBOOT_ROCKCHIP_BIN_FIT_OFFSET));
            BestSize = Extent;
            BestTime = TimeKey (&Info->ModificationTime);
            StrCpyS (BestPath, ARRAY_SIZE (BestPath), Path);
          }
        }

        if (Data != NULL) {
          FreePool (Data);
        }
      }

      if (Info != NULL) {
        FreePool (Info);
      }

      File->Close (File);
    }

    Dir->Close (Dir);
    Root->Close (Root);
  }

  FreePool (Entry);
  FreePool (Handles);

  if (Best == NULL) {
    return EFI_NOT_FOUND;
  }

  Print (L"Found Armbian's U-Boot in %s\n", BestPath);
  Status = WriteSlot (Emmc, Best, BestSize);
  FreePool (Best);
  return Status;
}

STATIC
EFI_STATUS
WriteRequest (
  IN ARMBIAN_ONCE_DISK  *Emmc,
  IN UINT32    Target,
  IN CONST CHAR16  *Name
  )
{
  ARMBIAN_ONCE_REQUEST  Request;
  ARMBIAN_ONCE_REQUEST  Old;
  UINT64                Offset = MultU64x32 (ARMBIAN_ONCE_REQUEST_LBA, SECTOR);

  if (  !EFI_ERROR (ArmbianOnceDiskRead (Emmc, Offset, sizeof (Old), &Old))
     && (CompareMem (Old.Magic, ARMBIAN_ONCE_MAGIC, sizeof (Old.Magic)) == 0))
  {
    Print (L"Note: an earlier request was still on the %s; the SPL did not take it.\n", Name);
  }

  ZeroMem (&Request, sizeof (Request));
  CopyMem (Request.Magic, ARMBIAN_ONCE_MAGIC, sizeof (Request.Magic));
  Request.Version    = ARMBIAN_ONCE_VERSION;
  Request.Target     = Target;
  Request.SlotSector = (Target == ARMBIAN_ONCE_TARGET_EMMC_SLOT) ? ARMBIAN_ONCE_SLOT_LBA : 0;
  Request.Crc32      = CalculateCrc32 (&Request, OFFSET_OF (ARMBIAN_ONCE_REQUEST, Crc32));

  return DiskWriteVerify (Emmc, Offset, sizeof (Request), &Request);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  ARMBIAN_ONCE_DISK    Emmc;
  ARMBIAN_ONCE_DISK    Sd;
  BOOLEAN     HaveSd;
  UINT32      Target;

  Print (L"\nArmbian (U-Boot): the next boot runs Armbian's own U-Boot, once.\n\n");

  if (FixedPcdGetBool (PcdArmbianOnceRequestOnSd)) {
    //
    // SPI NOR board: the SPL and this firmware are in the SPI NOR, and the
    // SPL reads the request from the SD card and boots only that card's
    // U-Boot FIT.
    //
    if (!ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdRkSdmmcBaseAddress), &Sd)) {
      Print (L"No SD card found. On this board Armbian has to be on the SD card.\n");
      WaitForKey ();
      return EFI_NOT_FOUND;
    }

    if (!ArmbianOnceSpiSplTakesRequests ()) {
      Print (L"The SPL in the SPI NOR does not take requests (no \"%a\" in it).\n", ARMBIAN_ONCE_SPL_TAG);
      Print (L"It needs the SPL built with uboot/v2026.10 of this firmware.\n");
      WaitForKey ();
      return EFI_UNSUPPORTED;
    }

    if (ArmbianOnceClassifyFitOnDisk (&Sd, ARMBIAN_ONCE_FIT_LBA, NULL) != ArmbianOnceFitUBoot) {
      Print (L"No U-Boot FIT at sector 0x4000 of the SD card. On this board the SPL\n");
      Print (L"can only boot an Armbian U-Boot written to the SD card.\n");
      WaitForKey ();
      return EFI_NOT_FOUND;
    }

    Print (L"Found a U-Boot FIT on the SD card.\n");
    Status = WriteRequest (&Sd, ARMBIAN_ONCE_TARGET_SD, L"SD card");
    if (EFI_ERROR (Status)) {
      Print (L"Writing the request to SD card sector 0x%x failed: %r\n", ARMBIAN_ONCE_REQUEST_LBA, Status);
      WaitForKey ();
      return Status;
    }

    Print (L"Request written. Restarting; the boot after Armbian's is this firmware again.\n");
    gBS->Stall (1000 * 1000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    return EFI_SUCCESS;
  }

  if (!ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdDwcSdhciBaseAddress), &Emmc)) {
    Print (L"No eMMC found. The request for the SPL is kept on the eMMC.\n");
    WaitForKey ();
    return EFI_NOT_FOUND;
  }

  HaveSd = ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdRkSdmmcBaseAddress), &Sd);

  //
  // The BootROM of these boards loads the SPL from the eMMC (from the SD
  // card only when the eMMC has none), and the SPL reads the request from
  // the eMMC either way.
  //
  if (!ArmbianOnceSplTakesRequests (&Emmc) && !(HaveSd && ArmbianOnceSplTakesRequests (&Sd))) {
    Print (L"The SPL on this board does not take requests (no \"%a\" in it).\n", ARMBIAN_ONCE_SPL_TAG);
    Print (L"It needs the SPL built with uboot/v2026.10 of this firmware.\n");
    WaitForKey ();
    return EFI_UNSUPPORTED;
  }

  if (HaveSd && (ArmbianOnceClassifyFitOnDisk (&Sd, ARMBIAN_ONCE_FIT_LBA, NULL) == ArmbianOnceFitUBoot)) {
    Print (L"Found a U-Boot FIT on the SD card.\n");
    Target = ARMBIAN_ONCE_TARGET_SD;
  } else if (ArmbianOnceClassifyFitOnDisk (&Emmc, ARMBIAN_ONCE_FIT_LBA, NULL) == ArmbianOnceFitUBoot) {
    Print (L"Found a U-Boot FIT on the eMMC (this firmware runs from the SD card).\n");
    Target = ARMBIAN_ONCE_TARGET_EMMC_FIT;
  } else {
    Status = CopyFromOtherDisk (&Emmc);
    if (Status == EFI_NOT_FOUND) {
      Status = CopyFromRootFs (&Emmc);
    }

    //
    // Nothing to copy from any more, but an earlier use left a U-Boot in the
    // slot: boot that one.  The boot manager shows this option for it too.
    //
    if (  (Status == EFI_NOT_FOUND)
       && ArmbianOnceSlotIsFree (&Emmc, NULL, NULL)
       && (ArmbianOnceClassifyFitOnDisk (&Emmc, ARMBIAN_ONCE_SLOT_LBA, NULL) == ArmbianOnceFitUBoot))
    {
      Print (L"Using the U-Boot copied to the eMMC slot earlier (its source is not here now).\n");
      Status = EFI_SUCCESS;
    }

    if (Status == EFI_NOT_FOUND) {
      Print (L"No Armbian U-Boot found: none at sector 0x4000 of the SD card,\n");
      Print (L"the eMMC or a USB or NVMe disk, and no\n");
      Print (L"/usr/lib/linux-u-boot-*/u-boot-rockchip.bin on any volume.\n");
    }

    if (EFI_ERROR (Status)) {
      WaitForKey ();
      return Status;
    }

    Target = ARMBIAN_ONCE_TARGET_EMMC_SLOT;
  }

  Status = WriteRequest (&Emmc, Target, L"eMMC");
  if (EFI_ERROR (Status)) {
    Print (L"Writing the request to eMMC sector 0x%x failed: %r\n", ARMBIAN_ONCE_REQUEST_LBA, Status);
    WaitForKey ();
    return Status;
  }

  Print (L"Request written. Restarting; the boot after Armbian's is this firmware again.\n");
  gBS->Stall (1000 * 1000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  return EFI_SUCCESS;
}
