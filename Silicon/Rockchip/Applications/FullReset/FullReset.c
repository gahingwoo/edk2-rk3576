/** @file
 *
 *  Reset All Settings.
 *
 *  Asks in a Yes/No popup, then sets the one-shot FullResetRequest variable
 *  and restarts.  The erase itself happens in RkFvbDxe on the next boot,
 *  before the variable driver starts, so that no variable can refuse to go:
 *  read-only, locked and authenticated variables are erased with the store
 *  they are in, rather than deleted one at a time through SetVariable().
 *  See Include/Guid/FullResetRequest.h.
 *
 *  Started from the front page (patches/0016) and by the F9 hotkey that
 *  PlatformBootManagerLib registers, which also works from a serial
 *  terminal while the boot countdown runs.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/FullResetRequest.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/HiiPopup.h>

extern UINT8  FullResetStrings[];

/**
  Show Message with Yes and No, or with OK when AskYesNo is FALSE.

  @return TRUE if the user chose Yes.
**/
STATIC
BOOLEAN
FullResetPopup (
  IN EFI_HII_HANDLE  HiiHandle,
  IN EFI_STRING_ID   Message,
  IN BOOLEAN         AskYesNo
  )
{
  EFI_STATUS               Status;
  EFI_HII_POPUP_PROTOCOL   *HiiPopup;
  EFI_HII_POPUP_SELECTION  Selection;
  EFI_INPUT_KEY            Key;
  CHAR16                   *Text;
  CHAR16                   *Prompt;
  UINTN                    Index;

  //
  // Throw away keys typed before the question was on screen, so that an
  // Enter meant for the boot countdown does not answer it.
  //
  gST->ConIn->Reset (gST->ConIn, FALSE);

  Status = gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid, NULL, (VOID **)&HiiPopup);
  if (!EFI_ERROR (Status)) {
    Selection = EfiHiiPopupSelectionNo;
    Status    = HiiPopup->CreatePopup (
                            HiiPopup,
                            EfiHiiPopupStyleWarning,
                            AskYesNo ? EfiHiiPopupTypeYesNo : EfiHiiPopupTypeOk,
                            HiiHandle,
                            Message,
                            &Selection
                            );
    if (!EFI_ERROR (Status)) {
      return AskYesNo && (Selection == EfiHiiPopupSelectionYes);
    }
  }

  //
  // No display engine: a plain text popup, which also reaches a serial
  // console.  Only 'y' confirms.
  //
  Text   = HiiGetString (HiiHandle, Message, NULL);
  Prompt = HiiGetString (HiiHandle, AskYesNo ? STRING_TOKEN (STR_FULL_RESET_PRESS_Y) : STRING_TOKEN (STR_FULL_RESET_PRESS_ANY), NULL);
  if ((Text != NULL) && (Prompt != NULL)) {
    Print (L"\n%s\n\n%s\n", Text, Prompt);
  }

  if (Text != NULL) {
    FreePool (Text);
  }

  if (Prompt != NULL) {
    FreePool (Prompt);
  }

  do {
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  } while (Status == EFI_NOT_READY);

  return AskYesNo && !EFI_ERROR (Status) && ((Key.UnicodeChar | 0x20) == L'y');
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS      Status;
  EFI_HII_HANDLE  HiiHandle;
  UINT32          Value;

  HiiHandle = HiiAddPackages (&gEfiCallerIdGuid, ImageHandle, FullResetStrings, NULL);
  if (HiiHandle == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (!FullResetPopup (HiiHandle, STRING_TOKEN (STR_FULL_RESET_CONFIRM), TRUE)) {
    HiiRemovePackages (HiiHandle);
    return EFI_ABORTED;
  }

  Value  = FULL_RESET_REQUEST_VALUE;
  Status = gRT->SetVariable (
                  FULL_RESET_REQUEST_NAME,
                  &gRockchipFullResetRequestGuid,
                  FULL_RESET_REQUEST_ATTRIBUTES,
                  sizeof (Value),
                  &Value
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FullReset: cannot set the request: %r\n", Status));
    FullResetPopup (HiiHandle, STRING_TOKEN (STR_FULL_RESET_FAILED), FALSE);
    HiiRemovePackages (HiiHandle);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "FullReset: requested; restarting to erase the variable store\n"));

  //
  // On SD/eMMC the request reaches the medium in RkFvbDxe's reset notify,
  // which runs inside this call.  On SPI NOR it already has.
  //
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  return EFI_SUCCESS;
}
