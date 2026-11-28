/** @file
  The default boot entry: one choice kept first in BootOrder, whatever
  order an OS leaves.

  Each Windows puts its own entry first when it runs (and on the CM5-IO
  with Windows on eMMC and NVMe, deletes the other's), so the last one run
  became the default, and a crashing one a boot loop (2026-11-28).

  The setting DefaultBootOs (RK3576 Platform Configuration > Boot, read
  through PcdDefaultBootOs) says which entry:

    0    remembered: the boot target in the variable DefaultBootTarget,
         or while there is none, the Windows found first (the first entry
         of WindowsBootPartitions that is present)
    1-4  the Windows on eMMC, NVMe, SD, USB (the first of that kind)
    255  off: BootOrder is left alone

  A boot target is a device path; an entry matches it by partition and file
  path, not by Boot#### number, which Windows changes when it writes its
  entry again (WindowsBootPlan.h, WbpDpSameTarget).  DefaultBootTarget
  ({6b57d6bf-5d25-4798-863a-86b9d464a5ba}, NV+BS, so an OS cannot change
  it) holds the path from the HD() node on for a disk entry, the whole path
  for another.

  Every boot, after the Windows entries are synced, the entry for the
  default is moved to the front of BootOrder when it is not there.
  BootNext is not touched, so a one-shot boot (the boot menu, an OS's
  "restart to") still goes first.

  A choice made by hand: the first active entry of BootOrder is noted at the
  end of AfterConsole.  When a different entry is first at ReadyToBoot or
  at a reset before the OS has started, BootOrder was changed in setup
  (Boot Maintenance Manager) or the UEFI Shell in this boot, never by an
  OS, whose changes come after ExitBootServices.  That entry becomes
  DefaultBootTarget, and a disk-kind setting (1-4) goes back to 0.

  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Guid/GlobalVariable.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Protocol/PlatformSpecificResetFilter.h>
#include <Protocol/ResetNotification.h>

#include "PlatformBm.h"
#include "WindowsBootPlan.h"

#define DEFAULT_TARGET_VAR    L"DefaultBootTarget"
#define DEFAULT_TARGET_MAX    1024
#define DEFAULT_MAX_ESPS      8

STATIC EFI_DEVICE_PATH_PROTOCOL  *mFirstAtConsole;
STATIC BOOLEAN                   mWatching;
STATIC BOOLEAN                   mOsStarted;

STATIC
CONST CHAR8 *
DefaultModeName (
  IN UINT8    Mode,
  IN BOOLEAN  FromTarget
  )
{
  switch (Mode) {
    case WBP_DEFAULT_REMEMBERED:
      return FromTarget ? "remembered entry" : "first Windows found";
    case WBP_DEFAULT_EMMC:
      return "Windows on eMMC";
    case WBP_DEFAULT_NVME:
      return "Windows on NVMe";
    case WBP_DEFAULT_SD:
      return "Windows on SD card";
    case WBP_DEFAULT_USB:
      return "Windows on USB";
    default:
      return "?";
  }
}

/**
  The setting, with an unknown value read as 0.
**/
STATIC
UINT8
DefaultMode (
  VOID
  )
{
  UINT8  Mode;

  Mode = PcdGet8 (PcdDefaultBootOs);
  if ((Mode > WBP_DEFAULT_USB) && (Mode != WBP_DEFAULT_OFF)) {
    Mode = WBP_DEFAULT_REMEMBERED;
  }

  return Mode;
}

/**
  DefaultBootTarget when it holds a well-formed device path, else NULL.
**/
STATIC
EFI_DEVICE_PATH_PROTOCOL *
DefaultReadTarget (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINTN       Size;

  Status = GetVariable2 (DEFAULT_TARGET_VAR, &gPlatformWindowsBootVarGuid, &Data, &Size);
  if (EFI_ERROR (Status) || (Data == NULL)) {
    return NULL;
  }

  if ((Size > DEFAULT_TARGET_MAX) || (WbpDpSize (Data, (wbp_u32)Size) != Size)) {
    DEBUG ((DEBUG_WARN, "PlatformApplyDefaultBootEntry: %s is not a device path (%u bytes), ignored\n", DEFAULT_TARGET_VAR, (UINT32)Size));
    FreePool (Data);
    return NULL;
  }

  return Data;
}

STATIC
BOOLEAN
DefaultSameTarget (
  IN EFI_DEVICE_PATH_PROTOCOL  *A,
  IN EFI_DEVICE_PATH_PROTOCOL  *B
  )
{
  return WbpDpSameTarget (
           (CONST wbp_u8 *)A,
           (wbp_u32)GetDevicePathSize (A),
           (CONST wbp_u8 *)B,
           (wbp_u32)GetDevicePathSize (B)
           ) != 0;
}

/**
  Index in Options (BootOrder order) of the first active, shown entry, or
  Count.
**/
STATIC
UINTN
DefaultFirstShown (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Options,
  IN UINTN                         Count
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (  ((Options[Index].Attributes & LOAD_OPTION_ACTIVE) != 0)
       && ((Options[Index].Attributes & LOAD_OPTION_HIDDEN) == 0))
    {
      break;
    }
  }

  return Index;
}

VOID
PlatformApplyDefaultBootEntry (
  VOID
  )
{
  UINT8                         Mode;
  UINT8                         Kind[DEFAULT_MAX_ESPS];
  UINT32                        Seen[DEFAULT_MAX_ESPS];
  UINTN                         EspCount;
  UINT32                        Pick;
  EFI_DEVICE_PATH_PROTOCOL      *Stored;
  EFI_DEVICE_PATH_PROTOCOL      *Target;
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         Count;
  UINTN                         Index;
  UINT16                        *BootOrder;
  UINT16                        *BootNext;
  UINTN                         Size;
  CHAR16                        *Text;

  Mode = DefaultMode ();
  if (Mode == WBP_DEFAULT_OFF) {
    DEBUG ((DEBUG_INFO, "%a: off, BootOrder left as it is\n", __func__));
    return;
  }

  Stored   = (Mode == WBP_DEFAULT_REMEMBERED) ? DefaultReadTarget () : NULL;
  EspCount = PlatformWindowsFound (Kind, Seen, DEFAULT_MAX_ESPS);
  Pick     = WbpPickDefault (Mode, Stored != NULL, Kind, Seen, (wbp_u32)EspCount);
  if (Pick == WBP_PICK_TARGET) {
    Target = Stored;
    Stored = NULL;
  } else {
    Target = (Pick != WBP_PICK_NONE) ? PlatformWindowsTarget (Pick) : NULL;
  }

  if (Stored != NULL) {
    FreePool (Stored);
  }

  if (Target == NULL) {
    DEBUG ((DEBUG_INFO, "%a: no %a on this boot, BootOrder left as it is\n", __func__, DefaultModeName (Mode, FALSE)));
    return;
  }

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (Index = 0; Index < Count; Index++) {
    if (DefaultSameTarget (Target, Options[Index].FilePath)) {
      break;
    }
  }

  if (Index == Count) {
    Text = ConvertDevicePathToText (Target, FALSE, FALSE);
    DEBUG ((
      DEBUG_INFO,
      "%a: %a %s has no boot option, BootOrder left as it is\n",
      __func__,
      DefaultModeName (Mode, Pick == WBP_PICK_TARGET),
      (Text != NULL) ? Text : L"?"
      ));
    if (Text != NULL) {
      FreePool (Text);
    }
  } else {
    GetEfiGlobalVariable2 (EFI_BOOT_ORDER_VARIABLE_NAME, (VOID **)&BootOrder, &Size);
    if ((BootOrder != NULL) && (Size >= sizeof (UINT16)) && (BootOrder[0] == Options[Index].OptionNumber)) {
      DEBUG ((
        DEBUG_INFO,
        "%a: default Boot%04x \"%s\" (%a) is first\n",
        __func__,
        (UINT32)Options[Index].OptionNumber,
        Options[Index].Description,
        DefaultModeName (Mode, Pick == WBP_PICK_TARGET)
        ));
    } else {
      PlatformMoveBootOptionToFront (Options[Index].OptionNumber);
      DEBUG ((
        DEBUG_INFO,
        "%a: default Boot%04x \"%s\" (%a) moved to the front of BootOrder\n",
        __func__,
        (UINT32)Options[Index].OptionNumber,
        Options[Index].Description,
        DefaultModeName (Mode, Pick == WBP_PICK_TARGET)
        ));
    }

    if (BootOrder != NULL) {
      FreePool (BootOrder);
    }
  }

  GetEfiGlobalVariable2 (EFI_BOOT_NEXT_VARIABLE_NAME, (VOID **)&BootNext, &Size);
  if (BootNext != NULL) {
    if (Size == sizeof (UINT16)) {
      DEBUG ((DEBUG_INFO, "%a: BootNext Boot%04x boots once first\n", __func__, (UINT32)*BootNext));
    }

    FreePool (BootNext);
  }

  EfiBootManagerFreeLoadOptions (Options, Count);
  FreePool (Target);
}

/**
  Called at ReadyToBoot and at a reset before the OS: see the file header.
**/
STATIC
VOID
DefaultCheckFirst (
  IN CONST CHAR8  *When
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         Count;
  UINTN                         First;
  EFI_DEVICE_PATH_PROTOCOL      *Path;
  UINTN                         Size;
  UINT32                        Hd;
  UINT8                         Mode;

  if (mOsStarted || (mFirstAtConsole == NULL)) {
    return;
  }

  Mode = DefaultMode ();
  if (Mode == WBP_DEFAULT_OFF) {
    return;
  }

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  First   = DefaultFirstShown (Options, Count);
  if ((First == Count) || DefaultSameTarget (mFirstAtConsole, Options[First].FilePath)) {
    EfiBootManagerFreeLoadOptions (Options, Count);
    return;
  }

  //
  // From the HD() node on for a disk entry, so that it matches the entry
  // whichever form (full or short) it is written in later.
  //
  Path = Options[First].FilePath;
  Size = GetDevicePathSize (Path);
  Hd   = WbpDpFindHd ((CONST wbp_u8 *)Path, (wbp_u32)Size);
  if (Hd < Size) {
    Path  = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Path + Hd);
    Size -= Hd;
  }

  Status = gRT->SetVariable (
                  DEFAULT_TARGET_VAR,
                  &gPlatformWindowsBootVarGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                  Size,
                  Path
                  );
  if (!EFI_ERROR (Status) && (Mode != WBP_DEFAULT_REMEMBERED)) {
    Status = PcdSet8S (PcdDefaultBootOs, WBP_DEFAULT_REMEMBERED);
  }

  DEBUG ((
    DEBUG_INFO,
    "PlatformWatchDefaultBootEntry: Boot%04x \"%s\" was put first before %a: it is the default now: %r\n",
    (UINT32)Options[First].OptionNumber,
    Options[First].Description,
    When,
    Status
    ));

  FreePool (mFirstAtConsole);
  mFirstAtConsole = DuplicateDevicePath (Options[First].FilePath);
  EfiBootManagerFreeLoadOptions (Options, Count);
}

STATIC
VOID
EFIAPI
DefaultOnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DefaultCheckFirst ("booting");
}

STATIC
VOID
EFIAPI
DefaultOnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mOsStarted = TRUE;
}

STATIC
VOID
EFIAPI
DefaultOnReset (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  //
  // Also called for an OS's reset, at runtime: mOsStarted is set then.
  //
  DefaultCheckFirst ("a reset");
}

VOID
PlatformWatchDefaultBootEntry (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_BOOT_MANAGER_LOAD_OPTION     *Options;
  UINTN                            Count;
  UINTN                            First;
  EFI_EVENT                        Event;
  EFI_RESET_NOTIFICATION_PROTOCOL  *ResetNotify;

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  First   = DefaultFirstShown (Options, Count);
  if (mFirstAtConsole != NULL) {
    FreePool (mFirstAtConsole);
    mFirstAtConsole = NULL;
  }

  if (First < Count) {
    mFirstAtConsole = DuplicateDevicePath (Options[First].FilePath);
  }

  EfiBootManagerFreeLoadOptions (Options, Count);

  if (mWatching) {
    return;
  }

  mWatching = TRUE;
  EfiCreateEventReadyToBootEx (TPL_CALLBACK, DefaultOnReadyToBoot, NULL, &Event);
  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_CALLBACK,
         DefaultOnExitBootServices,
         NULL,
         &gEfiEventExitBootServicesGuid,
         &Event
         );

  //
  // The filter list is called before the reset notifications, among them
  // RkFvbDxe's, which writes the variable store to the eMMC.
  //
  Status = gBS->LocateProtocol (&gEdkiiPlatformSpecificResetFilterProtocolGuid, NULL, (VOID **)&ResetNotify);
  if (!EFI_ERROR (Status)) {
    ResetNotify->RegisterResetNotify (ResetNotify, DefaultOnReset);
  }
}
