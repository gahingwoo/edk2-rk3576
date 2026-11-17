/** @file
 *
 *  The "About" page of the RK3576 platform form: what firmware this is,
 *  what board it runs on, what it found, and where it comes from.
 *
 *  Every line is one HII string, "<label> <value>", set per language: the
 *  label comes from RK3576DxeHii.uni in that language, the value is read
 *  here.  A value that cannot be read keeps the line's default text from
 *  the .uni file, which says "unknown".  Nothing here is a hard-coded
 *  measurement: memory comes from SMBIOS (as on the front page), the CPU
 *  clocks from SCMI (as CpuPerf.c sets them), the display from the VOP2
 *  CRTC protocol and the GOP, the touch screen from the same test as
 *  PlatformBm.c.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/RockchipCrtcProtocol.h>
#include <Library/DrmModes.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/Smbios.h>

#include "About.h"
#include "CpuPerf.h"

#define ABOUT_TEXT_LENGTH  256

STATIC EFI_HII_HANDLE  mAboutHiiHandle;

/**
  Set LineId, in every language of the package list, to LabelId in that
  language followed by a value: Value if given, else ValueId in that
  language.
**/
STATIC
VOID
AboutSetLine (
  IN EFI_STRING_ID  LineId,
  IN EFI_STRING_ID  LabelId,
  IN CONST CHAR16   *Value OPTIONAL,
  IN EFI_STRING_ID  ValueId
  )
{
  CHAR8   *Languages;
  CHAR8   *Lang;
  CHAR8   *Next;
  CHAR16  *Label;
  CHAR16  *Translated;
  CHAR16  Text[ABOUT_TEXT_LENGTH];

  Languages = HiiGetSupportedLanguages (mAboutHiiHandle);
  if (Languages == NULL) {
    return;
  }

  for (Lang = Languages; *Lang != '\0'; Lang = Next) {
    for (Next = Lang; (*Next != '\0') && (*Next != ';'); Next++) {
    }

    if (*Next == ';') {
      *Next++ = '\0';
    }

    Label = HiiGetString (mAboutHiiHandle, LabelId, Lang);
    if (Label == NULL) {
      continue;
    }

    Translated = NULL;
    if (Value == NULL) {
      Translated = HiiGetString (mAboutHiiHandle, ValueId, Lang);
    }

    if ((Value != NULL) || (Translated != NULL)) {
      UnicodeSPrint (Text, sizeof (Text), L"%s%s", Label, (Value != NULL) ? Value : Translated);
      HiiSetString (mAboutHiiHandle, LineId, Text, Lang);
    }

    if (Translated != NULL) {
      FreePool (Translated);
    }

    FreePool (Label);
  }

  FreePool (Languages);
}

/**
  Installed memory in MiB, added up from the SMBIOS type 19 records the way
  UiApp's front page does it.

  @return The size, or 0 if there is no SMBIOS or no type 19 record.
**/
STATIC
UINT64
AboutMemoryMiB (
  VOID
  )
{
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE19      *Type19;
  UINT64                   KiB;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios))) {
    return 0;
  }

  KiB    = 0;
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Type   = SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL))) {
    Type19 = (SMBIOS_TABLE_TYPE19 *)Record;
    if (Type19->StartingAddress != 0xFFFFFFFF) {
      KiB += (UINT64)Type19->EndingAddress - Type19->StartingAddress + 1;
    } else {
      KiB += RShiftU64 (Type19->ExtendedEndingAddress - Type19->ExtendedStartingAddress + 1, 10);
    }
  }

  return RShiftU64 (KiB, 10);
}

/**
  Is there a touch screen that types keys?  The test PlatformBm.c uses: a
  device that is both a text input and an absolute pointer, other than the
  console splitter's own handle.
**/
STATIC
BOOLEAN
AboutHasTouchKeys (
  VOID
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  VOID        *Interface;
  BOOLEAN     Present;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles))) {
    return FALSE;
  }

  Present = FALSE;
  for (Index = 0; Index < Count; Index++) {
    if ((Handles[Index] != gST->ConsoleInHandle) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleTextInProtocolGuid, &Interface)))
    {
      Present = TRUE;
      break;
    }
  }

  FreePool (Handles);
  return Present;
}

/**
  Append the display outputs the VOP2 driver has enabled, and the GOP's
  resolution, to Text.

  @retval TRUE   Something was found.
  @retval FALSE  No enabled output and no GOP.
**/
STATIC
BOOLEAN
AboutDisplay (
  OUT CHAR16  *Text,
  IN  UINTN   TextSize
  )
{
  ROCKCHIP_CRTC_PROTOCOL        *Crtc;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINTN                         Index;
  CONST CHAR16                  *Name;
  BOOLEAN                       Found;

  Text[0] = L'\0';
  Found   = FALSE;

  if (!EFI_ERROR (gBS->LocateProtocol (&gRockchipCrtcProtocolGuid, NULL, (VOID **)&Crtc))) {
    for (Index = 0; Index < ARRAY_SIZE (Crtc->Vps); Index++) {
      if (!Crtc->Vps[Index].Enable) {
        continue;
      }

      switch (Crtc->Vps[Index].OutputType) {
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:
          Name = L"HDMI";
          break;
        case DRM_MODE_CONNECTOR_DSI:
          Name = L"MIPI DSI";
          break;
        case DRM_MODE_CONNECTOR_eDP:
          Name = L"eDP";
          break;
        case DRM_MODE_CONNECTOR_DisplayPort:
          Name = L"DP";
          break;
        default:
          Name = NULL;
          break;
      }

      if (Name == NULL) {
        continue;
      }

      UnicodeSPrint (
        Text + StrLen (Text),
        TextSize - StrSize (Text) + sizeof (CHAR16),
        Found ? L" + %s" : L"%s",
        Name
        );
      Found = TRUE;
    }
  }

  if (EFI_ERROR (gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) &&
      EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop)))
  {
    return Found;
  }

  UnicodeSPrint (
    Text + StrLen (Text),
    TextSize - StrSize (Text) + sizeof (CHAR16),
    Found ? L", %ux%u" : L"%ux%u",
    Gop->Mode->Info->HorizontalResolution,
    Gop->Mode->Info->VerticalResolution
    );
  return TRUE;
}

/**
  Fill in what is only known once the consoles are connected.
**/
STATIC
VOID
EFIAPI
AboutOnAfterConsole (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  CHAR16  Value[ABOUT_TEXT_LENGTH];
  UINT64  LittleHz;
  UINT64  BigHz;
  UINT64  MemoryMiB;

  gBS->CloseEvent (Event);

  if (!EFI_ERROR (RK3576GetCpuClusterRates (&LittleHz, &BigHz))) {
    UnicodeSPrint (
      Value,
      sizeof (Value),
      L"Cortex-A72 %lu MHz, Cortex-A53 %lu MHz",
      DivU64x32 (BigHz, 1000000),
      DivU64x32 (LittleHz, 1000000)
      );
    AboutSetLine (STRING_TOKEN (STR_ABOUT_CPU_CLOCK), STRING_TOKEN (STR_ABOUT_CPU_CLOCK_LABEL), Value, 0);
  }

  MemoryMiB = AboutMemoryMiB ();
  if (MemoryMiB != 0) {
    UnicodeSPrint (Value, sizeof (Value), L"%lu MB", MemoryMiB);
    AboutSetLine (STRING_TOKEN (STR_ABOUT_MEMORY), STRING_TOKEN (STR_ABOUT_MEMORY_LABEL), Value, 0);
  }

  if (AboutDisplay (Value, sizeof (Value))) {
    AboutSetLine (STRING_TOKEN (STR_ABOUT_DISPLAY), STRING_TOKEN (STR_ABOUT_DISPLAY_LABEL), Value, 0);
  }

  AboutSetLine (
    STRING_TOKEN (STR_ABOUT_TOUCH),
    STRING_TOKEN (STR_ABOUT_TOUCH_LABEL),
    NULL,
    AboutHasTouchKeys () ? STRING_TOKEN (STR_ABOUT_FOUND) : STRING_TOKEN (STR_ABOUT_NOT_FOUND)
    );
}

VOID
RK3576InitAboutPage (
  IN EFI_HII_HANDLE  HiiHandle,
  IN CONST CHAR8     *BuildStamp
  )
{
  CHAR16      Value[ABOUT_TEXT_LENGTH];
  EFI_EVENT   Event;
  EFI_STATUS  Status;

  mAboutHiiHandle = HiiHandle;

  AboutSetLine (
    STRING_TOKEN (STR_ABOUT_VERSION),
    STRING_TOKEN (STR_ABOUT_VERSION_LABEL),
    (CONST CHAR16 *)PcdGetPtr (PcdFirmwareVersionString),
    0
    );

  UnicodeSPrint (Value, sizeof (Value), L"%a", BuildStamp);
  AboutSetLine (STRING_TOKEN (STR_ABOUT_BUILD), STRING_TOKEN (STR_ABOUT_BUILD_LABEL), Value, 0);

  UnicodeSPrint (Value, sizeof (Value), L"%a", (CHAR8 *)PcdGetPtr (PcdPlatformName));
  AboutSetLine (STRING_TOKEN (STR_ABOUT_BOARD), STRING_TOKEN (STR_ABOUT_BOARD_LABEL), Value, 0);

  UnicodeSPrint (Value, sizeof (Value), L"%a", (CHAR8 *)PcdGetPtr (PcdPlatformVendorName));
  AboutSetLine (STRING_TOKEN (STR_ABOUT_VENDOR), STRING_TOKEN (STR_ABOUT_VENDOR_LABEL), Value, 0);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  AboutOnAfterConsole,
                  NULL,
                  &gRockchipEventPlatformBmAfterConsoleGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "RK3576Dxe: About page will not show detected hardware: %r\n", Status));
  }
}
