/** @file
 *
 *  Save a debug report: everything needed to look into a problem, in one
 *  folder, so that a user can hand it over in one go.
 *
 *  It goes to \uefi-debug\YYYYMMDD-HHMMSS\ (report-NNNN when the clock is
 *  not set) on the first writable FAT file system: a USB stick, else an SD
 *  card or other removable medium, else internal storage.  Nothing is
 *  written anywhere else.  In it:
 *
 *    README.txt        what was saved, and how each part went
 *    firmware-log.txt  the firmware's debug output of this boot, from the
 *                      DXE core on (MemLogDebugLib, 2 MB, oldest lines go
 *                      first when it is full)
 *    summary.txt       firmware, SMBIOS, boot options and BootOrder,
 *                      display, PCI, USB and block devices, ACPI tables,
 *                      device tree, configuration tables, memory map
 *    acpi\             every installed ACPI table as SIGN.aml
 *    devicetree.dtb    the device tree handed to the OS, if one is
 *    screen.png        the screen when the report was asked for
 *    pages\            from the setup page only: every form of every form
 *                      set shown in turn, one screenshot each, and
 *                      pages.txt listing them
 *
 *  Two ways in:
 *
 *    Device Manager > Debug Report: with all setup pages, or without.
 *    Ctrl+Alt+F11 anywhere (F11 on the serial console, which TerminalDxe
 *    matches without Ctrl and Alt): without the setup pages, which can only
 *    be walked from inside the setup browser.  A square flashes in the top
 *    left corner: green saved, yellow nowhere to write.
 *
 *  Every part is on its own: one failing is written in README.txt and the
 *  others go on.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Guid/EventGroup.h>
#include <Guid/MdeModuleHii.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/SimpleTextInEx.h>

#include "DebugReport.h"
#include "DebugReportFormSet.h"

#define STATUS_SIDE     32
#define STATUS_SHOW_US  (400 * 1000)

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} REPORT_DEVICE_PATH;

extern UINT8  DebugReportVfrBin[];

STATIC REPORT_DEVICE_PATH  mHiiPath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    DEBUG_REPORT_HII_DEVICE_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_GUID        mFormSetGuid = DEBUG_REPORT_FORMSET_GUID;
STATIC EFI_HANDLE      mHiiDriverHandle;
STATIC EFI_HII_HANDLE  mHiiHandle;
STATIC EFI_EVENT       mHotKeyEvent;
STATIC BOOLEAN         mGone;
STATIC BOOLEAN         mRunning;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mSplitter;
STATIC VOID            *mTextInRegistration;

/**
  Make a report.

  @param  Pages  Walk the setup pages too (only from the setup browser).

  @retval EFI_NOT_FOUND  No writable FAT file system.
**/
STATIC
EFI_STATUS
ReportRun (
  IN  BOOLEAN  Pages,
  OUT CHAR16   *Where,
  IN  UINTN    WhereSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Dir;
  REPORT_TEXT        Parts;

  if (mRunning) {
    return EFI_ALREADY_STARTED;
  }

  mRunning = TRUE;
  Status   = ReportOpenDestination (&Dir, Where, WhereSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DebugReport: not saved, no writable FAT file system (plug in a USB stick)\n"));
    mRunning = FALSE;
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "DebugReport: saving to %s\n", Where));

  ZeroMem (&Parts, sizeof (Parts));
  TextAdd (&Parts, "edk2-rk3576 debug report\n\nsaved to ");
  TextAddUnicode (&Parts, Where);
  TextAdd (&Parts, "\n\nWhat is in this folder, and how saving each part went:\n\n");

  ReportWriteAll (Dir, &Parts);
  if (Pages) {
    WalkForms (Dir, mHiiHandle, &Parts);
  } else {
    TextAdd (&Parts, "pages\\                   not asked for (only from Device Manager > Debug Report)\n");
  }

  if (Parts.Buffer != NULL) {
    ReportWriteFile (Dir, L"README.txt", Parts.Buffer, Parts.Length);
  }

  TextFree (&Parts);
  Dir->Close (Dir);
  DEBUG ((DEBUG_INFO, "DebugReport: saved to %s\n", Where));
  mRunning = FALSE;
  return EFI_SUCCESS;
}

//
// The setup page
//

STATIC
EFI_STATUS
EFIAPI
ReportExtractConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Request,
  OUT EFI_STRING                            *Progress,
  OUT EFI_STRING                            *Results
  )
{
  if ((Progress == NULL) || (Results == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Request;
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
ReportRouteConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Configuration,
  OUT EFI_STRING                            *Progress
  )
{
  if ((Configuration == NULL) || (Progress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Configuration;
  return EFI_NOT_FOUND;
}

STATIC
VOID
ReportPopUp (
  IN EFI_STRING_ID  Line1,
  IN CONST CHAR16   *Line2
  )
{
  CHAR16         *First;
  CHAR16         *Last;
  EFI_INPUT_KEY  Key;

  First = HiiGetString (mHiiHandle, Line1, NULL);
  Last  = HiiGetString (mHiiHandle, STRING_TOKEN (STR_PRESS_ENTER), NULL);
  if ((First != NULL) && (Last != NULL)) {
    do {
      CreatePopUp (EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE, &Key, First, Line2, Last, NULL);
    } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
  }

  if (First != NULL) {
    FreePool (First);
  }

  if (Last != NULL) {
    FreePool (Last);
  }
}

STATIC
EFI_STATUS
EFIAPI
ReportCallback (
  IN     CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN     EFI_BROWSER_ACTION                    Action,
  IN     EFI_QUESTION_ID                       QuestionId,
  IN     UINT8                                 Type,
  IN OUT EFI_IFR_TYPE_VALUE                    *Value,
  OUT    EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  EFI_STATUS  Status;
  CHAR16      Where[256];

  if ((QuestionId != DEBUG_REPORT_QUESTION_ALL) && (QuestionId != DEBUG_REPORT_QUESTION_QUICK)) {
    return EFI_UNSUPPORTED;
  }

  //
  // The browser asks CHANGING first and, when that succeeds, CHANGED;
  // the report runs on CHANGED.
  //
  if (Action == EFI_BROWSER_ACTION_CHANGING) {
    return EFI_SUCCESS;
  }

  if (Action != EFI_BROWSER_ACTION_CHANGED) {
    return EFI_UNSUPPORTED;
  }

  Status = ReportRun ((BOOLEAN)(QuestionId == DEBUG_REPORT_QUESTION_ALL), Where, sizeof (Where));
  if (EFI_ERROR (Status)) {
    ReportPopUp (STRING_TOKEN (STR_NOT_SAVED), L"");
  } else {
    ReportPopUp (STRING_TOKEN (STR_SAVED), Where);
  }

  if (ActionRequest != NULL) {
    *ActionRequest = EFI_BROWSER_ACTION_REQUEST_NONE;
  }

  return EFI_SUCCESS;
}

STATIC EFI_HII_CONFIG_ACCESS_PROTOCOL  mConfigAccess = {
  ReportExtractConfig,
  ReportRouteConfig,
  ReportCallback
};

//
// The hot key
//

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
ReportFindGop (
  VOID
  )
{
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  VOID                          *Path;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }

  for (Index = 0; Index < Count; Index++) {
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, &Path)) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) &&
        (Gop->Mode != NULL) && (Gop->Mode->Info != NULL))
    {
      FreePool (Handles);
      return Gop;
    }
  }

  FreePool (Handles);
  return NULL;
}

/**
  A square in the top left corner for a moment; what was there is put
  back.  The same as ScreenshotDxe's.
**/
STATIC
VOID
ReportShowStatus (
  IN UINT8  Red,
  IN UINT8  Green,
  IN UINT8  Blue
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Square;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Saved;
  UINTN                          Side;

  Gop = ReportFindGop ();
  if (Gop == NULL) {
    return;
  }

  Side  = MIN (STATUS_SIDE, MIN (Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution));
  Saved = AllocatePool (Side * Side * sizeof (*Saved));
  if (Saved == NULL) {
    return;
  }

  Square.Red      = Red;
  Square.Green    = Green;
  Square.Blue     = Blue;
  Square.Reserved = 0;
  if (!EFI_ERROR (Gop->Blt (Gop, Saved, EfiBltVideoToBltBuffer, 0, 0, 0, 0, Side, Side, 0))) {
    Gop->Blt (Gop, &Square, EfiBltVideoFill, 0, 0, 0, 0, Side, Side, 0);
    gBS->Stall (STATUS_SHOW_US);
    Gop->Blt (Gop, Saved, EfiBltBufferToVideo, 0, 0, 0, 0, Side, Side, 0);
  }

  FreePool (Saved);
}

STATIC
VOID
EFIAPI
ReportHotKeyRun (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  CHAR16      Where[256];
  EFI_STATUS  Status;

  if (mGone || mRunning) {
    return;
  }

  Status = ReportRun (FALSE, Where, sizeof (Where));
  if (Status == EFI_NOT_FOUND) {
    ReportShowStatus (0xff, 0xff, 0x00);
  } else if (EFI_ERROR (Status)) {
    ReportShowStatus (0xff, 0x00, 0x00);
  } else {
    ReportShowStatus (0x00, 0xff, 0x00);
  }
}

STATIC
EFI_STATUS
EFIAPI
ReportHotKey (
  IN EFI_KEY_DATA  *KeyData
  )
{
  if (!mGone) {
    gBS->SignalEvent (mHotKeyEvent);
  }

  return EFI_SUCCESS;
}

/**
  Ctrl+Alt+F11, with the console splitter (the text input without a
  device path), which passes it on to every keyboard.
**/
STATIC
VOID
EFIAPI
ReportTextInNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  STATIC CONST UINT32  Shift[4] = {
    EFI_LEFT_CONTROL_PRESSED  | EFI_LEFT_ALT_PRESSED,
    EFI_LEFT_CONTROL_PRESSED  | EFI_RIGHT_ALT_PRESSED,
    EFI_RIGHT_CONTROL_PRESSED | EFI_LEFT_ALT_PRESSED,
    EFI_RIGHT_CONTROL_PRESSED | EFI_RIGHT_ALT_PRESSED
  };
  EFI_HANDLE                         *Handles;
  UINTN                              Count;
  UINTN                              Index;
  UINTN                              Combo;
  VOID                               *Path;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *TextInEx;
  EFI_KEY_DATA                       Key;
  VOID                               *NotifyHandle;

  if ((mSplitter != NULL) || mGone ||
      EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleTextInputExProtocolGuid, NULL, &Count, &Handles)))
  {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, &Path)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleTextInputExProtocolGuid, (VOID **)&TextInEx)))
    {
      continue;
    }

    for (Combo = 0; Combo < ARRAY_SIZE (Shift); Combo++) {
      ZeroMem (&Key, sizeof (Key));
      Key.Key.ScanCode           = SCAN_F11;
      Key.KeyState.KeyShiftState = EFI_SHIFT_STATE_VALID | Shift[Combo];
      if (!EFI_ERROR (TextInEx->RegisterKeyNotify (TextInEx, &Key, ReportHotKey, &NotifyHandle))) {
        mSplitter = TextInEx;
      }
    }

    if (mSplitter != NULL) {
      DEBUG ((DEBUG_INFO, "DebugReport: Ctrl+Alt+F11 (F11 on serial) saves a debug report\n"));
      break;
    }
  }

  FreePool (Handles);
}

STATIC
VOID
EFIAPI
ReportExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mGone = TRUE;
}

STATIC
VOID
EFIAPI
ReportEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  WalkInstallKeys ();
}

EFI_STATUS
EFIAPI
DebugReportEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Status = WalkInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReportHotKeyRun, NULL, &mHotKeyEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // The setup page, under Device Manager.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHiiDriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mHiiPath,
                  &gEfiHiiConfigAccessProtocolGuid,
                  &mConfigAccess,
                  NULL
                  );
  if (!EFI_ERROR (Status)) {
    mHiiHandle = HiiAddPackages (&mFormSetGuid, mHiiDriverHandle, DebugReportDxeStrings, DebugReportVfrBin, NULL);
    if (mHiiHandle == NULL) {
      DEBUG ((DEBUG_ERROR, "DebugReport: could not add the setup page\n"));
    }
  }

  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ReportExitBootServices, NULL, &gEfiEventExitBootServicesGuid, &Event);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReportEndOfDxe, NULL, &gEfiEndOfDxeEventGroupGuid, &Event);

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReportTextInNotify, NULL, &Event);
  if (!EFI_ERROR (Status)) {
    gBS->RegisterProtocolNotify (&gEfiSimpleTextInputExProtocolGuid, Event, &mTextInRegistration);
    ReportTextInNotify (NULL, NULL);
  }

  return EFI_SUCCESS;
}
