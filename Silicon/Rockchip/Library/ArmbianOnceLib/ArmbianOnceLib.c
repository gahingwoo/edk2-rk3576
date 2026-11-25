/** @file
 *
 *  Finding a U-Boot for "Armbian (U-Boot)".  See ArmbianOnceLib.h and
 *  docs/ARMBIAN-CHAINLOAD.md.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/ArmbianOnceLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/NorFlashProtocol.h>
#include <Protocol/PartitionInfo.h>

#define SECTOR          ARMBIAN_ONCE_SECTOR
#define FIT_HEADER_MAX  ARMBIAN_ONCE_FIT_HEADER_MAX

//
// The partition type package.sh gives the firmware region on the eMMC.
//
STATIC CONST EFI_GUID  mFirmwarePartitionType = {
  0x8DA63339, 0x0007, 0x60C0, { 0xC4, 0x36, 0x08, 0x3A, 0xC8, 0x23, 0x09, 0x08 }
};

STATIC
BOOLEAN
IsControllerAt (
  IN EFI_HANDLE  DeviceHandle,
  IN UINT64      BaseAddress
  )
{
  NON_DISCOVERABLE_DEVICE            *Device;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Descriptor;

  if (EFI_ERROR (gBS->HandleProtocol (DeviceHandle, &gEdkiiNonDiscoverableDeviceProtocolGuid, (VOID **)&Device))) {
    return FALSE;
  }

  Descriptor = &Device->Resources[0];
  return (Descriptor->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) &&
         (Descriptor->ResType == ACPI_ADDRESS_SPACE_TYPE_MEM) &&
         (Descriptor->AddrRangeMin == BaseAddress);
}

/**
  Same test as RkFvbDxe: the device path starts at the NonDiscoverableDevice
  node of the controller.
**/
BOOLEAN
EFIAPI
ArmbianOnceIsOnController (
  IN EFI_HANDLE  Handle,
  IN UINT64      BaseAddress
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_HANDLE                DeviceHandle;

  DevicePath = DevicePathFromHandle (Handle);
  if (  (DevicePath == NULL)
     || (DevicePathType (DevicePath) != HARDWARE_DEVICE_PATH)
     || (DevicePathSubType (DevicePath) != HW_VENDOR_DP))
  {
    return FALSE;
  }

  if (EFI_ERROR (gBS->LocateDevicePath (&gEdkiiNonDiscoverableDeviceProtocolGuid, &DevicePath, &DeviceHandle))) {
    return FALSE;
  }

  return IsControllerAt (DeviceHandle, BaseAddress);
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

BOOLEAN
EFIAPI
ArmbianOnceOpenDisk (
  IN  EFI_HANDLE         Handle,
  OUT ARMBIAN_ONCE_DISK  *Disk
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

BOOLEAN
EFIAPI
ArmbianOnceFindMmcDisk (
  IN  UINT64             BaseAddress,
  OUT ARMBIAN_ONCE_DISK  *Disk
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
    Found = ArmbianOnceIsOnController (Handles[Index], BaseAddress) &&
            ArmbianOnceOpenDisk (Handles[Index], Disk);
  }

  FreePool (Handles);
  return Found;
}

EFI_STATUS
EFIAPI
ArmbianOnceDiskRead (
  IN  ARMBIAN_ONCE_DISK  *Disk,
  IN  UINT64             Offset,
  IN  UINTN              Size,
  OUT VOID               *Buffer
  )
{
  return Disk->DiskIo->ReadDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, Size, Buffer);
}

STATIC
BOOLEAN
PropIs (
  IN CONST VOID   *Fdt,
  IN INT32        Node,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Value
  )
{
  CONST CHAR8  *Prop;
  INT32        Len;

  Prop = FdtGetProp (Fdt, Node, Name, &Len);
  return (Prop != NULL) && (Len == (INT32)AsciiStrSize (Value)) &&
         (CompareMem (Prop, Value, Len) == 0);
}

ARMBIAN_ONCE_FIT_KIND
EFIAPI
ArmbianOnceClassifyFit (
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
  BOOLEAN       UBoot = FALSE;

  if ((Size < sizeof (FDT_HEADER)) || (FdtCheckHeader (Header) != 0)) {
    return ArmbianOnceFitNone;
  }

  Total = FdtTotalSize (Header);
  if ((Total > Size) || (Total > FIT_HEADER_MAX)) {
    return ArmbianOnceFitNone;
  }

  Images = FdtPathOffset (Header, "/images");
  if (Images < 0) {
    return ArmbianOnceFitNone;
  }

  if (FdtSubnodeOffset (Header, Images, "edk2") >= 0) {
    return ArmbianOnceFitFirmware;
  }

  //
  // mkimage -E puts each image's data after the device tree: at an absolute
  // data-position, or at data-offset from the 4-byte aligned end of the tree.
  //
  Max = Total;
  for (Node = FdtFirstSubnode (Header, Images); Node >= 0; Node = FdtNextSubnode (Header, Node)) {
    if (FdtGetProp (Header, Node, "arch", &Len) != NULL) {
      if (!PropIs (Header, Node, "arch", "arm64")) {
        return ArmbianOnceFitNone;     // built for another CPU
      }

      if (PropIs (Header, Node, "os", "u-boot")) {
        UBoot = TRUE;
      }
    }

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
        return ArmbianOnceFitNone;
      }

      Start = ALIGN_VALUE ((UINT64)Total, 4) + Fdt32ToCpu (*Prop);
    }

    Max = MAX (Max, Start + DataSize);
  }

  if (!UBoot) {
    return ArmbianOnceFitNone;
  }

  if (Extent != NULL) {
    *Extent = (UINTN)ALIGN_VALUE (Max, SECTOR);
  }

  return ArmbianOnceFitUBoot;
}

ARMBIAN_ONCE_FIT_KIND
EFIAPI
ArmbianOnceClassifyFitOnDisk (
  IN  ARMBIAN_ONCE_DISK  *Disk,
  IN  UINT64             Lba,
  OUT UINTN              *Extent  OPTIONAL
  )
{
  VOID                   *Header;
  ARMBIAN_ONCE_FIT_KIND  Kind = ArmbianOnceFitNone;
  UINT64                 Offset;
  UINT64                 DiskBytes;

  Offset    = MultU64x32 (Lba, SECTOR);
  DiskBytes = MultU64x32 (Disk->BlockIo->Media->LastBlock + 1, Disk->BlockIo->Media->BlockSize);
  if (Offset + FIT_HEADER_MAX > DiskBytes) {
    return ArmbianOnceFitNone;
  }

  Header = AllocatePool (FIT_HEADER_MAX);
  if (Header == NULL) {
    return ArmbianOnceFitNone;
  }

  if (!EFI_ERROR (ArmbianOnceDiskRead (Disk, Offset, FIT_HEADER_MAX, Header))) {
    Kind = ArmbianOnceClassifyFit (Header, FIT_HEADER_MAX, Extent);
  }

  FreePool (Header);
  return Kind;
}

BOOLEAN
EFIAPI
ArmbianOnceSplTakesRequests (
  IN ARMBIAN_ONCE_DISK  *Disk
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

  if (!EFI_ERROR (ArmbianOnceDiskRead (Disk, 64 * SECTOR, Size, Buffer))) {
    for (Index = 0; Index + TagSize <= Size && !Found; Index++) {
      Found = (Buffer[Index] == Tag[0]) && (CompareMem (Buffer + Index, Tag, TagSize) == 0);
    }
  }

  FreePool (Buffer);
  return Found;
}

//
// Where package.sh puts the idbloader in an SPI NOR image, and how far it
// may reach: the FIT starts at 0x60000.
//
#define SPI_IDB_OFFSET  0x8000
#define SPI_IDB_SIZE    (0x60000 - SPI_IDB_OFFSET)

BOOLEAN
EFIAPI
ArmbianOnceSpiSplTakesRequests (
  VOID
  )
{
  CONST CHAR8             *Tag    = ARMBIAN_ONCE_SPL_TAG;
  UINTN                   TagSize = AsciiStrLen (ARMBIAN_ONCE_SPL_TAG);
  UNI_NOR_FLASH_PROTOCOL  *Nor;
  UINT8                   *Buffer;
  UINTN                   Index;
  BOOLEAN                 Found = FALSE;

  if (EFI_ERROR (gBS->LocateProtocol (&gUniNorFlashProtocolGuid, NULL, (VOID **)&Nor))) {
    return FALSE;
  }

  Buffer = AllocatePool (SPI_IDB_SIZE);
  if (Buffer == NULL) {
    return FALSE;
  }

  if (!EFI_ERROR (Nor->Read (Nor, SPI_IDB_OFFSET, Buffer, SPI_IDB_SIZE))) {
    for (Index = 0; Index + TagSize <= SPI_IDB_SIZE && !Found; Index++) {
      Found = (Buffer[Index] == Tag[0]) && (CompareMem (Buffer + Index, Tag, TagSize) == 0);
    }
  }

  FreePool (Buffer);
  return Found;
}

BOOLEAN
EFIAPI
ArmbianOnceSlotIsFree (
  IN  ARMBIAN_ONCE_DISK  *Emmc,
  OUT UINT64             *PartStart  OPTIONAL,
  OUT UINT64             *PartEnd    OPTIONAL
  )
{
  EFI_HANDLE                   *Handles;
  UINTN                        Count;
  UINTN                        Index;
  EFI_PARTITION_INFO_PROTOCOL  *Info;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;
  UINTN                        PrefixSize;
  UINT64                       Start = 0;
  UINT64                       End   = 0;
  BOOLEAN                      Free  = TRUE;

  if (ARMBIAN_ONCE_SLOT_LBA + ARMBIAN_ONCE_SLOT_SECTORS > Emmc->BlockIo->Media->LastBlock + 1) {
    Free = FALSE;
    goto Done;
  }

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid, NULL, &Count, &Handles))) {
    goto Done;
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
      Free = FALSE;
    }
  }

  FreePool (Handles);

Done:
  if (!Free) {
    if (PartStart != NULL) {
      *PartStart = Start;
    }

    if (PartEnd != NULL) {
      *PartEnd = (End != 0) ? End - 1 : 0;
    }
  }

  return Free;
}

VOID
EFIAPI
ArmbianOnceConnectSd (
  VOID
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEdkiiNonDiscoverableDeviceProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (IsControllerAt (Handles[Index], FixedPcdGet32 (PcdRkSdmmcBaseAddress))) {
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
      break;
    }
  }

  FreePool (Handles);
}

/**
  A U-Boot FIT at sector 0x4000 of a whole disk that is neither the eMMC
  nor the SD card (USB, NVMe).
**/
STATIC
BOOLEAN
OtherDiskHasUBoot (
  VOID
  )
{
  EFI_HANDLE         *Handles;
  UINTN              Count;
  UINTN              Index;
  ARMBIAN_ONCE_DISK  Disk;
  BOOLEAN            Found = FALSE;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    return FALSE;
  }

  for (Index = 0; Index < Count && !Found; Index++) {
    Found = !ArmbianOnceIsOnController (Handles[Index], FixedPcdGet32 (PcdDwcSdhciBaseAddress)) &&
            !ArmbianOnceIsOnController (Handles[Index], FixedPcdGet32 (PcdRkSdmmcBaseAddress)) &&
            ArmbianOnceOpenDisk (Handles[Index], &Disk) &&
            (ArmbianOnceClassifyFitOnDisk (&Disk, ARMBIAN_ONCE_FIT_LBA, NULL) == ArmbianOnceFitUBoot);
  }

  FreePool (Handles);
  return Found;
}

EFI_STATUS
EFIAPI
ArmbianOnceProbe (
  OUT CONST CHAR16  **Where  OPTIONAL
  )
{
  ARMBIAN_ONCE_DISK  Emmc;
  ARMBIAN_ONCE_DISK  Sd;
  BOOLEAN            HaveSd;
  CONST CHAR16       *Found = NULL;

  if (Where != NULL) {
    *Where = NULL;
  }

  //
  // SPI NOR board: the request goes to the SD card, and the SPL boots only
  // the U-Boot FIT at sector 0x4000 of that card.
  //
  if (FixedPcdGetBool (PcdArmbianOnceRequestOnSd)) {
    if (  !ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdRkSdmmcBaseAddress), &Sd)
       || (ArmbianOnceClassifyFitOnDisk (&Sd, ARMBIAN_ONCE_FIT_LBA, NULL) != ArmbianOnceFitUBoot))
    {
      return EFI_NOT_FOUND;
    }

    if (!ArmbianOnceSpiSplTakesRequests ()) {
      return EFI_UNSUPPORTED;
    }

    if (Where != NULL) {
      *Where = L"the SD card";
    }

    return EFI_SUCCESS;
  }

  //
  // The request goes to the eMMC, so without one there is nothing to do.
  //
  if (!ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdDwcSdhciBaseAddress), &Emmc)) {
    return EFI_NOT_FOUND;
  }

  //
  // Cheapest first.  With no card in the slot the SD disk has no media and
  // costs no read at all.
  //
  HaveSd = ArmbianOnceFindMmcDisk (FixedPcdGet32 (PcdRkSdmmcBaseAddress), &Sd);
  if (HaveSd && (ArmbianOnceClassifyFitOnDisk (&Sd, ARMBIAN_ONCE_FIT_LBA, NULL) == ArmbianOnceFitUBoot)) {
    Found = L"the SD card";
  } else if (ArmbianOnceClassifyFitOnDisk (&Emmc, ARMBIAN_ONCE_FIT_LBA, NULL) == ArmbianOnceFitUBoot) {
    Found = L"the eMMC";
  } else if (OtherDiskHasUBoot ()) {
    Found = L"a USB or NVMe disk";
  } else if (  ArmbianOnceSlotIsFree (&Emmc, NULL, NULL)
            && (ArmbianOnceClassifyFitOnDisk (&Emmc, ARMBIAN_ONCE_SLOT_LBA, NULL) == ArmbianOnceFitUBoot))
  {
    Found = L"the eMMC slot";
  }

  if (Found == NULL) {
    return EFI_NOT_FOUND;
  }

  //
  // Only now, with something to boot, the 1 MiB read of the SPL.
  //
  if (!ArmbianOnceSplTakesRequests (&Emmc) && !(HaveSd && ArmbianOnceSplTakesRequests (&Sd))) {
    return EFI_UNSUPPORTED;
  }

  if (Where != NULL) {
    *Where = Found;
  }

  return EFI_SUCCESS;
}
