/** @file
 *
 *  The debug report, except the setup pages: where it goes, the firmware
 *  log, a summary of the machine, the ACPI tables, the device tree and the
 *  screen.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Guid/Acpi.h>
#include <Guid/Fdt.h>
#include <Guid/GlobalVariable.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Library/MemDebugLogLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>
#include <Protocol/RockchipScreenshot.h>
#include <Protocol/Smbios.h>
#include <Protocol/UsbIo.h>

#include "DebugReport.h"

STATIC EFI_GUID  mScreenshotGuid = ROCKCHIP_SCREENSHOT_PROTOCOL_GUID;

//
// Text
//

VOID
EFIAPI
TextAdd (
  IN OUT REPORT_TEXT  *Text,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR8    Line[512];
  UINTN    Length;
  CHAR8    *Bigger;
  UINTN    NewSize;

  VA_START (Marker, Format);
  Length = AsciiVSPrint (Line, sizeof (Line), Format, Marker);
  VA_END (Marker);

  if (Text->Length + Length + 1 > Text->Size) {
    NewSize = MAX (Text->Size * 2, Text->Length + Length + 4096);
    Bigger  = ReallocatePool (Text->Size, NewSize, Text->Buffer);
    if (Bigger == NULL) {
      return;
    }

    Text->Buffer = Bigger;
    Text->Size   = NewSize;
  }

  CopyMem (Text->Buffer + Text->Length, Line, Length);
  Text->Length              += Length;
  Text->Buffer[Text->Length] = 0;
}

VOID
TextAddUnicode (
  IN OUT REPORT_TEXT   *Text,
  IN     CONST CHAR16  *String
  )
{
  CHAR8  Chunk[129];
  UINTN  Index;

  if (String == NULL) {
    TextAdd (Text, "(none)");
    return;
  }

  while (*String != 0) {
    for (Index = 0; (Index < sizeof (Chunk) - 1) && (*String != 0); Index++, String++) {
      Chunk[Index] = ((*String >= 0x20) && (*String < 0x7f)) ? (CHAR8)*String : '?';
    }

    Chunk[Index] = 0;
    TextAdd (Text, "%a", Chunk);
  }
}

VOID
TextFree (
  IN OUT REPORT_TEXT  *Text
  )
{
  if (Text->Buffer != NULL) {
    FreePool (Text->Buffer);
  }

  ZeroMem (Text, sizeof (*Text));
}

STATIC
VOID
TextAddPath (
  IN OUT REPORT_TEXT  *Text,
  IN     EFI_HANDLE   Handle
  )
{
  CHAR16  *Path;

  Path = ConvertDevicePathToText (DevicePathFromHandle (Handle), FALSE, TRUE);
  TextAddUnicode (Text, Path);
  if (Path != NULL) {
    FreePool (Path);
  }
}

//
// Files
//

EFI_STATUS
ReportWriteFile (
  IN EFI_FILE_PROTOCOL  *Dir,
  IN CONST CHAR16       *Name,
  IN CONST VOID         *Data,
  IN UINTN              Size
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  UINTN              Written;

  Status = Dir->Open (Dir, &File, (CHAR16 *)Name, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Written = Size;
  Status  = (Size == 0) ? EFI_SUCCESS : File->Write (File, &Written, (VOID *)Data);
  if (!EFI_ERROR (Status) && (Written != Size)) {
    Status = EFI_VOLUME_FULL;
  }

  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }

  File->Close (File);
  return Status;
}

EFI_STATUS
ReportOpenDir (
  IN  EFI_FILE_PROTOCOL  *Parent,
  IN  CONST CHAR16       *Name,
  OUT EFI_FILE_PROTOCOL  **Dir
  )
{
  return Parent->Open (
                   Parent,
                   Dir,
                   (CHAR16 *)Name,
                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                   EFI_FILE_DIRECTORY
                   );
}

//
// Where to
//

/**
  0 a USB stick, 1 an SD card or other removable medium, 2 the rest, as
  ScreenshotDxe does.  MAX_UINTN: read only.
**/
STATIC
UINTN
ReportRank (
  IN EFI_HANDLE  Handle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_BLOCK_IO_PROTOCOL     *BlockIo;
  BOOLEAN                   Removable;

  Removable = FALSE;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo)) &&
      (BlockIo->Media != NULL))
  {
    if (BlockIo->Media->ReadOnly) {
      return MAX_UINTN;
    }

    Removable = BlockIo->Media->RemovableMedia;
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path))) {
    for ( ; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path)) {
      if (DevicePathType (Path) != MESSAGING_DEVICE_PATH) {
        continue;
      }

      if ((DevicePathSubType (Path) == MSG_USB_DP) || (DevicePathSubType (Path) == MSG_USB_CLASS_DP)) {
        return 0;
      }

      if (DevicePathSubType (Path) == MSG_SD_DP) {
        Removable = TRUE;
      }
    }
  }

  return Removable ? 1 : 2;
}

EFI_STATUS
ReportOpenDestination (
  OUT EFI_FILE_PROTOCOL  **Dir,
  OUT CHAR16             *Where,
  IN  UINTN              WhereSize
  )
{
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Rank;
  UINTN                            Index;
  UINTN                            Number;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *Top;
  EFI_FILE_PROTOCOL                *Probe;
  EFI_TIME                         Time;
  BOOLEAN                          Clock;
  CHAR16                           Name[32];
  CHAR16                           *PathText;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles))) {
    return EFI_NOT_FOUND;
  }

  Clock = !EFI_ERROR (gRT->GetTime (&Time, NULL)) && (Time.Year >= 2025) && (Time.Year < 2100);

  for (Rank = 0; Rank <= 2; Rank++) {
    for (Index = 0; Index < Count; Index++) {
      if ((ReportRank (Handles[Index]) != Rank) ||
          EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
          EFI_ERROR (Fs->OpenVolume (Fs, &Root)))
      {
        continue;
      }

      if (EFI_ERROR (ReportOpenDir (Root, REPORT_DIRECTORY, &Top))) {
        Root->Close (Root);
        continue;
      }

      //
      // A new folder each time: the date and time, or a counter when the
      // clock is not set.
      //
      for (Number = 1; Number < 10000; Number++) {
        if (Clock) {
          UnicodeSPrint (
            Name,
            sizeof (Name),
            (Number == 1) ? L"%04u%02u%02u-%02u%02u%02u" : L"%04u%02u%02u-%02u%02u%02u-%u",
            Time.Year,
            Time.Month,
            Time.Day,
            Time.Hour,
            Time.Minute,
            Time.Second,
            Number
            );
        } else {
          UnicodeSPrint (Name, sizeof (Name), L"report-%04u", Number);
        }

        if (EFI_ERROR (Top->Open (Top, &Probe, Name, EFI_FILE_MODE_READ, 0))) {
          break;
        }

        Probe->Close (Probe);
      }

      if (EFI_ERROR (ReportOpenDir (Top, Name, Dir))) {
        Top->Close (Top);
        Root->Close (Root);
        continue;
      }

      PathText = ConvertDevicePathToText (DevicePathFromHandle (Handles[Index]), TRUE, TRUE);
      UnicodeSPrint (Where, WhereSize, L"\\%s\\%s on %s", REPORT_DIRECTORY, Name, (PathText != NULL) ? PathText : L"?");
      if (PathText != NULL) {
        FreePool (PathText);
      }

      Top->Close (Top);
      Root->Close (Root);
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  return EFI_NOT_FOUND;
}

//
// The parts
//

/**
  The memory debug log (MemLogDebugLib), oldest line first.
**/
STATIC
EFI_STATUS
ReportLog (
  IN EFI_FILE_PROTOCOL  *Dir,
  OUT UINTN             *Bytes
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;
  MEM_DEBUG_LOG_HDR     *Header;
  CHAR8                 *Start;
  CHAR8                 *Copy;
  UINTN                 Head;
  UINTN                 Tail;
  UINTN                 Size;
  UINTN                 Length;

  *Bytes = 0;
  Status = MemDebugLogAddrFromHOB (&Address);
  if (EFI_ERROR (Status) || (Address == 0)) {
    return EFI_NOT_FOUND;
  }

  Header = (MEM_DEBUG_LOG_HDR *)(UINTN)Address;
  if ((Header->Magic1 != MEM_DEBUG_LOG_MAGIC1) || (Header->Magic2 != MEM_DEBUG_LOG_MAGIC2)) {
    return EFI_NOT_FOUND;
  }

  //
  // Copied first: the debug output of writing the file goes to the same
  // buffer.
  //
  Start = (CHAR8 *)(UINTN)(Address + Header->HeaderSize);
  Size  = (UINTN)Header->DebugLogSize;
  Head  = (UINTN)Header->DebugLogHeadOffset;
  Tail  = (UINTN)Header->DebugLogTailOffset;
  if ((Head >= Size) || (Tail >= Size)) {
    return EFI_VOLUME_CORRUPTED;
  }

  Copy = AllocatePool (Size);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (Head <= Tail) {
    Length = Tail - Head;
    CopyMem (Copy, Start + Head, Length);
  } else {
    // wrapped: from the head to the end, then from the start to the tail
    Length = Size - 1 - Head;
    CopyMem (Copy, Start + Head, Length);
    CopyMem (Copy + Length, Start, Tail);
    Length += Tail;
  }

  Status = ReportWriteFile (Dir, L"firmware-log.txt", Copy, Length);
  FreePool (Copy);
  *Bytes = Length;
  return Status;
}

STATIC
VOID
ReportSmbios (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  CHAR8                    *String;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios))) {
    TextAdd (Text, "  no SMBIOS\n");
    return;
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, NULL, &Record, NULL))) {
    //
    // The records that say what the machine is: firmware (0), system (1),
    // board (2), enclosure (3), processor (4).  All their strings.
    //
    if (Record->Type > 4) {
      continue;
    }

    TextAdd (Text, "  type %u:", Record->Type);
    for (String = (CHAR8 *)Record + Record->Length; *String != 0; String += AsciiStrLen (String) + 1) {
      TextAdd (Text, " \"%a\"", String);
    }

    TextAdd (Text, "\n");
  }
}

STATIC
VOID
ReportMemoryMap (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Entry;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  UINT64                 Total;

  STATIC CONST CHAR8  *TypeName[] = {
    "Reserved", "LoaderCode", "LoaderData", "BootServicesCode", "BootServicesData",
    "RuntimeServicesCode", "RuntimeServicesData", "Conventional", "Unusable",
    "ACPIReclaim", "ACPINVS", "MMIO", "MMIOPortSpace", "PalCode", "Persistent", "Unaccepted"
  };

  Size   = 0;
  Map    = NULL;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    TextAdd (Text, "  GetMemoryMap: %r\n", Status);
    return;
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    return;
  }

  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    TextAdd (Text, "  GetMemoryMap: %r\n", Status);
    FreePool (Map);
    return;
  }

  Total = 0;
  for (Entry = Map; (UINT8 *)Entry < (UINT8 *)Map + Size; Entry = NEXT_MEMORY_DESCRIPTOR (Entry, DescSize)) {
    TextAdd (
      Text,
      "  %016lx-%016lx %-20a %8lu pages  attr %016lx\n",
      Entry->PhysicalStart,
      Entry->PhysicalStart + EFI_PAGES_TO_SIZE (Entry->NumberOfPages) - 1,
      (Entry->Type < ARRAY_SIZE (TypeName)) ? TypeName[Entry->Type] : "?",
      Entry->NumberOfPages,
      Entry->Attribute
      );
    if (Entry->Type != EfiReservedMemoryType && Entry->Type != EfiMemoryMappedIO && Entry->Type != EfiMemoryMappedIOPortSpace) {
      Total += EFI_PAGES_TO_SIZE (Entry->NumberOfPages);
    }
  }

  TextAdd (Text, "  memory in the map, not reserved or MMIO: %lu MB\n", RShiftU64 (Total, 20));
  FreePool (Map);
}

STATIC
VOID
ReportBoot (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         Count;
  UINTN                         Index;
  UINT16                        *Order;
  UINTN                         OrderSize;
  UINT16                        Current;
  UINTN                         Size;
  CHAR16                        *Path;

  Size = sizeof (Current);
  if (!EFI_ERROR (gRT->GetVariable (L"BootCurrent", &gEfiGlobalVariableGuid, NULL, &Size, &Current))) {
    TextAdd (Text, "  BootCurrent: Boot%04x\n", Current);
  }

  GetEfiGlobalVariable2 (L"BootOrder", (VOID **)&Order, &OrderSize);
  TextAdd (Text, "  BootOrder:");
  if (Order != NULL) {
    for (Index = 0; Index < OrderSize / sizeof (UINT16); Index++) {
      TextAdd (Text, " %04x", Order[Index]);
    }

    FreePool (Order);
  } else {
    TextAdd (Text, " (not set)");
  }

  TextAdd (Text, "\n");

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (Index = 0; Index < Count; Index++) {
    TextAdd (
      Text,
      "  Boot%04x %a%a ",
      Options[Index].OptionNumber,
      ((Options[Index].Attributes & LOAD_OPTION_ACTIVE) != 0) ? "active" : "inactive",
      ((Options[Index].Attributes & LOAD_OPTION_HIDDEN) != 0) ? " hidden" : ""
      );
    TextAddUnicode (Text, Options[Index].Description);
    TextAdd (Text, "\n    ");
    Path = ConvertDevicePathToText (Options[Index].FilePath, FALSE, TRUE);
    TextAddUnicode (Text, Path);
    if (Path != NULL) {
      FreePool (Path);
    }

    TextAdd (Text, "\n");
  }

  EfiBootManagerFreeLoadOptions (Options, Count);
}

STATIC
VOID
ReportDisplay (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_HANDLE                            *Handles;
  UINTN                                 Count;
  UINTN                                 Index;
  UINT32                                Mode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINTN                                 InfoSize;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    TextAdd (Text, "  no graphics output\n");
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) ||
        (Gop->Mode == NULL) || (Gop->Mode->Info == NULL))
    {
      continue;
    }

    TextAdd (Text, "  ");
    TextAddPath (Text, Handles[Index]);
    TextAdd (
      Text,
      "\n    mode %u of %u: %ux%u, pixel format %u, framebuffer 0x%lx (%lu bytes)\n    modes:",
      Gop->Mode->Mode,
      Gop->Mode->MaxMode,
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      Gop->Mode->Info->PixelFormat,
      Gop->Mode->FrameBufferBase,
      (UINT64)Gop->Mode->FrameBufferSize
      );
    for (Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
      if (!EFI_ERROR (Gop->QueryMode (Gop, Mode, &InfoSize, &Info))) {
        TextAdd (Text, " %ux%u", Info->HorizontalResolution, Info->VerticalResolution);
        FreePool (Info);
      }
    }

    TextAdd (Text, "\n");
  }

  FreePool (Handles);
}

STATIC
VOID
ReportPci (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_HANDLE           *Handles;
  UINTN                Count;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINTN                Seg, Bus, Dev, Fun;
  UINT32               Id;
  UINT32               Class;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &Count, &Handles))) {
    TextAdd (Text, "  none\n");
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) ||
        EFI_ERROR (PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Fun)) ||
        EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &Id)) ||
        EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 8, 1, &Class)))
    {
      continue;
    }

    TextAdd (
      Text,
      "  %04x:%02x:%02x.%x  %04x:%04x  class %06x  ",
      Seg,
      Bus,
      Dev,
      Fun,
      Id & 0xffff,
      Id >> 16,
      Class >> 8
      );
    TextAddPath (Text, Handles[Index]);
    TextAdd (Text, "\n");
  }

  FreePool (Handles);
}

STATIC
VOID
ReportUsb (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  EFI_USB_IO_PROTOCOL       *UsbIo;
  EFI_USB_DEVICE_DESCRIPTOR  Device;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles))) {
    TextAdd (Text, "  none\n");
    return;
  }

  //
  // The descriptors UsbBusDxe keeps: nothing is sent to the devices.
  //
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo)) ||
        EFI_ERROR (UsbIo->UsbGetDeviceDescriptor (UsbIo, &Device)) ||
        EFI_ERROR (UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Interface)))
    {
      continue;
    }

    TextAdd (
      Text,
      "  %04x:%04x  device class %02x  interface %u class %02x/%02x/%02x  ",
      Device.IdVendor,
      Device.IdProduct,
      Device.DeviceClass,
      Interface.InterfaceNumber,
      Interface.InterfaceClass,
      Interface.InterfaceSubClass,
      Interface.InterfaceProtocol
      );
    TextAddPath (Text, Handles[Index]);
    TextAdd (Text, "\n");
  }

  FreePool (Handles);
}

STATIC
VOID
ReportStorage (
  IN OUT REPORT_TEXT  *Text
  )
{
  EFI_HANDLE             *Handles;
  UINTN                  Count;
  UINTN                  Index;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  VOID                   *Fs;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    TextAdd (Text, "  none\n");
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo)) ||
        (BlockIo->Media == NULL))
    {
      continue;
    }

    TextAdd (
      Text,
      "  %a%a%a%a%a %lu MB (%u-byte blocks)  ",
      BlockIo->Media->LogicalPartition ? "partition" : "disk",
      BlockIo->Media->MediaPresent ? "" : " no-media",
      BlockIo->Media->RemovableMedia ? " removable" : "",
      BlockIo->Media->ReadOnly ? " read-only" : "",
      EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, &Fs)) ? "" : " FAT",
      BlockIo->Media->MediaPresent ? RShiftU64 (MultU64x32 (BlockIo->Media->LastBlock + 1, BlockIo->Media->BlockSize), 20) : 0,
      BlockIo->Media->BlockSize
      );
    TextAddPath (Text, Handles[Index]);
    TextAdd (Text, "\n");
  }

  FreePool (Handles);
}

STATIC
VOID *
ReportConfigTable (
  IN EFI_GUID  *Guid
  )
{
  UINTN  Index;

  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, Guid)) {
      return gST->ConfigurationTable[Index].VendorTable;
    }
  }

  return NULL;
}

/**
  One ACPI table, as SIGN.aml (SIGN2.aml and on for more with the same
  signature).
**/
STATIC
EFI_STATUS
ReportAcpiTable (
  IN     EFI_FILE_PROTOCOL            *Dir,
  IN     EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN OUT REPORT_TEXT                  *Text
  )
{
  CHAR16             Name[24];
  CHAR8              Sig[5];
  UINTN              Number;
  EFI_FILE_PROTOCOL  *Probe;
  EFI_STATUS         Status;

  if ((Table == NULL) || (Table->Length < sizeof (EFI_ACPI_DESCRIPTION_HEADER)) || (Table->Length > SIZE_16MB)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Sig, &Table->Signature, 4);
  for (Number = 0; Number < 4; Number++) {
    if ((Sig[Number] < 0x21) || (Sig[Number] > 0x7e) || (Sig[Number] == '\\') || (Sig[Number] == '/')) {
      Sig[Number] = '_';
    }
  }

  Sig[4] = 0;
  for (Number = 1; Number < 100; Number++) {
    UnicodeSPrint (Name, sizeof (Name), (Number == 1) ? L"%a.aml" : L"%a%u.aml", Sig, Number);
    if (EFI_ERROR (Dir->Open (Dir, &Probe, Name, EFI_FILE_MODE_READ, 0))) {
      break;
    }

    Probe->Close (Probe);
  }

  Status = ReportWriteFile (Dir, Name, Table, Table->Length);
  TextAdd (
    Text,
    "  %a  %6u bytes  rev %u  OEM \"%.6a\" \"%.8a\"  -> %s %r\n",
    Sig,
    Table->Length,
    Table->Revision,
    Table->OemId,
    (CHAR8 *)&Table->OemTableId,
    Name,
    Status
    );
  return Status;
}

STATIC
EFI_STATUS
ReportAcpi (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN OUT REPORT_TEXT        *Text
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *Table;
  EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE     *Fadt;
  EFI_FILE_PROTOCOL                             *AcpiDir;
  UINTN                                         Entries;
  UINTN                                         Index;
  UINT64                                        Address;

  Rsdp = ReportConfigTable (&gEfiAcpi20TableGuid);
  if (Rsdp == NULL) {
    TextAdd (Text, "  no ACPI tables (device tree mode)\n");
    return EFI_NOT_FOUND;
  }

  if ((Rsdp->Revision < 2) || (Rsdp->XsdtAddress == 0)) {
    TextAdd (Text, "  ACPI RSDP without an XSDT\n");
    return EFI_UNSUPPORTED;
  }

  if (EFI_ERROR (ReportOpenDir (Dir, L"acpi", &AcpiDir))) {
    return EFI_DEVICE_ERROR;
  }

  ReportWriteFile (AcpiDir, L"RSDP.bin", Rsdp, Rsdp->Length);
  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  ReportAcpiTable (AcpiDir, Xsdt, Text);

  Entries = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / sizeof (UINT64);
  for (Index = 0; Index < Entries; Index++) {
    CopyMem (&Address, (UINT8 *)Xsdt + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + Index * sizeof (UINT64), sizeof (UINT64));
    Table = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Address;
    if (Table == NULL) {
      continue;
    }

    ReportAcpiTable (AcpiDir, Table, Text);

    // the DSDT and the FACS hang off the FADT
    if (Table->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
      Fadt = (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE *)Table;
      ReportAcpiTable (AcpiDir, (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)((Fadt->XDsdt != 0) ? Fadt->XDsdt : Fadt->Dsdt), Text);
      Address = (Fadt->XFirmwareCtrl != 0) ? Fadt->XFirmwareCtrl : Fadt->FirmwareCtrl;
      if (Address != 0) {
        ReportWriteFile (
          AcpiDir,
          L"FACS.bin",
          (VOID *)(UINTN)Address,
          ((EFI_ACPI_2_0_FIRMWARE_ACPI_CONTROL_STRUCTURE *)(UINTN)Address)->Length
          );
      }
    }
  }

  AcpiDir->Close (AcpiDir);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReportDtb (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN OUT REPORT_TEXT        *Text
  )
{
  UINT8       *Fdt;
  UINT32      Size;
  EFI_STATUS  Status;

  Fdt = ReportConfigTable (&gFdtTableGuid);
  if (Fdt == NULL) {
    TextAdd (Text, "  no device tree installed (ACPI mode)\n");
    return EFI_NOT_FOUND;
  }

  // big endian: magic 0xd00dfeed, then the total size
  if ((Fdt[0] != 0xd0) || (Fdt[1] != 0x0d) || (Fdt[2] != 0xfe) || (Fdt[3] != 0xed)) {
    TextAdd (Text, "  the device tree table has no FDT header\n");
    return EFI_VOLUME_CORRUPTED;
  }

  Size   = ((UINT32)Fdt[4] << 24) | ((UINT32)Fdt[5] << 16) | ((UINT32)Fdt[6] << 8) | Fdt[7];
  Status = (Size <= SIZE_16MB) ? ReportWriteFile (Dir, L"devicetree.dtb", Fdt, Size) : EFI_VOLUME_CORRUPTED;
  TextAdd (Text, "  device tree at 0x%p, %u bytes -> devicetree.dtb %r\n", Fdt, Size, Status);
  return Status;
}

EFI_STATUS
ReportScreenshot (
  IN EFI_FILE_PROTOCOL  *Dir,
  IN CONST CHAR16       *Name
  )
{
  ROCKCHIP_SCREENSHOT_PROTOCOL  *Shot;

  if (EFI_ERROR (gBS->LocateProtocol (&mScreenshotGuid, NULL, (VOID **)&Shot)) ||
      (Shot->Revision < 2) || (Shot->SaveTo == NULL))
  {
    return EFI_UNSUPPORTED;
  }

  return Shot->SaveTo (Shot, Dir, Name, NULL, NULL);
}

VOID
ReportWriteAll (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN OUT REPORT_TEXT        *Parts
  )
{
  REPORT_TEXT  Text;
  EFI_STATUS   Status;
  UINTN        Bytes;
  EFI_TIME     Time;
  UINTN        Index;

  ZeroMem (&Text, sizeof (Text));

  //
  // The screen first, as it was when the report was asked for.
  //
  Status = ReportScreenshot (Dir, L"screen.png");
  TextAdd (Parts, "screen.png               %r\n", Status);

  Status = ReportLog (Dir, &Bytes);
  TextAdd (Parts, "firmware-log.txt         %r (%u bytes)\n", Status, Bytes);

  //
  // The summary, one section after another; none of them stops the others.
  //
  TextAdd (&Text, "edk2-rk3576 debug report\n\n");
  if (!EFI_ERROR (gRT->GetTime (&Time, NULL))) {
    TextAdd (&Text, "time (RTC)  %04u-%02u-%02u %02u:%02u:%02u\n", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
  }

  TextAdd (&Text, "firmware    ");
  TextAddUnicode (&Text, (CHAR16 *)PcdGetPtr (PcdFirmwareVersionString));
  TextAdd (&Text, "\nvendor      ");
  TextAddUnicode (&Text, gST->FirmwareVendor);
  TextAdd (&Text, ", revision 0x%x, UEFI %u.%u\n", gST->FirmwareRevision, gST->Hdr.Revision >> 16, (gST->Hdr.Revision & 0xffff) / 10);
  TextAdd (&Text, "this driver built %a %a\nSoC         Rockchip RK3576\n", __DATE__, __TIME__);

  TextAdd (&Text, "\nSMBIOS\n");
  ReportSmbios (&Text);
  TextAdd (&Text, "\nboot options\n");
  ReportBoot (&Text);
  TextAdd (&Text, "\ndisplay\n");
  ReportDisplay (&Text);
  TextAdd (&Text, "\nPCI devices\n");
  ReportPci (&Text);
  TextAdd (&Text, "\nUSB devices\n");
  ReportUsb (&Text);
  TextAdd (&Text, "\nblock devices\n");
  ReportStorage (&Text);

  TextAdd (&Text, "\nACPI tables (acpi\\)\n");
  Status = ReportAcpi (Dir, &Text);
  TextAdd (Parts, "acpi\\                    %r\n", Status);

  TextAdd (&Text, "\ndevice tree\n");
  Status = ReportDtb (Dir, &Text);
  TextAdd (Parts, "devicetree.dtb           %r\n", Status);

  TextAdd (&Text, "\nconfiguration tables\n");
  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    TextAdd (&Text, "  %g at 0x%p\n", &gST->ConfigurationTable[Index].VendorGuid, gST->ConfigurationTable[Index].VendorTable);
  }

  TextAdd (&Text, "\nmemory map\n");
  ReportMemoryMap (&Text);

  Status = (Text.Buffer != NULL) ? ReportWriteFile (Dir, L"summary.txt", Text.Buffer, Text.Length) : EFI_OUT_OF_RESOURCES;
  TextAdd (Parts, "summary.txt              %r\n", Status);
  TextFree (&Text);
}
