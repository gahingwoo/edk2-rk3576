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

#include "ArmbianOnce.h"

#define SECTOR          512
#define FIT_HEADER_MAX  SIZE_64KB
#define COPY_CHUNK      SIZE_256KB

STATIC_ASSERT (sizeof (ARMBIAN_ONCE_REQUEST) == SECTOR, "the request is one sector");

//
// Armbian writes u-boot-rockchip.bin at sector 64, so the FIT inside it,
// which lands on sector 0x4000, starts this far into the file.
//
#define UBOOT_ROCKCHIP_BIN_FIT_OFFSET  ((ARMBIAN_ONCE_FIT_LBA - 64) * SECTOR)

//
// The partition type package.sh gives the firmware region on the eMMC.
//
STATIC CONST EFI_GUID  mFirmwarePartitionType = {
  0x8DA63339, 0x0007, 0x60C0, { 0xC4, 0x36, 0x08, 0x3A, 0xC8, 0x23, 0x09, 0x08 }
};

typedef enum {
  FitNone,
  FitFirmware,    // has /images/edk2: this firmware
  FitForeign      // another FIT: a U-Boot one, as far as we are concerned
} FIT_KIND;

typedef struct {
  EFI_HANDLE                  Handle;
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;
  EFI_BLOCK_IO_PROTOCOL       *BlockIo;
  EFI_DISK_IO_PROTOCOL        *DiskIo;
} RAW_DISK;

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

/**
  TRUE if Handle is on the Rockchip MMC controller at BaseAddress.  Same
  test as RkFvbDxe: the device path starts at the NonDiscoverableDevice
  node of the controller.
**/
STATIC
BOOLEAN
IsOnController (
  IN EFI_HANDLE  Handle,
  IN UINT64      BaseAddress
  )
{
  EFI_STATUS                         Status;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;
  EFI_HANDLE                         DeviceHandle;
  NON_DISCOVERABLE_DEVICE            *Device;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Descriptor;

  DevicePath = DevicePathFromHandle (Handle);
  if (  (DevicePath == NULL)
     || (DevicePathType (DevicePath) != HARDWARE_DEVICE_PATH)
     || (DevicePathSubType (DevicePath) != HW_VENDOR_DP))
  {
    return FALSE;
  }

  Status = gBS->LocateDevicePath (&gEdkiiNonDiscoverableDeviceProtocolGuid, &DevicePath, &DeviceHandle);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = gBS->HandleProtocol (DeviceHandle, &gEdkiiNonDiscoverableDeviceProtocolGuid, (VOID **)&Device);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Descriptor = &Device->Resources[0];
  return (Descriptor->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) &&
         (Descriptor->ResType == ACPI_ADDRESS_SPACE_TYPE_MEM) &&
         (Descriptor->AddrRangeMin == BaseAddress);
}

/**
  TRUE if the device path ends in an eMMC hardware partition other than the
  user data area (boot1, boot2, general purpose ones): EmmcDxe ends those
  in Ctrl(n) with n > 0.
**/
STATIC
BOOLEAN
IsEmmcHwPartition (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Last = NULL;

  for ( ; !IsDevicePathEnd (DevicePath); DevicePath = NextDevicePathNode (DevicePath)) {
    Last = DevicePath;
  }

  return (Last != NULL) &&
         (DevicePathType (Last) == HARDWARE_DEVICE_PATH) &&
         (DevicePathSubType (Last) == HW_CONTROLLER_DP) &&
         (((CONTROLLER_DEVICE_PATH *)Last)->ControllerNumber != 0);
}

STATIC
BOOLEAN
OpenRawDisk (
  IN  EFI_HANDLE  Handle,
  OUT RAW_DISK    *Disk
  )
{
  ZeroMem (Disk, sizeof (*Disk));
  Disk->Handle     = Handle;
  Disk->DevicePath = DevicePathFromHandle (Handle);

  if (  (Disk->DevicePath == NULL)
     || EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&Disk->BlockIo))
     || EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDiskIoProtocolGuid, (VOID **)&Disk->DiskIo)))
  {
    return FALSE;
  }

  return Disk->BlockIo->Media->MediaPresent &&
         !Disk->BlockIo->Media->LogicalPartition &&
         !IsEmmcHwPartition (Disk->DevicePath);
}

/**
  Find the whole-disk handle (user data area) on the MMC controller at
  BaseAddress.
**/
STATIC
BOOLEAN
FindMmcDisk (
  IN  UINT64    BaseAddress,
  OUT RAW_DISK  *Disk
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  BOOLEAN     Found = FALSE;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    return FALSE;
  }

  for (Index = 0; Index < Count && !Found; Index++) {
    Found = IsOnController (Handles[Index], BaseAddress) && OpenRawDisk (Handles[Index], Disk);
  }

  FreePool (Handles);
  return Found;
}

STATIC
EFI_STATUS
DiskRead (
  IN  RAW_DISK  *Disk,
  IN  UINT64    Offset,
  IN  UINTN     Size,
  OUT VOID      *Buffer
  )
{
  return Disk->DiskIo->ReadDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, Size, Buffer);
}

STATIC
EFI_STATUS
DiskWriteVerify (
  IN RAW_DISK  *Disk,
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
    Status = DiskRead (Disk, Offset, Size, Check);
  }

  if (!EFI_ERROR (Status) && (CompareMem (Check, Buffer, Size) != 0)) {
    Status = EFI_DEVICE_ERROR;
  }

  FreePool (Check);
  return Status;
}

/**
  What kind of FIT starts at Header, and how many bytes it spans with its
  external data.

  @param[in]  Header   at least the FIT's device tree, up to FIT_HEADER_MAX
  @param[in]  Size     bytes valid at Header
  @param[out] Extent   bytes from the start of the FIT to the end of the
                       last image in it, rounded up to a sector
**/
STATIC
FIT_KIND
ClassifyFit (
  IN  VOID   *Header,
  IN  UINTN  Size,
  OUT UINTN  *Extent  OPTIONAL
  )
{
  UINT32        Total;
  INT32         Images;
  INT32         Node;
  INT32         Len;
  CONST UINT32  *Prop;
  UINT64        Start;
  UINT64        DataSize;
  UINT64        Max;

  if ((Size < sizeof (FDT_HEADER)) || (FdtCheckHeader (Header) != 0)) {
    return FitNone;
  }

  Total = FdtTotalSize (Header);
  if ((Total > Size) || (Total > FIT_HEADER_MAX)) {
    return FitNone;
  }

  Images = FdtPathOffset (Header, "/images");
  if (Images < 0) {
    return FitNone;
  }

  if (FdtSubnodeOffset (Header, Images, "edk2") >= 0) {
    return FitFirmware;
  }

  //
  // mkimage -E puts each image's data after the device tree: at an absolute
  // data-position, or at data-offset from the 4-byte aligned end of the tree.
  //
  Max = Total;
  for (Node = FdtFirstSubnode (Header, Images); Node >= 0; Node = FdtNextSubnode (Header, Node)) {
    Prop = FdtGetProp (Header, Node, "data-size", &Len);
    if ((Prop == NULL) || (Len != sizeof (UINT32))) {
      continue;     // data inside the tree
    }

    DataSize = Fdt32ToCpu (*Prop);
    Prop     = FdtGetProp (Header, Node, "data-position", &Len);
    if ((Prop != NULL) && (Len == sizeof (UINT32))) {
      Start = Fdt32ToCpu (*Prop);
    } else {
      Prop = FdtGetProp (Header, Node, "data-offset", &Len);
      if ((Prop == NULL) || (Len != sizeof (UINT32))) {
        return FitNone;
      }

      Start = ALIGN_VALUE ((UINT64)Total, 4) + Fdt32ToCpu (*Prop);
    }

    Max = MAX (Max, Start + DataSize);
  }

  if (Extent != NULL) {
    *Extent = (UINTN)ALIGN_VALUE (Max, SECTOR);
  }

  return FitForeign;
}

STATIC
FIT_KIND
ClassifyFitOnDisk (
  IN  RAW_DISK  *Disk,
  IN  UINT64    Lba,
  OUT UINTN     *Extent  OPTIONAL
  )
{
  VOID      *Header;
  FIT_KIND  Kind = FitNone;
  UINT64    Offset;
  UINT64    DiskBytes;

  Offset    = MultU64x32 (Lba, SECTOR);
  DiskBytes = MultU64x32 (Disk->BlockIo->Media->LastBlock + 1, Disk->BlockIo->Media->BlockSize);
  if (Offset + FIT_HEADER_MAX > DiskBytes) {
    return FitNone;
  }

  Header = AllocatePool (FIT_HEADER_MAX);
  if (Header == NULL) {
    return FitNone;
  }

  if (!EFI_ERROR (DiskRead (Disk, Offset, FIT_HEADER_MAX, Header))) {
    Kind = ClassifyFit (Header, FIT_HEADER_MAX, Extent);
  }

  FreePool (Header);
  return Kind;
}

/**
  TRUE if the idbloader at sector 64 of Disk has an SPL that takes requests.
**/
STATIC
BOOLEAN
SplTakesRequests (
  IN RAW_DISK  *Disk
  )
{
  CONST CHAR8  *Tag    = ARMBIAN_ONCE_SPL_TAG;
  UINTN        TagSize = AsciiStrLen (ARMBIAN_ONCE_SPL_TAG);
  UINTN        Size    = SIZE_1MB;
  UINT8        *Buffer;
  UINTN        Index;
  BOOLEAN      Found = FALSE;

  Buffer = AllocatePool (Size);
  if (Buffer == NULL) {
    return FALSE;
  }

  if (!EFI_ERROR (DiskRead (Disk, 64 * SECTOR, Size, Buffer))) {
    for (Index = 0; Index + TagSize <= Size && !Found; Index++) {
      Found = (Buffer[Index] == Tag[0]) && (CompareMem (Buffer + Index, Tag, TagSize) == 0);
    }
  }

  FreePool (Buffer);
  return Found;
}

/**
  The slot must not be inside a partition, other than the firmware region
  package.sh declares.
**/
STATIC
BOOLEAN
SlotIsFree (
  IN RAW_DISK  *Emmc
  )
{
  EFI_HANDLE                   *Handles;
  UINTN                        Count;
  UINTN                        Index;
  EFI_PARTITION_INFO_PROTOCOL  *Info;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;
  UINTN                        PrefixSize;
  UINT64                       Start;
  UINT64                       End;
  BOOLEAN                      Free = TRUE;

  if (ARMBIAN_ONCE_SLOT_LBA + ARMBIAN_ONCE_SLOT_SECTORS > Emmc->BlockIo->Media->LastBlock + 1) {
    return FALSE;
  }

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles))) {
    return TRUE;
  }

  PrefixSize = GetDevicePathSize (Emmc->DevicePath) - END_DEVICE_PATH_LENGTH;
  for (Index = 0; Index < Count && Free; Index++) {
    DevicePath = DevicePathFromHandle (Handles[Index]);
    if (  (DevicePath == NULL)
       || (GetDevicePathSize (DevicePath) <= PrefixSize)
       || (CompareMem (DevicePath, Emmc->DevicePath, PrefixSize) != 0)
       || EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPartitionInfoProtocolGuid, (VOID **)&Info)))
    {
      continue;
    }

    if (Info->Type == PARTITION_TYPE_GPT) {
      if (CompareGuid (&Info->Info.Gpt.PartitionTypeGUID, &mFirmwarePartitionType)) {
        continue;
      }

      Start = Info->Info.Gpt.StartingLBA;
      End   = Info->Info.Gpt.EndingLBA + 1;
    } else if (Info->Type == PARTITION_TYPE_MBR) {
      Start = ReadUnaligned32 ((UINT32 *)Info->Info.Mbr.StartingLBA);
      End   = Start + ReadUnaligned32 ((UINT32 *)Info->Info.Mbr.SizeInLBA);
    } else {
      continue;
    }

    if ((Start < ARMBIAN_ONCE_SLOT_LBA + ARMBIAN_ONCE_SLOT_SECTORS) && (End > ARMBIAN_ONCE_SLOT_LBA)) {
      Print (L"The eMMC slot (sectors 0x%x..0x%x) is inside the partition at %lu..%lu.\n",
        ARMBIAN_ONCE_SLOT_LBA, ARMBIAN_ONCE_SLOT_LBA + ARMBIAN_ONCE_SLOT_SECTORS - 1, Start, End - 1);
      Free = FALSE;
    }
  }

  FreePool (Handles);
  return Free;
}

/**
  Put the FIT in Buffer into the eMMC slot, unless it is already there.
**/
STATIC
EFI_STATUS
WriteSlot (
  IN RAW_DISK  *Emmc,
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

  Status = DiskRead (Emmc, Offset, Size, Current);
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
  IN RAW_DISK  *Emmc
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  RAW_DISK    Disk;
  UINTN       Extent;
  UINT8       *Buffer;
  CHAR16      *Text;
  EFI_STATUS  Status = EFI_NOT_FOUND;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < Count && Status == EFI_NOT_FOUND; Index++) {
    if (  IsOnController (Handles[Index], FixedPcdGet32 (PcdDwcSdhciBaseAddress))
       || IsOnController (Handles[Index], FixedPcdGet32 (PcdRkSdmmcBaseAddress))
       || !OpenRawDisk (Handles[Index], &Disk)
       || (ClassifyFitOnDisk (&Disk, ARMBIAN_ONCE_FIT_LBA, &Extent) != FitForeign))
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

    Status = DiskRead (&Disk, MultU64x32 (ARMBIAN_ONCE_FIT_LBA, SECTOR), Extent, Buffer);
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
  IN RAW_DISK  *Emmc
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
           && (ClassifyFit (Data + UBOOT_ROCKCHIP_BIN_FIT_OFFSET, Size - UBOOT_ROCKCHIP_BIN_FIT_OFFSET, &Extent) == FitForeign)
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
  IN RAW_DISK  *Emmc,
  IN UINT32    Target
  )
{
  ARMBIAN_ONCE_REQUEST  Request;
  ARMBIAN_ONCE_REQUEST  Old;
  UINT64                Offset = MultU64x32 (ARMBIAN_ONCE_REQUEST_LBA, SECTOR);

  if (  !EFI_ERROR (DiskRead (Emmc, Offset, sizeof (Old), &Old))
     && (CompareMem (Old.Magic, ARMBIAN_ONCE_MAGIC, sizeof (Old.Magic)) == 0))
  {
    Print (L"Note: an earlier request was still on the eMMC; the SPL did not take it.\n");
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
  RAW_DISK    Emmc;
  RAW_DISK    Sd;
  BOOLEAN     HaveSd;
  UINT32      Target;

  Print (L"\nArmbian (U-Boot): the next boot runs Armbian's own U-Boot, once.\n\n");

  if (!FindMmcDisk (FixedPcdGet32 (PcdDwcSdhciBaseAddress), &Emmc)) {
    Print (L"No eMMC found. The request for the SPL is kept on the eMMC.\n");
    WaitForKey ();
    return EFI_NOT_FOUND;
  }

  HaveSd = FindMmcDisk (FixedPcdGet32 (PcdRkSdmmcBaseAddress), &Sd);

  //
  // The BootROM of these boards loads the SPL from the eMMC (from the SD
  // card only when the eMMC has none), and the SPL reads the request from
  // the eMMC either way.
  //
  if (!SplTakesRequests (&Emmc) && !(HaveSd && SplTakesRequests (&Sd))) {
    Print (L"The SPL on this board does not take requests (no \"%a\" in it).\n", ARMBIAN_ONCE_SPL_TAG);
    Print (L"It needs the SPL built with uboot/v2026.10 of this firmware.\n");
    WaitForKey ();
    return EFI_UNSUPPORTED;
  }

  if (HaveSd && (ClassifyFitOnDisk (&Sd, ARMBIAN_ONCE_FIT_LBA, NULL) == FitForeign)) {
    Print (L"Found a U-Boot FIT on the SD card.\n");
    Target = ARMBIAN_ONCE_TARGET_SD;
  } else if (ClassifyFitOnDisk (&Emmc, ARMBIAN_ONCE_FIT_LBA, NULL) == FitForeign) {
    Print (L"Found a U-Boot FIT on the eMMC (this firmware runs from the SD card).\n");
    Target = ARMBIAN_ONCE_TARGET_EMMC_FIT;
  } else {
    Status = CopyFromOtherDisk (&Emmc);
    if (Status == EFI_NOT_FOUND) {
      Status = CopyFromRootFs (&Emmc);
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

  Status = WriteRequest (&Emmc, Target);
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
