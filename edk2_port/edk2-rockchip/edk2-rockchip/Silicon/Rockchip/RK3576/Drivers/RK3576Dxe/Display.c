/** @file
 *
 *  RK3576 DXE display variable management.
 *
 *  Ported from Silicon/Rockchip/RK3588/Drivers/RK3588Dxe/Display.c
 *  Key differences from RK3588:
 *   - Uses gRK3576DxeFormSetGuid as the NVRAM variable namespace
 *   - No HDMI0/EDP0 mutual exclusion (RK3576 has no EDP connector)
 *   - ROCK 4D has a single HDMI0 output
 *
 *  Copyright (c) 2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/SimpleTextInEx.h>
#include <VarStoreData.h>

#include "RK3576DxeFormSetGuid.h"
#include "Display.h"

STATIC VOID  *mTextInExInstallRegistration;

STATIC
EFI_STATUS
InitializeDisplayVariables (
  IN BOOLEAN  Reset
  )
{
  EFI_STATUS                                 Status;
  UINTN                                      Index;
  UINT32                                     *Connectors;
  UINTN                                      ConnectorsCount;
  UINT32                                     ConnectorsMask;
  UINTN                                      Size;
  UINT8                                      Var8;
  UINT16                                     Var16;
  VOID                                       *PcdData;
  DISPLAY_MODE_PRESET_VARSTORE_DATA          ModePreset;
  DISPLAY_MODE                               ModeCustom;
  DISPLAY_CONNECTORS_PRIORITY_VARSTORE_DATA  ConnectorsPriority;

  Connectors      = PcdGetPtr (PcdDisplayConnectors);
  ConnectorsCount = PcdGetSize (PcdDisplayConnectors) / sizeof (*Connectors);
  ASSERT (ConnectorsCount <= VOP_OUTPUT_IF_NUMS);

  ConnectorsMask = 0;
  for (Index = 0; Index < ConnectorsCount; Index++) {
    ConnectorsMask |= Connectors[Index];
  }

  Status = PcdSet32S (PcdDisplayConnectorsMask, ConnectorsMask);
  ASSERT_EFI_ERROR (Status);

  if (ConnectorsMask == 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // Write a boot-time (volatile) variable so the VFR browser can evaluate
  // suppressif conditions that reference get(DisplayConnectorsMask).
  //
  Size = sizeof (ConnectorsMask);
  gRT->SetVariable (
         L"DisplayConnectorsMask",
         &gRK3576DxeFormSetGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS,
         Size,
         &ConnectorsMask
         );

  //
  // DisplayModePreset — write boot-time default hint var for VFR, then seed
  // the runtime PCD if no user preference is stored in NVRAM.
  //
  PcdData = PcdGetPtr (PcdDisplayModePresetDefault);
  ASSERT (PcdData != NULL);
  if (PcdData != NULL) {
    Size   = sizeof (ModePreset);
    Status = gRT->SetVariable (
                    L"DisplayModeDefault",
                    &gRK3576DxeFormSetGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    Size,
                    PcdData
                    );
    ASSERT_EFI_ERROR (Status);

    Size   = sizeof (ModePreset);
    Status = !Reset ? gRT->GetVariable (
                             L"DisplayModePreset",
                             &gRK3576DxeFormSetGuid,
                             NULL,
                             &Size,
                             &ModePreset
                             ) : EFI_NOT_FOUND;
    if (EFI_ERROR (Status) ||
        (ModePreset.Preset == DISPLAY_MODE_NATIVE &&
         ((DISPLAY_MODE_PRESET_VARSTORE_DATA *)PcdData)->Preset != DISPLAY_MODE_NATIVE))
    {
      //
      // Seed the PCD with the firmware's FixedAtBuild default when:
      //  (a) No NVRAM variable exists yet (first boot or after NVRAM clear), OR
      //  (b) NVRAM has NATIVE but the firmware default has been changed to a
      //      specific mode (e.g. 1080p60). This migrates boards where the old
      //      default was NATIVE to the new board-specific default without
      //      requiring manual NVRAM clearing. Users who explicitly chose a
      //      non-NATIVE preset keep their selection.
      //
      Size   = sizeof (ModePreset);
      Status = PcdSetPtrS (PcdDisplayModePreset, &Size, PcdData);
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // DisplayModeCustom — write boot-time default for VFR, seed PCD on first boot.
  //
  PcdData = PcdGetPtr (PcdDisplayModeCustomDefault);
  ASSERT (PcdData != NULL);
  if (PcdData != NULL) {
    Size   = sizeof (ModeCustom);
    Status = gRT->SetVariable (
                    L"DisplayModeCustomDefault",
                    &gRK3576DxeFormSetGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    Size,
                    PcdData
                    );
    ASSERT_EFI_ERROR (Status);

    Status = !Reset ? gRT->GetVariable (
                             L"DisplayModeCustom",
                             &gRK3576DxeFormSetGuid,
                             NULL,
                             &Size,
                             &ModeCustom
                             ) : EFI_NOT_FOUND;
    if (EFI_ERROR (Status)) {
      Status = PcdSetPtrS (PcdDisplayModeCustom, &Size, PcdData);
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // DisplayConnectorsPriority — seed from board connector list on first boot.
  //
  Size   = sizeof (ConnectorsPriority);
  Status = !Reset ? gRT->GetVariable (
                           L"DisplayConnectorsPriority",
                           &gRK3576DxeFormSetGuid,
                           NULL,
                           &Size,
                           &ConnectorsPriority
                           ) : EFI_NOT_FOUND;
  if (EFI_ERROR (Status)) {
    ZeroMem (&ConnectorsPriority, sizeof (ConnectorsPriority));
    CopyMem (&ConnectorsPriority.Order, Connectors, ConnectorsCount * sizeof (*Connectors));

    Status = PcdSetPtrS (PcdDisplayConnectorsPriority, &Size, &ConnectorsPriority);
    ASSERT_EFI_ERROR (Status);
  }

  //
  // DisplayForceOutput
  //
  Size   = sizeof (Var8);
  Status = !Reset ? gRT->GetVariable (
                           L"DisplayForceOutput",
                           &gRK3576DxeFormSetGuid,
                           NULL,
                           &Size,
                           &Var8
                           ) : EFI_NOT_FOUND;
  if (EFI_ERROR (Status)) {
    Status = PcdSetBoolS (PcdDisplayForceOutput, FixedPcdGetBool (PcdDisplayForceOutputDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // DisplayDuplicateOutput
  //
  Size   = sizeof (Var8);
  Status = !Reset ? gRT->GetVariable (
                           L"DisplayDuplicateOutput",
                           &gRK3576DxeFormSetGuid,
                           NULL,
                           &Size,
                           &Var8
                           ) : EFI_NOT_FOUND;
  if (EFI_ERROR (Status)) {
    Status = PcdSetBoolS (PcdDisplayDuplicateOutput, FixedPcdGetBool (PcdDisplayDuplicateOutputDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // DisplayRotation
  //
  Size   = sizeof (Var16);
  Status = !Reset ? gRT->GetVariable (
                           L"DisplayRotation",
                           &gRK3576DxeFormSetGuid,
                           NULL,
                           &Size,
                           &Var16
                           ) : EFI_NOT_FOUND;
  if (EFI_ERROR (Status)) {
    Status = PcdSet16S (PcdDisplayRotation, FixedPcdGet16 (PcdDisplayRotationDefault));
    ASSERT_EFI_ERROR (Status);
  }

  //
  // HdmiSignalingMode
  //
  Size   = sizeof (Var8);
  Status = !Reset ? gRT->GetVariable (
                           L"HdmiSignalingMode",
                           &gRK3576DxeFormSetGuid,
                           NULL,
                           &Size,
                           &Var8
                           ) : EFI_NOT_FOUND;
  if (EFI_ERROR (Status)) {
    Status = PcdSet8S (PcdHdmiSignalingMode, FixedPcdGet8 (PcdHdmiSignalingModeDefault));
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "RK3576Dxe: Display ConnectorsMask=0x%08x, %u connector(s)\n",
          ConnectorsMask, (UINT32)ConnectorsCount));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ResetKeyHandler (
  IN EFI_KEY_DATA  *KeyData
  )
{
  DEBUG ((DEBUG_INFO, "RK3576Dxe: Display reset key pressed — restoring default settings.\n"));

  InitializeDisplayVariables (TRUE);

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
OnTextInExInstallNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                         Status;
  UINTN                              HandleCount;
  EFI_HANDLE                         *HandleBuffer;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *SimpleTextInEx;
  EFI_HANDLE                         KeyNotifyHandle;
  EFI_KEY_DATA                       ResetKey;

  while (TRUE) {
    Status = gBS->LocateHandleBuffer (
                    ByRegisterNotify,
                    NULL,
                    mTextInExInstallRegistration,
                    &HandleCount,
                    &HandleBuffer
                    );
    if (EFI_ERROR (Status)) {
      if (Status != EFI_NOT_FOUND) {
        ASSERT_EFI_ERROR (Status);
      }

      break;
    }

    ASSERT (HandleCount == 1);

    Status = gBS->HandleProtocol (
                    HandleBuffer[0],
                    &gEfiSimpleTextInputExProtocolGuid,
                    (VOID **)&SimpleTextInEx
                    );
    FreePool (HandleBuffer);
    if (EFI_ERROR (Status)) {
      ASSERT_EFI_ERROR (Status);
      continue;
    }

    // LCtrl + LShift + F6  →  cold reset, restoring display defaults
    ResetKey.Key.ScanCode            = SCAN_F6;
    ResetKey.Key.UnicodeChar         = CHAR_NULL;
    ResetKey.KeyState.KeyShiftState  = EFI_SHIFT_STATE_VALID |
                                       EFI_LEFT_CONTROL_PRESSED |
                                       EFI_LEFT_SHIFT_PRESSED;
    ResetKey.KeyState.KeyToggleState = 0;

    KeyNotifyHandle = NULL;

    Status = SimpleTextInEx->RegisterKeyNotify (
                               SimpleTextInEx,
                               &ResetKey,
                               ResetKeyHandler,
                               &KeyNotifyHandle
                               );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "RK3576Dxe: Failed to register display reset key: %r\n",
        Status
        ));
    }
  }
}

STATIC
VOID
RegisterResetHandlers (
  VOID
  )
{
  EFI_EVENT  Event;

  Event = EfiCreateProtocolNotifyEvent (
            &gEfiSimpleTextInputExProtocolGuid,
            TPL_CALLBACK,
            OnTextInExInstallNotify,
            NULL,
            &mTextInExInstallRegistration
            );

  ASSERT (Event != NULL);
}

VOID
EFIAPI
ApplyDisplayVariables (
  VOID
  )
{
  // On RK3576, display hardware init is performed by the VOP2 and DW HDMI QP
  // DXE drivers which read the display PCDs directly.  Nothing to do here.
}

VOID
EFIAPI
SetupDisplayVariables (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = InitializeDisplayVariables (FALSE);
  if (EFI_ERROR (Status)) {
    return;
  }

  RegisterResetHandlers ();
}
