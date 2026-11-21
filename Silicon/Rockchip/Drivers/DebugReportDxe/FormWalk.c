/** @file
 *
 *  A screenshot of every setup page.
 *
 *  Every form set in the HII database is looked through for its forms
 *  (EFI_IFR_FORM_OP and EFI_IFR_FORM_MAP_OP), and each form is shown with
 *  the form browser's SendForm (), from inside the setup browser callback
 *  of the debug report page, which the browser allows (it saves and
 *  restores its own state around a nested SendForm).  A timer then takes
 *  the screenshot through ScreenshotDxe and types Esc on a keyboard of this
 *  driver's own, which closes the form and returns from SendForm ().  Esc
 *  is typed again every second in case one Esc only went back to a parent
 *  form; after 10 tries it stops, and a person can still press Esc.  No
 *  Esc is left over once SendForm () has returned.
 *
 *  Two form sets are not walked: the debug report page itself, which is
 *  open underneath, and UiApp's front page, which does not close on Esc
 *  (on the board the walk stayed there, and the report never finished).
 *  pages.txt is written again after every page, so that it is there even
 *  if a page does not let go.
 *
 *  Nothing is changed in any form: only Esc is typed, and a form left with
 *  Esc and no change asks nothing.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Guid/ConsoleInDevice.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FormBrowser2.h>
#include <Protocol/HiiDatabase.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>

#include "DebugReport.h"

#define WALK_TICK_100NS    (250 * 10000)   // 250 ms
#define WALK_SHOT_TICKS    4               // the screenshot after a second
#define WALK_ESC_EVERY     4               // then Esc every second
#define WALK_MAX_TICKS     (WALK_SHOT_TICKS + 10 * WALK_ESC_EVERY)
#define WALK_MAX_FORMS     256

//
// UiApp's front page (FRONT_PAGE_FORMSET_GUID in UiApp/FrontPage.h).
//
#define WALK_FRONT_PAGE_FORMSET_GUID \
  { 0x9e0c30bc, 0x3f06, 0x4ba6, { 0x82, 0x88, 0x09, 0x17, 0x9b, 0x85, 0x5d, 0xbe } }

#define WALK_KEYS_DEVICE_GUID \
  { 0x86855d8c, 0x85fe, 0x4925, { 0x9b, 0x58, 0x31, 0xb0, 0x8b, 0x6f, 0xde, 0x83 } }

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} WALK_DEVICE_PATH;

typedef struct {
  EFI_HII_HANDLE    Handle;
  EFI_GUID          FormSet;
  EFI_STRING_ID     FormSetTitle;
  UINT16            FormId;
  EFI_STRING_ID     FormTitle;
} WALK_FORM;

STATIC WALK_DEVICE_PATH  mKeysPath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    WALK_KEYS_DEVICE_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_GUID    mFrontPageGuid = WALK_FRONT_PAGE_FORMSET_GUID;
STATIC EFI_HANDLE  mKeysHandle;
STATIC EFI_EVENT   mKeysWait;
STATIC UINTN       mEscPending;

// the form being shown
STATIC EFI_EVENT          mWalkTimer;
STATIC UINTN              mWalkTicks;
STATIC EFI_FILE_PROTOCOL  *mWalkDir;
STATIC CHAR16             mWalkName[64];
STATIC EFI_STATUS         mWalkShot;

//
// The keyboard: it only ever types Esc.
//

STATIC
EFI_STATUS
EFIAPI
KeysReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  mEscPending = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeysRead (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  EFI_TPL  Tpl;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mEscPending == 0) {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }

  mEscPending--;
  gBS->RestoreTPL (Tpl);
  Key->ScanCode    = SCAN_ESC;
  Key->UnicodeChar = 0;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
KeysWait (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mEscPending != 0) {
    gBS->SignalEvent (Event);
  }
}

STATIC
EFI_STATUS
EFIAPI
KeysResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  return KeysReset (NULL, ExtendedVerification);
}

STATIC
EFI_STATUS
EFIAPI
KeysReadEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (KeyData, sizeof (*KeyData));
  return KeysRead (NULL, &KeyData->Key);
}

STATIC
EFI_STATUS
EFIAPI
KeysSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_UNSUPPORTED;
}

//
// Hot keys are not this keyboard's business: Esc is never one.  The
// console splitter registers every hot key with every keyboard, so these
// accept and forget.
//
STATIC UINT8  mNotifyToken;

STATIC
EFI_STATUS
EFIAPI
KeysRegisterNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  if ((KeyData == NULL) || (KeyNotificationFunction == NULL) || (NotifyHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *NotifyHandle = &mNotifyToken;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
KeysUnregisterNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_SUCCESS;
}

STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL  mKeys = {
  KeysReset,
  KeysRead,
  NULL
};

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mKeysEx = {
  KeysResetEx,
  KeysReadEx,
  NULL,
  KeysSetState,
  KeysRegisterNotify,
  KeysUnregisterNotify
};

STATIC
VOID
KeysTypeEsc (
  VOID
  )
{
  EFI_TPL  Tpl;

  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mEscPending < 4) {
    mEscPending++;
  }

  gBS->RestoreTPL (Tpl);
  gBS->SignalEvent (mKeysWait);
}

EFI_STATUS
WalkInstallKeys (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, KeysWait, NULL, &mKeysWait);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mKeys.WaitForKey     = mKeysWait;
  mKeysEx.WaitForKeyEx = mKeysWait;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mKeysHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mKeysPath,
                  &gEfiSimpleTextInProtocolGuid,
                  &mKeys,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &mKeysEx,
                  &gEfiConsoleInDeviceGuid,
                  NULL,
                  NULL
                  );
  if (!EFI_ERROR (Status)) {
    // the console splitter takes it now; BDS connects it again later
    gBS->ConnectController (mKeysHandle, NULL, NULL, TRUE);
  }

  return Status;
}

//
// The walk
//

STATIC
VOID
EFIAPI
WalkTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mWalkTicks++;
  if (mWalkTicks == WALK_SHOT_TICKS) {
    mWalkShot = ReportScreenshot (mWalkDir, mWalkName);
    KeysTypeEsc ();
  } else if ((mWalkTicks > WALK_SHOT_TICKS) && (mWalkTicks <= WALK_MAX_TICKS) &&
             (((mWalkTicks - WALK_SHOT_TICKS) % WALK_ESC_EVERY) == 0))
  {
    KeysTypeEsc ();
  }
}

/**
  The forms of one HII handle, from its form packages.
**/
STATIC
VOID
WalkCollect (
  IN     EFI_HII_DATABASE_PROTOCOL  *Database,
  IN     EFI_HII_HANDLE             Handle,
  IN OUT WALK_FORM                  *Forms,
  IN OUT UINTN                      *Count
  )
{
  EFI_HII_PACKAGE_LIST_HEADER  *List;
  EFI_HII_PACKAGE_HEADER       *Package;
  EFI_IFR_OP_HEADER            *Op;
  UINTN                        Size;
  UINT8                        *End;
  UINT8                        *PackageEnd;
  EFI_GUID                     FormSet;
  EFI_STRING_ID                FormSetTitle;
  BOOLEAN                      InFormSet;

  Size = 0;
  if (Database->ExportPackageLists (Database, Handle, &Size, NULL) != EFI_BUFFER_TOO_SMALL) {
    return;
  }

  List = AllocatePool (Size);
  if ((List == NULL) || EFI_ERROR (Database->ExportPackageLists (Database, Handle, &Size, List))) {
    if (List != NULL) {
      FreePool (List);
    }

    return;
  }

  InFormSet    = FALSE;
  FormSetTitle = 0;
  ZeroMem (&FormSet, sizeof (FormSet));
  End     = (UINT8 *)List + MIN (Size, List->PackageLength);
  Package = (EFI_HII_PACKAGE_HEADER *)(List + 1);
  while (((UINT8 *)Package + sizeof (*Package) <= End) && (Package->Type != EFI_HII_PACKAGE_END) && (Package->Length != 0)) {
    PackageEnd = MIN ((UINT8 *)Package + Package->Length, End);
    if (Package->Type == EFI_HII_PACKAGE_FORMS) {
      for (Op = (EFI_IFR_OP_HEADER *)(Package + 1);
           ((UINT8 *)Op + sizeof (*Op) <= PackageEnd) && (Op->Length != 0);
           Op = (EFI_IFR_OP_HEADER *)((UINT8 *)Op + Op->Length))
      {
        if ((Op->OpCode == EFI_IFR_FORM_SET_OP) && (Op->Length >= sizeof (EFI_IFR_FORM_SET))) {
          CopyMem (&FormSet, &((EFI_IFR_FORM_SET *)Op)->Guid, sizeof (EFI_GUID));
          FormSetTitle = ((EFI_IFR_FORM_SET *)Op)->FormSetTitle;
          InFormSet    = TRUE;
        } else if (InFormSet && (*Count < WALK_MAX_FORMS) &&
                   (((Op->OpCode == EFI_IFR_FORM_OP) && (Op->Length >= sizeof (EFI_IFR_FORM))) ||
                    ((Op->OpCode == EFI_IFR_FORM_MAP_OP) && (Op->Length >= sizeof (EFI_IFR_FORM_MAP)))))
        {
          Forms[*Count].Handle       = Handle;
          Forms[*Count].FormSetTitle = FormSetTitle;
          CopyMem (&Forms[*Count].FormSet, &FormSet, sizeof (EFI_GUID));
          if (Op->OpCode == EFI_IFR_FORM_OP) {
            Forms[*Count].FormId    = ((EFI_IFR_FORM *)Op)->FormId;
            Forms[*Count].FormTitle = ((EFI_IFR_FORM *)Op)->FormTitle;
          } else {
            Forms[*Count].FormId    = ((EFI_IFR_FORM_MAP *)Op)->FormId;
            Forms[*Count].FormTitle = 0;
          }

          (*Count)++;
        }
      }
    }

    Package = (EFI_HII_PACKAGE_HEADER *)PackageEnd;
  }

  FreePool (List);
}

/**
  A title as part of a file name: ASCII letters and digits, each run of
  anything else one '-', none at either end, 32 characters at most.  Empty
  when the title has nothing usable.
**/
STATIC
VOID
WalkTitlePart (
  IN  EFI_HII_HANDLE  Handle,
  IN  EFI_STRING_ID   Title,
  OUT CHAR16          *Part,
  IN  UINTN           PartChars
  )
{
  CHAR16   *String;
  UINTN    Length;
  UINTN    Index;
  CHAR16   C;
  BOOLEAN  Dash;

  Part[0] = 0;
  if (Title == 0) {
    return;
  }

  // English when there is English, else whatever there is
  String = HiiGetString (Handle, Title, "en-US");
  if (String == NULL) {
    String = HiiGetString (Handle, Title, NULL);
  }

  if (String == NULL) {
    return;
  }

  Length = 0;
  Dash   = FALSE;
  for (Index = 0; (String[Index] != 0) && (Length + 1 < MIN (PartChars, 33)); Index++) {
    C = String[Index];
    if (((C >= L'a') && (C <= L'z')) || ((C >= L'A') && (C <= L'Z')) || ((C >= L'0') && (C <= L'9'))) {
      if (Dash && (Length != 0)) {
        Part[Length++] = L'-';
        if (Length + 1 >= MIN (PartChars, 33)) {
          break;
        }
      }

      Part[Length++] = C;
      Dash           = FALSE;
    } else {
      Dash = TRUE;
    }
  }

  // a trailing '-' cannot happen: one is only written before a letter
  Part[Length] = 0;
  FreePool (String);
}

/**
  NNN-<form title>.png, else the form set title, else form-XXXX.
**/
STATIC
VOID
WalkFileName (
  IN  UINTN            Number,
  IN  CONST WALK_FORM  *Form,
  OUT CHAR16           *Name,
  IN  UINTN            NameSize
  )
{
  CHAR16  Part[40];

  WalkTitlePart (Form->Handle, Form->FormTitle, Part, ARRAY_SIZE (Part));
  if (Part[0] == 0) {
    WalkTitlePart (Form->Handle, Form->FormSetTitle, Part, ARRAY_SIZE (Part));
  }

  if (Part[0] == 0) {
    UnicodeSPrint (Part, sizeof (Part), L"form-%04x", Form->FormId);
  }

  UnicodeSPrint (Name, NameSize, L"%03u-%s.png", Number, Part);
}

VOID
WalkForms (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN     EFI_HII_HANDLE     Skip,
  IN     CONST EFI_GUID     *SkipFormSet,
  IN OUT REPORT_TEXT        *Parts
  )
{
  EFI_STATUS                   Status;
  EFI_FORM_BROWSER2_PROTOCOL   *Browser;
  EFI_HII_DATABASE_PROTOCOL    *Database;
  EFI_HII_HANDLE               *Handles;
  WALK_FORM                    *Forms;
  UINTN                        Count;
  UINTN                        Index;
  EFI_FILE_PROTOCOL            *Pages;
  EFI_BROWSER_ACTION_REQUEST   Request;
  REPORT_TEXT                  List;
  CHAR16                       *Title;
  UINTN                        Shown;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiFormBrowser2ProtocolGuid, NULL, (VOID **)&Browser)) ||
      EFI_ERROR (gBS->LocateProtocol (&gEfiHiiDatabaseProtocolGuid, NULL, (VOID **)&Database)))
  {
    TextAdd (Parts, "pages\\                   no form browser\n");
    return;
  }

  if (EFI_ERROR (ReportOpenDir (Dir, L"pages", &Pages))) {
    TextAdd (Parts, "pages\\                   could not be made\n");
    return;
  }

  Forms   = AllocateZeroPool (WALK_MAX_FORMS * sizeof (WALK_FORM));
  Handles = HiiGetHiiHandles (NULL);
  if ((Forms == NULL) || (Handles == NULL) || (mWalkTimer == NULL)) {
    TextAdd (Parts, "pages\\                   out of resources\n");
    goto Done;
  }

  Count = 0;
  for (Index = 0; Handles[Index] != NULL; Index++) {
    if (Handles[Index] != Skip) {
      WalkCollect (Database, Handles[Index], Forms, &Count);
    }
  }

  ZeroMem (&List, sizeof (List));
  TextAdd (
    &List,
    "Setup pages, one screenshot each, in the order they were shown, with the\n"
    "form set GUID and form id to find each again.  Not walked: the front page\n"
    "(it does not close on Esc) and the Debug Report page (it is open\n"
    "underneath; screen.png shows it).\n\n"
    );

  Shown = 0;
  for (Index = 0; Index < Count; Index++) {
    if (CompareGuid (&Forms[Index].FormSet, &mFrontPageGuid) || CompareGuid (&Forms[Index].FormSet, SkipFormSet)) {
      TextAdd (&List, "(not walked)  form set %g form 0x%04x\n", &Forms[Index].FormSet, Forms[Index].FormId);
      continue;
    }

    Shown++;
    WalkFileName (Shown, &Forms[Index], mWalkName, sizeof (mWalkName));
    mWalkDir   = Pages;
    mWalkTicks = 0;
    mWalkShot  = EFI_NOT_STARTED;
    mEscPending = 0;

    gBS->SetTimer (mWalkTimer, TimerPeriodic, WALK_TICK_100NS);
    Request = EFI_BROWSER_ACTION_REQUEST_NONE;
    Status  = Browser->SendForm (Browser, &Forms[Index].Handle, 1, &Forms[Index].FormSet, Forms[Index].FormId, NULL, &Request);
    gBS->SetTimer (mWalkTimer, TimerCancel, 0);
    mEscPending = 0;

    TextAdd (&List, "%s  form set %g form 0x%04x  ", mWalkName, &Forms[Index].FormSet, Forms[Index].FormId);
    Title = (Forms[Index].FormSetTitle != 0) ? HiiGetString (Forms[Index].Handle, Forms[Index].FormSetTitle, "en-US") : NULL;
    TextAddUnicode (&List, Title);
    if (Title != NULL) {
      FreePool (Title);
    }

    TextAdd (&List, "  shown %r, screenshot %r\n", Status, mWalkShot);

    // again after every page: there even if a later one never lets go
    if (List.Buffer != NULL) {
      ReportWriteFile (Pages, L"pages.txt", List.Buffer, List.Length);
    }
  }

  if (List.Buffer != NULL) {
    ReportWriteFile (Pages, L"pages.txt", List.Buffer, List.Length);
  }

  TextFree (&List);
  TextAdd (Parts, "pages\\                   %u of %u forms shown (pages\\pages.txt)\n", Shown, Count);

Done:
  if (Forms != NULL) {
    FreePool (Forms);
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  Pages->Close (Pages);
}

/**
  The timer the walk uses; made once.
**/
EFI_STATUS
WalkInit (
  VOID
  )
{
  return gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, WalkTick, NULL, &mWalkTimer);
}
