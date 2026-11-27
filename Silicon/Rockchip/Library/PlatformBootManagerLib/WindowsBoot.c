/** @file
  A boot option for each Windows installation, named after the Windows
  version and first in BootOrder when the firmware adds it.

  Windows' own entry ("Windows Boot Manager", written by bcdboot from the
  OS) is not enough here: on eMMC the variables an OS writes are lost at the
  next reset unless "Save OS Variable Changes to eMMC" is on, so a fresh
  variable store has no Windows entry at all and the board only boots
  Windows through the eMMC's generic "UEFI ... eMMC User Data" option, if
  nothing else comes first.

  Once per boot, after the disks are connected: every EFI System Partition
  holding \EFI\Microsoft\Boot\bootmgfw.efi gets a Boot#### for that file
  unless one exists already (a bcdboot entry HD(...)/\EFI\...\bootmgfw.efi
  counts).  Its name is the description of the default entry in that ESP's
  BCD store ("Windows 11"), "Windows" when the store cannot be read, with
  the disk kind added ("Windows 11 (NVMe)") when more than one ESP has
  Windows.  An existing option called exactly "Windows Boot Manager" is
  renamed the same way.  An option is moved to the front of BootOrder only
  when it is added or renamed, so an order changed by hand stays.

  The cost on a normal boot is one file open per ESP and one read of the
  Boot#### variables; the BCD is read only when an option is added or
  renamed.

  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>
#include <Library/UefiBootManagerLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

#include "PlatformBm.h"
#include "WindowsBcd.h"

#define WINDOWS_BOOTMGR_PATH  L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define WINDOWS_BCD_PATH      L"\\EFI\\Microsoft\\Boot\\BCD"
#define WINDOWS_BOOTMGR_NAME  L"Windows Boot Manager"
#define WINDOWS_FALLBACK_NAME L"Windows"
#define WINDOWS_MAX_ESPS      8
#define WINDOWS_NAME_CHARS    64
#define WINDOWS_BCD_MAX_SIZE  SIZE_4MB

typedef struct {
  EFI_HANDLE               Handle;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  HARDDRIVE_DEVICE_PATH    *Hd;
  CONST CHAR16             *Kind;
} WINDOWS_ESP;

//
// The optional data bcdboot puts in the firmware entry of the Windows boot
// manager: "WINDOWS", version 1, the length of the whole, an offset, the
// boot manager's BCD object as text, and an empty device path.  With it
// Windows takes this option for its own and updates it instead of adding a
// second one.  Copied from an entry bcdboot wrote on a TaishanPi 3M.
//
#pragma pack(1)
typedef struct {
  CHAR8     Signature[8];
  UINT32    Version;
  UINT32    Length;
  UINT32    Offset;
  CHAR16    Object[49];
  UINT16    Pad;
  UINT32    DpType;
  UINT32    DpLength;
  UINT32    DpSize;
  UINT8     DpEnd[4];
} WINDOWS_BOOT_OPTION_DATA;
#pragma pack()

STATIC CONST WINDOWS_BOOT_OPTION_DATA  mWindowsOptionData = {
  { 'W', 'I', 'N', 'D', 'O', 'W', 'S', '\0' },
  1,
  sizeof (WINDOWS_BOOT_OPTION_DATA),
  sizeof (WINDOWS_BOOT_OPTION_DATA) - 16,
  L"BCDOBJECT={9dea862c-5cdd-4e70-acc1-f32b344d4795}",
  0,
  1,
  16,
  4,
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, 4, 0 }
};

STATIC_ASSERT (sizeof (WINDOWS_BOOT_OPTION_DATA) == 0x88, "the size bcdboot writes");

/**
  The kind of disk a device path is on, for telling two Windows apart.
**/
STATIC
CONST CHAR16 *
WindowsDiskKind (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = DevicePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) != MESSAGING_DEVICE_PATH) {
      continue;
    }

    switch (DevicePathSubType (Node)) {
      case MSG_USB_DP:
      case MSG_USB_CLASS_DP:
      case MSG_USB_WWID_DP:
        return L"USB";
      case MSG_NVME_NAMESPACE_DP:
        return L"NVMe";
      case MSG_EMMC_DP:
        return L"eMMC";
      case MSG_SD_DP:
        return L"SD";
      case MSG_SATA_DP:
        return L"SATA";
      default:
        break;
    }
  }

  return L"disk";
}

/**
  The partition node of a device path: the first one (a short-form option
  starts with it), or NULL.
**/
STATIC
HARDDRIVE_DEVICE_PATH *
WindowsHdNode (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = DevicePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (  (DevicePathType (Node) == MEDIA_DEVICE_PATH)
       && (DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP)
       && (DevicePathNodeLength (Node) >= sizeof (HARDDRIVE_DEVICE_PATH)))
    {
      return (HARDDRIVE_DEVICE_PATH *)Node;
    }
  }

  return NULL;
}

STATIC
BOOLEAN
WindowsSamePartition (
  IN CONST HARDDRIVE_DEVICE_PATH  *A,
  IN CONST HARDDRIVE_DEVICE_PATH  *B
  )
{
  return (A->PartitionNumber == B->PartitionNumber)
         && (A->MBRType == B->MBRType)
         && (A->SignatureType == B->SignatureType)
         && (CompareMem (A->Signature, B->Signature, sizeof (A->Signature)) == 0);
}

/**
  ASCII-case-insensitive comparison of a file path node's text, which is not
  necessarily NUL-terminated inside the node, with Want.
**/
STATIC
BOOLEAN
WindowsFileNodeIs (
  IN EFI_DEVICE_PATH_PROTOCOL  *Node,
  IN CONST CHAR16              *Want
  )
{
  CONST CHAR16  *Path;
  UINTN         Chars;
  UINTN         Index;
  CHAR16        A;
  CHAR16        B;

  if (  (DevicePathType (Node) != MEDIA_DEVICE_PATH)
     || (DevicePathSubType (Node) != MEDIA_FILEPATH_DP)
     || (DevicePathNodeLength (Node) < sizeof (EFI_DEVICE_PATH_PROTOCOL)))
  {
    return FALSE;
  }

  Path  = ((FILEPATH_DEVICE_PATH *)Node)->PathName;
  Chars = (DevicePathNodeLength (Node) - sizeof (EFI_DEVICE_PATH_PROTOCOL)) / sizeof (CHAR16);
  for (Index = 0; Index < Chars; Index++) {
    A = ReadUnaligned16 ((CONST UINT16 *)&Path[Index]);
    B = Want[Index];
    if (A == L'\0') {
      break;
    }

    if ((A >= L'A') && (A <= L'Z')) {
      A += L'a' - L'A';
    }

    if ((B >= L'A') && (B <= L'Z')) {
      B += L'a' - L'A';
    }

    if (A != B) {
      return FALSE;
    }
  }

  return Want[Index] == L'\0';
}

/**
  TRUE if Option boots bootmgfw.efi from the partition of Esp, through a
  short-form HD(...)/File path (bcdboot's) or a full one (ours).
**/
STATIC
BOOLEAN
WindowsOptionBootsEsp (
  IN CONST EFI_BOOT_MANAGER_LOAD_OPTION  *Option,
  IN CONST WINDOWS_ESP                   *Esp
  )
{
  HARDDRIVE_DEVICE_PATH  *Hd;

  Hd = WindowsHdNode (Option->FilePath);
  if ((Hd == NULL) || !WindowsSamePartition (Hd, Esp->Hd)) {
    return FALSE;
  }

  return WindowsFileNodeIs (NextDevicePathNode (&Hd->Header), WINDOWS_BOOTMGR_PATH);
}

/**
  The file's EFI_FILE_INFO, to be freed by the caller, or NULL.
**/
STATIC
EFI_FILE_INFO *
WindowsFileInfo (
  IN EFI_FILE_PROTOCOL  *File
  )
{
  EFI_STATUS     Status;
  EFI_FILE_INFO  *Info;
  UINTN          Size;

  Size   = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }

  Info = AllocatePool (Size);
  if (Info == NULL) {
    return NULL;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &Size, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return NULL;
  }

  return Info;
}

/**
  Read the default entry's description from the ESP's BCD store into Name
  (WINDOWS_NAME_CHARS), or WINDOWS_FALLBACK_NAME.
**/
STATIC
VOID
WindowsReadName (
  IN  CONST WINDOWS_ESP  *Esp,
  OUT CHAR16             *Name
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  EFI_FILE_INFO                    *Info;
  UINT8                            *Buffer;
  UINTN                            Size;
  INTN                             Result;

  Buffer = NULL;
  Size   = 0;
  Result = -1;

  Status = gBS->HandleProtocol (Esp->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (!EFI_ERROR (Status)) {
    Status = Fs->OpenVolume (Fs, &Root);
  }

  if (!EFI_ERROR (Status)) {
    Status = Root->Open (Root, &File, WINDOWS_BCD_PATH, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (Status)) {
      Info = WindowsFileInfo (File);
      if ((Info != NULL) && (Info->FileSize > 0) && (Info->FileSize <= WINDOWS_BCD_MAX_SIZE)) {
        Size   = (UINTN)Info->FileSize;
        Buffer = AllocatePool (Size);
        if (Buffer != NULL) {
          Status = File->Read (File, &Size, Buffer);
          if (!EFI_ERROR (Status)) {
            Result = WbcdDefaultDescription (Buffer, (wbcd_u32)Size, (wbcd_u16 *)Name, WINDOWS_NAME_CHARS);
          }

          FreePool (Buffer);
        }
      } else {
        Status = EFI_BAD_BUFFER_SIZE;
      }

      if (Info != NULL) {
        FreePool (Info);
      }

      File->Close (File);
    }

    Root->Close (Root);
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: BCD on %s: %r, %u bytes, result %d\n",
    __func__,
    Esp->Kind,
    Status,
    (UINT32)Size,
    (INT32)Result
    ));

  if (Result != WBCD_OK) {
    StrCpyS (Name, WINDOWS_NAME_CHARS, WINDOWS_FALLBACK_NAME);
  }
}

/**
  Move Boot#### OptionNumber to the front of BootOrder, adding it there if
  BootOrder does not list it.
**/
STATIC
VOID
WindowsMoveToFront (
  IN UINTN  OptionNumber
  )
{
  UINT16  *BootOrder;
  UINT16  *NewOrder;
  UINTN   Size;
  UINTN   Count;
  UINTN   Index;
  UINTN   Next;

  GetEfiGlobalVariable2 (EFI_BOOT_ORDER_VARIABLE_NAME, (VOID **)&BootOrder, &Size);
  Count = (BootOrder != NULL) ? Size / sizeof (UINT16) : 0;
  if ((Count > 0) && (BootOrder[0] == OptionNumber)) {
    FreePool (BootOrder);
    return;
  }

  NewOrder = AllocatePool ((Count + 1) * sizeof (UINT16));
  if (NewOrder != NULL) {
    NewOrder[0] = (UINT16)OptionNumber;
    Next        = 1;
    for (Index = 0; Index < Count; Index++) {
      if (BootOrder[Index] != OptionNumber) {
        NewOrder[Next++] = BootOrder[Index];
      }
    }

    gRT->SetVariable (
           EFI_BOOT_ORDER_VARIABLE_NAME,
           &gEfiGlobalVariableGuid,
           EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
           Next * sizeof (UINT16),
           NewOrder
           );
    FreePool (NewOrder);
  }

  if (BootOrder != NULL) {
    FreePool (BootOrder);
  }
}

/**
  Find the ESPs that hold bootmgfw.efi.  Only partitions marked as ESP
  (GPT type, or MBR type 0xEF) are looked at: a Windows installer or WinPE
  stick is a plain FAT partition and must not get a permanent option at the
  front of the boot order.
**/
STATIC
UINTN
WindowsFindEsps (
  OUT WINDOWS_ESP  *Esps
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;
  UINTN                            Count;
  EFI_PARTITION_INFO_PROTOCOL      *PartInfo;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  HARDDRIVE_DEVICE_PATH            *Hd;

  Count  = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  for (Index = 0; (Index < HandleCount) && (Count < WINDOWS_MAX_ESPS); Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionInfoProtocolGuid, (VOID **)&PartInfo);
    if (EFI_ERROR (Status) || (PartInfo->System != 1)) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Hd = WindowsHdNode (DevicePath);
    if (Hd == NULL) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status) || EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
      continue;
    }

    Status = Root->Open (Root, &File, WINDOWS_BOOTMGR_PATH, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (Status)) {
      File->Close (File);
      Esps[Count].Handle     = Handles[Index];
      Esps[Count].DevicePath = DevicePath;
      Esps[Count].Hd         = Hd;
      Esps[Count].Kind       = WindowsDiskKind (DevicePath);
      Count++;
    }

    Root->Close (Root);
  }

  FreePool (Handles);
  return Count;
}

/**
  The option's name for Esp: the BCD description, with the disk kind when
  there is more than one Windows.
**/
STATIC
VOID
WindowsOptionName (
  IN  CONST WINDOWS_ESP  *Esp,
  IN  UINTN              EspCount,
  OUT CHAR16             *Name,
  IN  UINTN              NameChars
  )
{
  CHAR16  Description[WINDOWS_NAME_CHARS];

  WindowsReadName (Esp, Description);
  if (EspCount > 1) {
    UnicodeSPrint (Name, NameChars * sizeof (CHAR16), L"%s (%s)", Description, Esp->Kind);
  } else {
    StrCpyS (Name, NameChars, Description);
  }
}

/**
  Add, name and order the Windows boot options (see the file header).
  Called once per boot, after the disks are connected.
**/
VOID
PlatformSyncWindowsBootOptions (
  VOID
  )
{
  EFI_STATUS                    Status;
  WINDOWS_ESP                   Esps[WINDOWS_MAX_ESPS];
  UINTN                         EspCount;
  UINTN                         EspIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  UINTN                         Index;
  BOOLEAN                       Found;
  CHAR16                        Name[WINDOWS_NAME_CHARS + 16];
  CHAR16                        *NewDescription;
  EFI_DEVICE_PATH_PROTOCOL      *FileNode;
  EFI_DEVICE_PATH_PROTOCOL      *ShortForm;
  EFI_DEVICE_PATH_PROTOCOL      *HdOnly;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;

  EspCount = WindowsFindEsps (Esps);
  if (EspCount == 0) {
    DEBUG ((DEBUG_INFO, "%a: no EFI System Partition with Windows\n", __func__));
    return;
  }

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);

  for (EspIndex = 0; EspIndex < EspCount; EspIndex++) {
    Found = FALSE;
    for (Index = 0; Index < BootOptionCount; Index++) {
      if (!WindowsOptionBootsEsp (&BootOptions[Index], &Esps[EspIndex])) {
        continue;
      }

      Found = TRUE;
      if (StrCmp (BootOptions[Index].Description, WINDOWS_BOOTMGR_NAME) != 0) {
        DEBUG ((
          DEBUG_INFO,
          "%a: Boot%04x \"%s\" boots Windows on %s\n",
          __func__,
          (UINT32)BootOptions[Index].OptionNumber,
          BootOptions[Index].Description,
          Esps[EspIndex].Kind
          ));
        continue;
      }

      WindowsOptionName (&Esps[EspIndex], EspCount, Name, ARRAY_SIZE (Name));
      NewDescription = AllocateCopyPool (StrSize (Name), Name);
      if (NewDescription == NULL) {
        continue;
      }

      FreePool (BootOptions[Index].Description);
      BootOptions[Index].Description = NewDescription;
      Status                         = EfiBootManagerLoadOptionToVariable (&BootOptions[Index]);
      if (!EFI_ERROR (Status)) {
        WindowsMoveToFront (BootOptions[Index].OptionNumber);
      }

      DEBUG ((
        DEBUG_INFO,
        "%a: Boot%04x \"%s\" renamed \"%s\" and moved to the front of BootOrder: %r\n",
        __func__,
        (UINT32)BootOptions[Index].OptionNumber,
        WINDOWS_BOOTMGR_NAME,
        Name,
        Status
        ));
    }

    if (Found) {
      continue;
    }

    //
    // HD(...)/\EFI\Microsoft\Boot\bootmgfw.efi, the short form bcdboot
    // writes: the boot manager finds the partition wherever the disk is
    // plugged in.
    //
    HdOnly    = AppendDevicePathNode (NULL, &Esps[EspIndex].Hd->Header);
    FileNode  = FileDevicePath (NULL, WINDOWS_BOOTMGR_PATH);
    ShortForm = ((HdOnly != NULL) && (FileNode != NULL)) ? AppendDevicePath (HdOnly, FileNode) : NULL;
    if (ShortForm != NULL) {
      WindowsOptionName (&Esps[EspIndex], EspCount, Name, ARRAY_SIZE (Name));
      Status = EfiBootManagerInitializeLoadOption (
                 &NewOption,
                 LoadOptionNumberUnassigned,
                 LoadOptionTypeBoot,
                 LOAD_OPTION_ACTIVE,
                 Name,
                 ShortForm,
                 (UINT8 *)&mWindowsOptionData,
                 sizeof (mWindowsOptionData)
                 );
      if (!EFI_ERROR (Status)) {
        Status = EfiBootManagerAddLoadOptionVariable (&NewOption, 0);
        DEBUG ((
          DEBUG_INFO,
          "%a: Boot%04x \"%s\" added at the front of BootOrder for Windows on %s: %r\n",
          __func__,
          (UINT32)NewOption.OptionNumber,
          Name,
          Esps[EspIndex].Kind,
          Status
          ));
        EfiBootManagerFreeLoadOption (&NewOption);
      }
    }

    if (HdOnly != NULL) {
      FreePool (HdOnly);
    }

    if (FileNode != NULL) {
      FreePool (FileNode);
    }

    if (ShortForm != NULL) {
      FreePool (ShortForm);
    }
  }

  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}
