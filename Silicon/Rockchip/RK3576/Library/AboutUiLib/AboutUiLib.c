/** @file
 *
 *  The "About" page: what firmware this is, what board it runs on, what it
 *  found, and where it comes from.
 *
 *  A NULL library of UiApp, like DeviceManagerUiLib: its formset has the
 *  front page class, so UiApp lists it next to Device Manager, Boot Manager
 *  and Boot Maintenance Manager.  It runs when the setup UI starts, after the
 *  consoles are connected, so everything can be read then.
 *
 *  Every line is one HII string, label plus value, set per language: the
 *  label comes from AboutUiLibStrings.uni in that language, the value is
 *  read here.  A value that cannot be read keeps the line's default text from
 *  the .uni file, which says "unknown".  Nothing here is a hard-coded
 *  measurement: memory comes from SMBIOS (as on the front page), the CPU
 *  clocks from SCMI (the clocks RK3576Dxe CpuPerf.c sets), the display from
 *  the VOP2 CRTC protocol and the GOP, the touch screen from the same test as
 *  PlatformBm.c.
 *
 *  The clock line is the one value that moves: it is filled in again each
 *  time the page is opened (the FORM_OPEN callback of the hidden question in
 *  AboutUiLibVfr.vfr), from gRT->GetTime(), with where the time came from
 *  when the firmware knows (the L"TimeSource" variable, Guid/TimeSource.h).
 *  It does not tick while the page is open: a refresh interval would make
 *  the browser repaint the whole page every second, on the serial console
 *  too.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/TimeSource.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/ArmScmiClock2Protocol.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/RockchipCrtcProtocol.h>
#include <Library/DrmModes.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/Smbios.h>
#include <ScmiDefinitions.h>

#include "AboutFormSetGuid.h"

extern UINT8  AboutUiLibVfrBin[];
extern UINT8  AboutUiLibStrings[];

#define ABOUT_TEXT_LENGTH  256

STATIC EFI_HII_HANDLE  mAboutHiiHandle;

/**
  Set LineId, in every language of the package list, to LabelId in that
  language followed by a value: Value if given, then ValueId in that
  language if given (not 0).
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
    if (ValueId != 0) {
      Translated = HiiGetString (mAboutHiiHandle, ValueId, Lang);
    }

    if ((Value != NULL) || (Translated != NULL)) {
      UnicodeSPrint (
        Text,
        sizeof (Text),
        L"%s%s%s",
        Label,
        (Value != NULL) ? Value : L"",
        (Translated != NULL) ? Translated : L""
        );
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
  Read the current rates of the two CPU clusters over SCMI.
**/
STATIC
EFI_STATUS
AboutCpuClusterRates (
  OUT UINT64  *LittleHz,
  OUT UINT64  *BigHz
  )
{
  EFI_STATUS            Status;
  SCMI_CLOCK2_PROTOCOL  *Clock;

  Status = gBS->LocateProtocol (&gArmScmiClock2ProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Clock->RateGet (Clock, SCMI_ARMCLK_L, LittleHz);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Clock->RateGet (Clock, SCMI_ARMCLK_B, BigHz);
}

/**
  The year this file was compiled, from __DATE__ ("Mmm dd yyyy").
**/
STATIC
UINT16
AboutBuildYear (
  VOID
  )
{
  CONST CHAR8  *Date;
  UINT16       Year;
  UINTN        Index;

  Date = __DATE__;
  Year = 0;
  for (Index = 7; Index < 11; Index++) {
    Year = (UINT16)(Year * 10 + (Date[Index] - '0'));
  }

  return Year;
}

/**
  Fill in the clock line: the time gRT->GetTime() gives now, and where it
  came from when the firmware knows.

  "not set" when GetTime() fails, when the year is 2000 or before (an 8563
  RTC that was never set reads 2000, an epoch of 0 reads 1970) or before
  the year this was built, and when the RTC chip lost power and nothing has
  set the clock since; the virtual clock that answers then is only the last
  time stored plus the uptime.
**/
STATIC
VOID
AboutFillClock (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;
  CHAR16      Value[ABOUT_TEXT_LENGTH];
  UINT8       Source;
  UINTN       Size;
  INT16       Zone;

  Size   = sizeof (Source);
  Status = gRT->GetVariable (
                  ROCKCHIP_TIME_SOURCE_NAME,
                  &gRockchipTimeSourceGuid,
                  NULL,
                  &Size,
                  &Source
                  );
  if (EFI_ERROR (Status) || (Size != sizeof (Source))) {
    Source = 0;
  }

  ZeroMem (&Time, sizeof (Time));
  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status) || (Time.Year <= 2000) || (Time.Year < AboutBuildYear ())) {
    AboutSetLine (STRING_TOKEN (STR_ABOUT_CLOCK), STRING_TOKEN (STR_ABOUT_CLOCK_LABEL), NULL, STRING_TOKEN (STR_ABOUT_CLOCK_NOT_SET));
    return;
  }

  if (Source == ROCKCHIP_TIME_SOURCE_RTC_LOST) {
    AboutSetLine (STRING_TOKEN (STR_ABOUT_CLOCK), STRING_TOKEN (STR_ABOUT_CLOCK_LABEL), NULL, STRING_TOKEN (STR_ABOUT_CLOCK_RTC_LOST));
    return;
  }

  UnicodeSPrint (
    Value,
    sizeof (Value),
    L"%04u-%02u-%02u %02u:%02u:%02u",
    Time.Year,
    Time.Month,
    Time.Day,
    Time.Hour,
    Time.Minute,
    Time.Second
    );

  //
  // NetworkTimeSyncDxe sets UTC with no time zone.  A time zone, if anyone
  // set one, is minutes to add to local time to get UTC.
  //
  if (Time.TimeZone != EFI_UNSPECIFIED_TIMEZONE) {
    Zone = (INT16)-Time.TimeZone;
    UnicodeSPrint (
      Value + StrLen (Value),
      sizeof (Value) - StrSize (Value) + sizeof (CHAR16),
      L" UTC%c%02u:%02u",
      (Zone < 0) ? L'-' : L'+',
      (UINT32)((Zone < 0) ? -Zone : Zone) / 60,
      (UINT32)((Zone < 0) ? -Zone : Zone) % 60
      );
  } else if (Source == ROCKCHIP_TIME_SOURCE_NETWORK) {
    StrCatS (Value, ARRAY_SIZE (Value), L" UTC");
  }

  AboutSetLine (
    STRING_TOKEN (STR_ABOUT_CLOCK),
    STRING_TOKEN (STR_ABOUT_CLOCK_LABEL),
    Value,
    (Source == ROCKCHIP_TIME_SOURCE_NETWORK) ? STRING_TOKEN (STR_ABOUT_CLOCK_FROM_NETWORK) :
    (Source == ROCKCHIP_TIME_SOURCE_RTC) ? STRING_TOKEN (STR_ABOUT_CLOCK_FROM_RTC) : 0
    );
}

/**
  Fill in every value line.
**/
STATIC
VOID
AboutFillValues (
  VOID
  )
{
  CHAR16  Value[ABOUT_TEXT_LENGTH];
  UINT64  LittleHz;
  UINT64  BigHz;
  UINT64  MemoryMiB;

  AboutSetLine (
    STRING_TOKEN (STR_ABOUT_VERSION),
    STRING_TOKEN (STR_ABOUT_VERSION_LABEL),
    (CONST CHAR16 *)PcdGetPtr (PcdFirmwareVersionString),
    0
    );

  //
  // When this file was compiled; scripts/build.sh touches it on every build
  // so that this moves.
  //
  UnicodeSPrint (Value, sizeof (Value), L"%a %a", __DATE__, __TIME__);
  AboutSetLine (STRING_TOKEN (STR_ABOUT_BUILD), STRING_TOKEN (STR_ABOUT_BUILD_LABEL), Value, 0);

  AboutFillClock ();

  UnicodeSPrint (Value, sizeof (Value), L"%a", (CHAR8 *)PcdGetPtr (PcdPlatformName));
  AboutSetLine (STRING_TOKEN (STR_ABOUT_BOARD), STRING_TOKEN (STR_ABOUT_BOARD_LABEL), Value, 0);

  UnicodeSPrint (Value, sizeof (Value), L"%a", (CHAR8 *)PcdGetPtr (PcdPlatformVendorName));
  AboutSetLine (STRING_TOKEN (STR_ABOUT_VENDOR), STRING_TOKEN (STR_ABOUT_VENDOR_LABEL), Value, 0);

  if (!EFI_ERROR (AboutCpuClusterRates (&LittleHz, &BigHz))) {
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

typedef struct {
  VENDOR_DEVICE_PATH          VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL    End;
} ABOUT_VENDOR_DEVICE_PATH;

STATIC ABOUT_VENDOR_DEVICE_PATH  mAboutDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    RK3576_ABOUT_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_GUID    mAboutFormSetGuid = RK3576_ABOUT_FORMSET_GUID;
STATIC EFI_HANDLE  mAboutDriverHandle;

//
// The page has no storage: the browser calls ExtractConfig and RouteConfig
// only for a formset with a varstore, and Callback only for the hidden clock
// question.
//
STATIC
EFI_STATUS
EFIAPI
AboutExtractConfig (
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
AboutRouteConfig (
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
EFI_STATUS
EFIAPI
AboutCallback (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  EFI_BROWSER_ACTION                    Action,
  IN  EFI_QUESTION_ID                       QuestionId,
  IN  UINT8                                 Type,
  IN  EFI_IFR_TYPE_VALUE                    *Value,
  OUT EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  if ((Action == EFI_BROWSER_ACTION_FORM_OPEN) && (QuestionId == RK3576_ABOUT_KEY_CLOCK)) {
    AboutFillClock ();
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC EFI_HII_CONFIG_ACCESS_PROTOCOL  mAboutConfigAccess = {
  AboutExtractConfig,
  AboutRouteConfig,
  AboutCallback
};

EFI_STATUS
EFIAPI
AboutUiLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mAboutDriverHandle = NULL;
  Status             = gBS->InstallMultipleProtocolInterfaces (
                              &mAboutDriverHandle,
                              &gEfiDevicePathProtocolGuid,
                              &mAboutDevicePath,
                              &gEfiHiiConfigAccessProtocolGuid,
                              &mAboutConfigAccess,
                              NULL
                              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "AboutUiLib: no About page: %r\n", Status));
    return EFI_SUCCESS;
  }

  mAboutHiiHandle = HiiAddPackages (
                      &mAboutFormSetGuid,
                      mAboutDriverHandle,
                      AboutUiLibVfrBin,
                      AboutUiLibStrings,
                      NULL
                      );
  if (mAboutHiiHandle == NULL) {
    DEBUG ((DEBUG_WARN, "AboutUiLib: no About page: HiiAddPackages failed\n"));
    return EFI_SUCCESS;
  }

  AboutFillValues ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AboutUiLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  if (mAboutHiiHandle != NULL) {
    HiiRemovePackages (mAboutHiiHandle);
    mAboutHiiHandle = NULL;
  }

  if (mAboutDriverHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mAboutDriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mAboutDevicePath,
           &gEfiHiiConfigAccessProtocolGuid,
           &mAboutConfigAccess,
           NULL
           );
    mAboutDriverHandle = NULL;
  }

  return EFI_SUCCESS;
}
