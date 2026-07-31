/** @file
  This file implements the Graphics Output protocol for Arm platforms

  Copyright (c) 2011 - 2020, Arm Limited. All rights reserved.<BR>
  Copyright (c) 2022 Rockchip Electronics Co. Ltd.
  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DrmModes.h>
#include <Library/MediaBusFormat.h>

#include <Guid/GlobalVariable.h>

#include "LcdGraphicsOutputDxe.h"

STATIC EFI_CPU_ARCH_PROTOCOL  *mCpu;

STATIC LCD_INSTANCE  mLcdTemplate = {
  LCD_INSTANCE_SIGNATURE,
  NULL,                                        // Handle
  {               // ModeInfo
    0,            // Version
    0,            // HorizontalResolution
    0,            // VerticalResolution
    PixelBltOnly, // PixelFormat
    { 0 },        // PixelInformation
    0,            // PixelsPerScanLine
  },
  {
    0,    // MaxMode;
    0,    // Mode;
    NULL, // Info;
    0,    // SizeOfInfo;
    0,    // FrameBufferBase;
    0     // FrameBufferSize;
  },
  {                       // Gop
    LcdGraphicsQueryMode, // QueryMode
    LcdGraphicsSetMode,   // SetMode
    LcdGraphicsBlt,       // Blt
    NULL                  // *Mode
  },
  { // DevicePath
    {
      {
        HARDWARE_DEVICE_PATH,                  HW_VENDOR_DP,
        {
          (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
        },
      },
      // Hardware Device Path for Lcd
      EFI_CALLER_ID_GUID // Use the driver's GUID
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        sizeof (EFI_DEVICE_PATH_PROTOCOL),
        0
      }
    }
  },
  { 0 },                                       // DisplayStates
  0,                                           // DisplayStatesCount
  NULL,                                        // DisplayModes
};

STATIC
EFI_STATUS
PrepareDisplays (
  IN LCD_INSTANCE  *Instance
  )
{
  EFI_STATUS                                 Status;
  ROCKCHIP_CRTC_PROTOCOL                     *Crtc;
  UINTN                                      ConnectorCount;
  EFI_HANDLE                                 *ConnectorHandles = NULL;
  DISPLAY_CONNECTORS_PRIORITY_VARSTORE_DATA  *ConnectorsPriority;
  ROCKCHIP_CONNECTOR_PROTOCOL                *Connector;
  UINTN                                      ConnectorIndex;
  UINTN                                      Index;
  DISPLAY_STATE                              *DisplayState;
  CONNECTOR_STATE                            *ConnectorState;
  CRTC_STATE                                 *CrtcState;

  Status = gBS->LocateProtocol (
                  &gRockchipCrtcProtocolGuid,
                  NULL,
                  (VOID **)&Crtc
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Failed to locate gRockchipCrtcProtocolGuid. Status=%r\n",
      __func__,
      Status
      ));
    return Status;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gRockchipConnectorProtocolGuid,
                  NULL,
                  &ConnectorCount,
                  &ConnectorHandles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Failed to locate gRockchipConnectorProtocolGuid. Status=%r\n",
      __func__,
      Status
      ));
    return Status;
  }

  ConnectorsPriority = PcdGetPtr (PcdDisplayConnectorsPriority);
  if (ConnectorsPriority == NULL) {
    ASSERT (FALSE);
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  Instance->DisplayStatesCount = PcdGetSize (PcdDisplayConnectors) / sizeof (UINT32);
  ASSERT (Instance->DisplayStatesCount <= VOP_OUTPUT_IF_NUMS);

  for (ConnectorIndex = 0; ConnectorIndex < ConnectorCount; ConnectorIndex++) {
    Status = gBS->HandleProtocol (
                    ConnectorHandles[ConnectorIndex],
                    &gRockchipConnectorProtocolGuid,
                    (VOID **)&Connector
                    );
    if (EFI_ERROR (Status)) {
      ASSERT_EFI_ERROR (Status);
      continue;
    }

    DisplayState = AllocateZeroPool (sizeof (DISPLAY_STATE));
    if (DisplayState == NULL) {
      ASSERT (FALSE);
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }

    ConnectorState = &DisplayState->ConnectorState;
    CrtcState      = &DisplayState->CrtcState;

    ConnectorState->Connector = (VOID *)Connector;
    CrtcState->Crtc           = (VOID *)Crtc;

    if (Connector->Preinit != NULL) {
      Status = Connector->Preinit (Connector, DisplayState);
      if (EFI_ERROR (Status)) {
        ASSERT_EFI_ERROR (Status);
        goto DiscardState;
      }
    }

    if (Connector->Init != NULL) {
      Status = Connector->Init (Connector, DisplayState);
      if (EFI_ERROR (Status)) {
        ASSERT_EFI_ERROR (Status);
        goto DiscardState;
      }
    } else if (Connector->Preinit == NULL) {
      ASSERT (FALSE);
      goto DiscardState;
    }

    for (Index = 0; Index < Instance->DisplayStatesCount; Index++) {
      if (ConnectorsPriority->Order[Index] == ConnectorState->OutputInterface) {
        Instance->DisplayStates[Index] = DisplayState;
        break;
      }
    }

    if (Index == Instance->DisplayStatesCount) {
      ASSERT (FALSE);
      goto DiscardState;
    }

    continue;

DiscardState:
    FreePool (DisplayState);
  }

  Status = EFI_SUCCESS;

Exit:
  if (EFI_ERROR (Status)) {
    for (Index = 0; Index < Instance->DisplayStatesCount; Index++) {
      if (Instance->DisplayStates[Index] != NULL) {
        FreePool (Instance->DisplayStates[Index]);
        Instance->DisplayStates[Index] = NULL;
      }
    }
  }

  if (ConnectorHandles != NULL) {
    FreePool (ConnectorHandles);
  }

  return Status;
}

STATIC
EFI_STATUS
DetectDisplays (
  IN  LCD_INSTANCE   *Instance,
  IN  BOOLEAN        ForceDetect,
  IN  BOOLEAN        DetectAll,
  OUT DISPLAY_STATE  **PrimaryDisplayState
  )
{
  EFI_STATUS                   Status;
  UINTN                        Index;
  UINTN                        NewCount;
  DISPLAY_STATE                *DisplayState;
  CONNECTOR_STATE              *ConnectorState;
  ROCKCHIP_CONNECTOR_PROTOCOL  *Connector;

  for (Index = 0, NewCount = 0; Index < Instance->DisplayStatesCount; Index++) {
    DisplayState = Instance->DisplayStates[Index];
    if (DisplayState == NULL) {
      continue;
    }

    ConnectorState = &DisplayState->ConnectorState;
    Connector      = (ROCKCHIP_CONNECTOR_PROTOCOL *)ConnectorState->Connector;

    if (Connector->Detect != NULL) {
      Status = Connector->Detect (Connector, DisplayState);
    } else {
      Status = EFI_SUCCESS; // Assume always connected.
    }

    DEBUG ((
      DEBUG_INFO,
      "%a: %a status: %r\n",
      __func__,
      GetVopOutputIfName (ConnectorState->OutputInterface),
      Status
      ));

    if (!EFI_ERROR (Status)) {
      if (*PrimaryDisplayState == NULL) {
        *PrimaryDisplayState = DisplayState;
      }
    } else if (!ForceDetect) {
      FreePool (DisplayState);
      Instance->DisplayStates[Index] = NULL;
      continue;
    }

    Instance->DisplayStates[NewCount++] = DisplayState;

    if ((*PrimaryDisplayState != NULL) && !DetectAll) {
      break;
    }
  }

  Instance->DisplayStatesCount = NewCount;

  if (Instance->DisplayStatesCount == 0) {
    DEBUG ((DEBUG_ERROR, "%a: No displays found!\n", __func__));
    return EFI_NOT_FOUND;
  }

  if (*PrimaryDisplayState != NULL) {
    DEBUG ((
      DEBUG_INFO,
      "%a: Primary: %a\n",
      __func__,
      GetVopOutputIfName ((*PrimaryDisplayState)->ConnectorState.OutputInterface)
      ));
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
IdentifyDisplay (
  IN DISPLAY_STATE  *DisplayState
  )
{
  EFI_STATUS                   Status;
  CONNECTOR_STATE              *ConnectorState;
  ROCKCHIP_CONNECTOR_PROTOCOL  *Connector;

  ConnectorState = &DisplayState->ConnectorState;
  Connector      = (ROCKCHIP_CONNECTOR_PROTOCOL *)ConnectorState->Connector;

  Status = EFI_UNSUPPORTED;

  //
  // Get predefined native mode.
  //
  if (Connector->GetTiming != NULL) {
    Status = Connector->GetTiming (Connector, DisplayState);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to get predefined native mode. Status=%r\n",
        __func__,
        Status
        ));
      return Status;
    }
  }

  //
  // Get sink info from EDID.
  //
  if (Connector->GetEdid != NULL) {
    Status = Connector->GetEdid (Connector, DisplayState);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to get EDID. Status=%r\n",
        __func__,
        Status
        ));
      if (Status == EFI_CRC_ERROR) {
        DEBUG ((DEBUG_INFO, "%a: ", __func__));
        DebugPrintEdid (ConnectorState->Edid);
      }

      return Status;
    }

    DEBUG ((DEBUG_INFO, "%a: ", __func__));
    DebugPrintEdid (ConnectorState->Edid);

    Status = EdidGetDisplaySinkInfo (ConnectorState);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to get sink info from EDID. Status=%r\n",
        __func__,
        Status
        ));
    }
  }

  DEBUG ((DEBUG_INFO, "%a: Sink Info:\n", __func__));
  DebugPrintDisplaySinkInfo (&ConnectorState->SinkInfo, 2);

  return Status;
}

STATIC
EFI_STATUS
SetupDisplay (
  IN DISPLAY_STATE  *DisplayState
  )
{
  EFI_STATUS              Status;
  CRTC_STATE              *CrtcState;
  ROCKCHIP_CRTC_PROTOCOL  *Crtc;
  CONNECTOR_STATE         *ConnectorState;

  CrtcState      = &DisplayState->CrtcState;
  Crtc           = (ROCKCHIP_CRTC_PROTOCOL *)CrtcState->Crtc;
  ConnectorState = &DisplayState->ConnectorState;

  //
  // RK3576: use VP0 (CRTC port 0) and plane config 0 (Cluster0 + Esmart0).
  //   VP0 connects to HDMI0 via the IF_CTRL mux and its DCLK is already
  //   sourced from the HDMI PHY PLL by U-Boot/SPL.
  //   Using VP2 (config 1) is wrong for RK3576: config slot [1][2] maps
  //   Cluster2/3 which do not exist on RK3576 (only 2 clusters present).
  // RK3588: keep the existing VP2 / config 1 assignment.
  //
#ifdef SOC_RK3576
  CrtcState->CrtcID             = 0;
  DisplayState->VpsConfigModeID = 0;
#else
  CrtcState->CrtcID             = 2;
  DisplayState->VpsConfigModeID = 1;
#endif

  //
  // Use RGB888_1X24 by default as it's the most compatible.
  // The connector could override this, but it may break other
  // sinks if the output is shared between multiple connectors.
  //
  ConnectorState->BusFormat = MEDIA_BUS_FMT_RGB888_1X24;

  Crtc->Vps[CrtcState->CrtcID].Enable     = TRUE;
  Crtc->Vps[CrtcState->CrtcID].OutputType = ConnectorState->Type;

  if (Crtc->Preinit != NULL) {
    Status = Crtc->Preinit (Crtc, DisplayState);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  DisplayState->IsEnable = TRUE;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SetupAllDisplays (
  IN LCD_INSTANCE   *Instance,
  IN DISPLAY_STATE  *PrimaryDisplayState
  )
{
  EFI_STATUS     Status;
  UINTN          Index;
  DISPLAY_STATE  *DisplayState;

  for (Index = 0; Index < Instance->DisplayStatesCount; Index++) {
    DisplayState = Instance->DisplayStates[Index];
    if ((DisplayState == NULL) || DisplayState->IsEnable) {
      continue;
    }

    if (PrimaryDisplayState != NULL) {
      //
      // Clone primary display sink info.
      // This is a best effort to support multiple outputs on
      // a single CRTC port. All sinks are assumed to have more
      // or less the same capabilities.
      //
      CopyMem (
        &DisplayState->ConnectorState.SinkInfo,
        &PrimaryDisplayState->ConnectorState.SinkInfo,
        sizeof (DisplayState->ConnectorState.SinkInfo)
        );
    }

    Status = SetupDisplay (DisplayState);
    if (EFI_ERROR (Status)) {
      continue;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetSupportedDisplayModes (
  IN LCD_INSTANCE   *Instance,
  IN DISPLAY_STATE  *PrimaryDisplayState
  )
{
  CONST DISPLAY_MODE                 *Mode;
  DISPLAY_MODE_PRESET_VARSTORE_DATA  *ModePreset;
  DISPLAY_SINK_INFO                  *SinkInfo;

  //
  // Only expose a single mode to GOP.
  //
  Instance->Gop.Mode->MaxMode = 1;

  Instance->Gop.Mode->Mode = MAX_UINT32;

  Instance->DisplayModes = AllocateZeroPool (
                             sizeof (DISPLAY_MODE) *
                             Instance->Gop.Mode->MaxMode
                             );
  if (Instance->DisplayModes == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  Mode = NULL;

  ModePreset = PcdGetPtr (PcdDisplayModePreset);
  ASSERT (ModePreset != NULL);

  if (ModePreset != NULL) {
    if (ModePreset->Preset == DISPLAY_MODE_NATIVE) {
      DEBUG ((DEBUG_INFO, "%a: Native\n", __func__));
      if (PrimaryDisplayState != NULL) {
        SinkInfo = &PrimaryDisplayState->ConnectorState.SinkInfo;
        if (SinkInfo->PreferredMode.OscFreq != 0) {
          Mode = &SinkInfo->PreferredMode;
        }
      }
    } else if (ModePreset->Preset == DISPLAY_MODE_CUSTOM) {
      DEBUG ((DEBUG_INFO, "%a: Custom:\n", __func__));
      Mode = PcdGetPtr (PcdDisplayModeCustom);
      ASSERT (Mode != NULL);
      if (Mode != NULL) {
        DebugPrintDisplayMode (Mode, 2, TRUE, TRUE);

        if ((Mode->OscFreq < VOP_PIXEL_CLOCK_MIN) ||
            (Mode->OscFreq > VOP_PIXEL_CLOCK_MAX) ||
            (Mode->HActive < VOP_HORIZONTAL_RES_MIN) ||
            (Mode->HActive > VOP_HORIZONTAL_RES_MAX) ||
            (Mode->VActive < VOP_VERTICAL_RES_MIN) ||
            (Mode->VActive > VOP_VERTICAL_RES_MAX))
        {
          Mode = NULL;
          DEBUG ((DEBUG_ERROR, "%a: Custom mode unsupported!\n", __func__));
          ASSERT (FALSE);
        }
      }
    } else if (ModePreset->Preset < GetPredefinedDisplayModesCount ()) {
      DEBUG ((DEBUG_INFO, "%a: Preset %u\n", __func__, ModePreset->Preset));
      Mode = GetPredefinedDisplayMode (ModePreset->Preset);
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Invalid preferred mode: %u\n",
        __func__,
        ModePreset->Preset
        ));
      ASSERT (FALSE);
    }
  }

  if (Mode == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "%a: No usable mode found! Falling back to lowest available.\n",
      __func__
      ));
    Mode = GetPredefinedDisplayMode (0);
  }

  CopyMem (&Instance->DisplayModes[0], Mode, sizeof (*Mode));

  return EFI_SUCCESS;
}

STATIC
VOID
LcdGraphicsOutputDestroy (
  IN LCD_INSTANCE  *Instance
  )
{
  UINTN  Index;

  if (Instance == NULL) {
    return;
  }

  if (Instance->Handle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           Instance->Handle,
           &gEfiGraphicsOutputProtocolGuid,
           &Instance->Gop,
           &gEfiDevicePathProtocolGuid,
           &Instance->DevicePath,
           NULL
           );
  }

  if (Instance->DisplayModes != NULL) {
    FreePool (Instance->DisplayModes);
  }

  for (Index = 0; Index < Instance->DisplayStatesCount; Index++) {
    if (Instance->DisplayStates[Index] != NULL) {
      FreePool (Instance->DisplayStates[Index]);
    }
  }

  FreePool (Instance);
}

STATIC
EFI_STATUS
EFIAPI
LcdGraphicsOutputInit (
  VOID
  )
{
  EFI_STATUS     Status;
  LCD_INSTANCE   *Instance = NULL;
  DISPLAY_STATE  *DisplayState;
  DISPLAY_STATE  *PrimaryDisplayState;
  BOOLEAN        ForceOutput;
  BOOLEAN        DuplicateOutput;

  Instance = AllocateCopyPool (sizeof (LCD_INSTANCE), &mLcdTemplate);
  if (Instance == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = PrepareDisplays (Instance);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  ForceOutput     = PcdGetBool (PcdDisplayForceOutput);
  DuplicateOutput = PcdGetBool (PcdDisplayDuplicateOutput);

  PrimaryDisplayState = NULL;

  Status = DetectDisplays (
             Instance,
             ForceOutput,
             DuplicateOutput,
             &PrimaryDisplayState
             );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  DisplayState = NULL;

  if (PrimaryDisplayState != NULL) {
    DisplayState = PrimaryDisplayState;

    IdentifyDisplay (PrimaryDisplayState);
  } else if (ForceOutput) {
    DisplayState = Instance->DisplayStates[0];

    DuplicateOutput = TRUE;
  }

  if (DisplayState == NULL) {
    ASSERT (FALSE);
    goto Exit;
  }

  Status = SetupDisplay (DisplayState);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (DuplicateOutput) {
    SetupAllDisplays (Instance, PrimaryDisplayState);
  }

  Instance->Gop.Mode  = &Instance->Mode;
  Instance->Mode.Info = &Instance->ModeInfo;

  Status = GetSupportedDisplayModes (Instance, PrimaryDisplayState);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Instance->Handle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &Instance->Gop,
                  &gEfiDevicePathProtocolGuid,
                  &Instance->DevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Failed to install GOP. Status=%r\n",
      __func__,
      Status
      ));
    goto Exit;
  }

Exit:
  if (EFI_ERROR (Status)) {
    LcdGraphicsOutputDestroy (Instance);
  }

  return Status;
}

VOID
EFIAPI
LcdGraphicsOutputEndOfDxeEventHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);

  LcdGraphicsOutputInit ();
}

EFI_STATUS
EFIAPI
LcdGraphicsOutputDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   EndOfDxeEvent;

  Status = gBS->LocateProtocol (
                  &gEfiCpuArchProtocolGuid,
                  NULL,
                  (VOID **)&mCpu
                  );
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  LcdGraphicsOutputEndOfDxeEventHandler,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &EndOfDxeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

STATIC
VOID
LcdGraphicsSetModeInfo (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info,
  IN  CONST DISPLAY_MODE                    *DisplayMode,
  IN  BOOLEAN                               Update
  )
{
  UINT16  Rotation = PcdGet16 (PcdDisplayRotation);

  This->Mode->Info->Version = 0;

  if ((Rotation == 90)) {
    //
    // Swap the reported resolution and only allow Blt operations.
    //
    Info->HorizontalResolution = DisplayMode->VActive;
    Info->VerticalResolution   = DisplayMode->HActive;
    Info->PixelFormat          = PixelBltOnly;
  } else {
    Info->HorizontalResolution = DisplayMode->HActive;
    Info->VerticalResolution   = DisplayMode->VActive;
    Info->PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  }

  Info->PixelsPerScanLine = Info->HorizontalResolution;

  if (Update) {
    switch (Rotation) {
      case 90:
        This->Blt = LcdGraphicsBlt90;
        break;

      default:
        This->Blt = LcdGraphicsBlt;

        if (Rotation != 0) {
          DEBUG ((DEBUG_ERROR, "%a: Unsupported rotation %u\n", __func__, Rotation));
          ASSERT (FALSE);
        }

        break;
    }
  }
}

EFI_STATUS
EFIAPI
LcdGraphicsQueryMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL           *This,
  IN UINT32                                 ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  LCD_INSTANCE  *Instance;
  DISPLAY_MODE  *Mode;

  if ((This == NULL) ||
      (Info == NULL) ||
      (SizeOfInfo == NULL) ||
      (ModeNumber >= This->Mode->MaxMode))
  {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Invalid parameter! (ModeNumber=%d)\n",
      __func__,
      ModeNumber
      ));
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateZeroPool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  if (*Info == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  Instance = LCD_INSTANCE_FROM_GOP_THIS (This);
  Mode     = &Instance->DisplayModes[ModeNumber];

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  LcdGraphicsSetModeInfo (This, *Info, Mode, FALSE);

  return EFI_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// VOP2 bring-up test pattern
// ---------------------------------------------------------------------------
//
// Set to 0 for a production build.
//
// "Signal locks but the picture is black" and "nothing is drawn into the
// framebuffer" look identical from the far end of an HDMI cable, and the whole
// EDK2 console stack (GraphicsConsole -> ConSplitter -> Blt -> LogoDxe) sits
// between the two.  This writes a known pattern straight into the framebuffer
// with plain 32-bit stores, so a photograph of the screen answers the question
// on its own without needing a single VOP2 register read.
//
// What each element tells us:
//
//   colour bars   scanout works at all, and the pixel format and stride are
//                 right.  Wrong stride shears the bars diagonally; a swapped
//                 format reorders them (expected left to right: white, yellow,
//                 cyan, green, magenta, red, blue, black).
//   white border  the active area lines up with the timing.  A border that is
//                 clipped or offset is the bg/pre-scan delay problem, not a
//                 compositing problem.
//   corner blocks orientation and that the last scanline is reached: red near
//                 the top-left, green near the bottom-right.
//   centre block  drawn *after* the connector is enabled.  Bars but no centre
//                 block means writes stop landing once scanout is running,
//                 which would point at the write-combining mapping rather than
//                 at VOP2.
//
#define RK_VOP2_TEST_PATTERN  1

#if RK_VOP2_TEST_PATTERN

//
// PixelBlueGreenRedReserved8BitPerColor: byte 0 blue, byte 1 green, byte 2
// red.  Little-endian, so a pixel is (R << 16) | (G << 8) | B.
//
#define RK_TP_WHITE    0x00FFFFFFu
#define RK_TP_YELLOW   0x00FFFF00u
#define RK_TP_CYAN     0x0000FFFFu
#define RK_TP_GREEN    0x0000FF00u
#define RK_TP_MAGENTA  0x00FF00FFu
#define RK_TP_RED      0x00FF0000u
#define RK_TP_BLUE     0x000000FFu
#define RK_TP_BLACK    0x00000000u

#define RK_TP_BORDER   4     // white frame thickness, pixels
#define RK_TP_CORNER   64    // corner block size, pixels
#define RK_TP_INSET    8     // corner block distance from the edge
#define RK_TP_CENTRE   128   // phase-1 centre block size, pixels

STATIC
VOID
RkFillRect (
  IN volatile UINT32  *Fb,
  IN UINT32           PixelsPerScanLine,
  IN UINT32           X,
  IN UINT32           Y,
  IN UINT32           W,
  IN UINT32           H,
  IN UINT32           Colour
  )
{
  UINT32  Row;
  UINT32  Col;

  for (Row = 0; Row < H; Row++) {
    volatile UINT32  *Line = Fb + ((UINTN)(Y + Row) * PixelsPerScanLine) + X;

    for (Col = 0; Col < W; Col++) {
      Line[Col] = Colour;
    }
  }
}

STATIC
VOID
RkDrawTestPattern (
  IN EFI_PHYSICAL_ADDRESS  FbBase,
  IN UINT32                Width,
  IN UINT32                Height,
  IN UINT32                PixelsPerScanLine,
  IN UINT32                Phase
  )
{
  STATIC CONST UINT32  Bars[8] = {
    RK_TP_WHITE, RK_TP_YELLOW, RK_TP_CYAN,  RK_TP_GREEN,
    RK_TP_MAGENTA, RK_TP_RED,  RK_TP_BLUE,  RK_TP_BLACK
  };

  volatile UINT32  *Fb;
  UINT32           BarWidth;
  UINT32           X;
  UINT32           Y;

  if ((FbBase == 0) || (Width == 0) || (Height == 0)) {
    return;
  }

  Fb = (volatile UINT32 *)(UINTN)FbBase;

  if (Phase == 0) {
    //
    // Colour bars across the whole visible area. The last bar absorbs the
    // remainder when the width does not divide by eight.
    //
    BarWidth = Width / ARRAY_SIZE (Bars);
    if (BarWidth == 0) {
      BarWidth = 1;
    }

    for (Y = 0; Y < Height; Y++) {
      volatile UINT32  *Line = Fb + ((UINTN)Y * PixelsPerScanLine);

      for (X = 0; X < Width; X++) {
        UINT32  Bar = X / BarWidth;

        Line[X] = Bars[MIN (Bar, ARRAY_SIZE (Bars) - 1)];
      }
    }

    //
    // White frame, so a clipped or shifted active area is obvious.
    //
    if ((Width > 2 * RK_TP_BORDER) && (Height > 2 * RK_TP_BORDER)) {
      RkFillRect (Fb, PixelsPerScanLine, 0, 0, Width, RK_TP_BORDER, RK_TP_WHITE);
      RkFillRect (Fb, PixelsPerScanLine, 0, Height - RK_TP_BORDER, Width, RK_TP_BORDER, RK_TP_WHITE);
      RkFillRect (Fb, PixelsPerScanLine, 0, 0, RK_TP_BORDER, Height, RK_TP_WHITE);
      RkFillRect (Fb, PixelsPerScanLine, Width - RK_TP_BORDER, 0, RK_TP_BORDER, Height, RK_TP_WHITE);
    }

    //
    // Corner markers, inset so they sit inside the frame.
    //
    if ((Width > 2 * (RK_TP_INSET + RK_TP_CORNER)) &&
        (Height > 2 * (RK_TP_INSET + RK_TP_CORNER)))
    {
      RkFillRect (
        Fb, PixelsPerScanLine,
        RK_TP_INSET, RK_TP_INSET,
        RK_TP_CORNER, RK_TP_CORNER, RK_TP_RED
        );
      RkFillRect (
        Fb, PixelsPerScanLine,
        Width - RK_TP_INSET - RK_TP_CORNER,
        Height - RK_TP_INSET - RK_TP_CORNER,
        RK_TP_CORNER, RK_TP_CORNER, RK_TP_GREEN
        );
    }

    DEBUG ((
      DEBUG_ERROR,
      "[RK3576-TP] phase 0: bars+frame+corners drawn at 0x%lx (%ux%u, ppsl %u)\n",
      (UINT64)FbBase, Width, Height, PixelsPerScanLine
      ));
  } else {
    //
    // Drawn after the connector is live. Blue so it cannot be confused with
    // anything from phase 0 at that position.
    //
    if ((Width > RK_TP_CENTRE) && (Height > RK_TP_CENTRE)) {
      RkFillRect (
        Fb, PixelsPerScanLine,
        (Width - RK_TP_CENTRE) / 2,
        (Height - RK_TP_CENTRE) / 2,
        RK_TP_CENTRE, RK_TP_CENTRE, RK_TP_BLUE
        );
    }

    DEBUG ((
      DEBUG_ERROR,
      "[RK3576-TP] phase 1: centre block drawn after connector enable\n"
      ));
  }

  //
  // The framebuffer is mapped write-combining; make sure the stores are out of
  // the write buffers before the VOP2 DMA reads them.
  //
  MemoryFence ();
}

#endif /* RK_VOP2_TEST_PATTERN */

EFI_STATUS
EFIAPI
LcdGraphicsSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  FillColour;
  LCD_INSTANCE                   *Instance;
  EFI_PHYSICAL_ADDRESS           VramBaseAddress;
  UINTN                          VramSize;
  UINTN                          NumVramPages;
  UINTN                          NumPreviousVramPages;
  DISPLAY_MODE                   *Mode;
  DRM_DISPLAY_MODE               *DrmMode;
  DISPLAY_STATE                  *DisplayState;
  ROCKCHIP_CRTC_PROTOCOL         *Crtc;
  CRTC_STATE                     *CrtcState;
  ROCKCHIP_CONNECTOR_PROTOCOL    *Connector;
  CONNECTOR_STATE                *ConnectorState;
  UINTN                          Index;

  Instance = LCD_INSTANCE_FROM_GOP_THIS (This);

  if (ModeNumber >= This->Mode->MaxMode) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Unsupported mode number %d\n",
      __func__,
      ModeNumber
      ));
    Status = EFI_UNSUPPORTED;
    goto EXIT;
  }

  if (ModeNumber == This->Mode->Mode) {
    Status = EFI_SUCCESS;
    goto EXIT;
  }

  Mode = &Instance->DisplayModes[ModeNumber];

  VramBaseAddress = This->Mode->FrameBufferBase;

  VramSize = Mode->HActive * Mode->VActive * RK_BYTES_PER_PIXEL;

  NumVramPages         = EFI_SIZE_TO_PAGES (VramSize);
  NumPreviousVramPages = EFI_SIZE_TO_PAGES (This->Mode->FrameBufferSize);

  if (NumPreviousVramPages < NumVramPages) {
    if (NumPreviousVramPages != 0) {
      gBS->FreePages (VramBaseAddress, NumPreviousVramPages);
      This->Mode->FrameBufferSize = 0;
    }

    VramBaseAddress = SIZE_4GB - 1; // VOP2 only supports 32-bit addresses
    Status          = gBS->AllocatePages (
                             AllocateMaxAddress,
                             EfiRuntimeServicesData,
                             NumVramPages,
                             &VramBaseAddress
                             );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Could not allocate %u pages for mode %u: %r\n",
        __func__,
        NumVramPages,
        ModeNumber,
        Status
        ));
      return Status;
    }

    Status = mCpu->SetMemoryAttributes (
                     mCpu,
                     VramBaseAddress,
                     ALIGN_VALUE (VramSize, EFI_PAGE_SIZE),
                     EFI_MEMORY_WC
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Couldn't set framebuffer attributes: %r\n",
        __func__,
        Status
        ));
      goto EXIT;
    }
  }

  // Update the UEFI mode information
  This->Mode->Mode = ModeNumber;

  LcdGraphicsSetModeInfo (This, This->Mode->Info, Mode, TRUE);

  Instance->Mode.SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  This->Mode->FrameBufferBase = VramBaseAddress;
  This->Mode->FrameBufferSize = VramSize;

  DEBUG ((DEBUG_ERROR,
    "[RK3576-GOP] FB installed: %ux%u PPSL=%u stride=%u bytes "
    "FbBase=0x%lx FbSize=%u (mode HxV = %ux%u)\n",
    This->Mode->Info->HorizontalResolution,
    This->Mode->Info->VerticalResolution,
    This->Mode->Info->PixelsPerScanLine,
    This->Mode->Info->PixelsPerScanLine * RK_BYTES_PER_PIXEL,
    (UINT64)VramBaseAddress,
    VramSize,
    Mode->HActive,
    Mode->VActive));

  // The UEFI spec requires that we now clear the visible portions of the
  // output display to black.

  // Set the fill colour to black
  SetMem (&FillColour, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL), 0x0);

  // Fill the entire visible area with the same colour.
  Status = This->Blt (
                   This,
                   &FillColour,
                   EfiBltVideoFill,
                   0,
                   0,
                   0,
                   0,
                   This->Mode->Info->HorizontalResolution,
                   This->Mode->Info->VerticalResolution,
                   0
                   );

#if RK_VOP2_TEST_PATTERN
  //
  // Overwrite the black fill the UEFI spec just asked for. This is a bring-up
  // build; the pattern has to be in memory before scanout starts.
  //
  RkDrawTestPattern (
    VramBaseAddress,
    This->Mode->Info->HorizontalResolution,
    This->Mode->Info->VerticalResolution,
    This->Mode->Info->PixelsPerScanLine,
    0
    );
#endif

  for (Index = 0; Index < Instance->DisplayStatesCount; Index++) {
    DisplayState = Instance->DisplayStates[Index];
    if ((DisplayState == NULL) || !DisplayState->IsEnable) {
      continue;
    }

    Crtc           = (ROCKCHIP_CRTC_PROTOCOL *)DisplayState->CrtcState.Crtc;
    Connector      = (ROCKCHIP_CONNECTOR_PROTOCOL *)DisplayState->ConnectorState.Connector;
    CrtcState      = &DisplayState->CrtcState;
    ConnectorState = &DisplayState->ConnectorState;
    DrmMode        = &DisplayState->ConnectorState.DisplayMode;

    DisplayModeToDrm (Mode, DrmMode);
    ConnectorState->DisplayModeVic = Mode->Vic;

    DEBUG ((
      DEBUG_INFO,
      "%a: detailed mode clock %u kHz, flags[%x]\n"
      "          H: %04d %04d %04d %04d\n"
      "          V: %04d %04d %04d %04d\n"
      "      bus_format: %x\n",
      __func__,
      DrmMode->Clock,
      DrmMode->Flags,
      DrmMode->HDisplay,
      DrmMode->HSyncStart,
      DrmMode->HSyncEnd,
      DrmMode->HTotal,
      DrmMode->VDisplay,
      DrmMode->VSyncStart,
      DrmMode->VSyncEnd,
      DrmMode->VTotal,
      ConnectorState->BusFormat
      ));

    DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] DisplaySetCrtcInfo enter\n", (UINT32)Index));
    Status = DisplaySetCrtcInfo (DrmMode, CRTC_INTERLACE_HALVE_V);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] DisplaySetCrtcInfo FAILED %r\n", (UINT32)Index, Status));
      goto EXIT;
    }
    DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] DisplaySetCrtcInfo OK\n", (UINT32)Index));

    if (Crtc->Init != NULL) {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Init enter\n", (UINT32)Index));
      Status = Crtc->Init (Crtc, DisplayState);
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Init exit %r\n", (UINT32)Index, Status));
      if (EFI_ERROR (Status)) {
        goto EXIT;
      }
    } else {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Init is NULL\n", (UINT32)Index));
    }

    /* adapt to uefi display architecture */
    CrtcState->Format  = ROCKCHIP_FMT_ARGB8888;
    CrtcState->SrcW    = ConnectorState->DisplayMode.HDisplay;
    CrtcState->SrcH    = ConnectorState->DisplayMode.VDisplay;
    CrtcState->SrcX    = 0;
    CrtcState->SrcY    = 0;
    CrtcState->CrtcW   = ConnectorState->DisplayMode.HDisplay;
    CrtcState->CrtcH   = ConnectorState->DisplayMode.VDisplay;
    CrtcState->CrtcX   = 0;
    CrtcState->CrtcY   = 0;
    CrtcState->YMirror = 0;
    CrtcState->RBSwap  = 0;

    CrtcState->XVirtual   = ALIGN (CrtcState->SrcW * RK_BYTES_PER_PIXEL * 8, 32) >> 5;
    CrtcState->DMAAddress = (UINT32)VramBaseAddress;

    if (Crtc->SetPlane != NULL) {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->SetPlane enter\n", (UINT32)Index));
      Crtc->SetPlane (Crtc, DisplayState);
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->SetPlane exit\n", (UINT32)Index));
    } else {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->SetPlane is NULL\n", (UINT32)Index));
    }

    if (Crtc->Enable != NULL) {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Enable enter\n", (UINT32)Index));
      Crtc->Enable (Crtc, DisplayState);
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Enable exit\n", (UINT32)Index));
    } else {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Crtc->Enable is NULL\n", (UINT32)Index));
    }

    if (Connector->Enable != NULL) {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Connector->Enable enter\n", (UINT32)Index));
      Connector->Enable (Connector, DisplayState);
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Connector->Enable exit\n", (UINT32)Index));
    } else {
      DEBUG ((DEBUG_ERROR, "[LCD-STEP] [%u] Connector->Enable is NULL\n", (UINT32)Index));
    }
  }

#if RK_VOP2_TEST_PATTERN
  //
  // Second marker, written while scanout is already running.
  //
  RkDrawTestPattern (
    VramBaseAddress,
    This->Mode->Info->HorizontalResolution,
    This->Mode->Info->VerticalResolution,
    This->Mode->Info->PixelsPerScanLine,
    1
    );
#endif

EXIT:
  return Status;
}

BOOLEAN
IsDisplayModeSupported (
  IN CONNECTOR_STATE     *ConnectorState,
  IN CONST DISPLAY_MODE  *DisplayMode
  )
{
  DISPLAY_SINK_INFO  *SinkInfo;

  SinkInfo = &ConnectorState->SinkInfo;

  // TODO: should query CRTC and connector instead.

  if ((DisplayMode->OscFreq < VOP_PIXEL_CLOCK_MIN) ||
      (DisplayMode->OscFreq > VOP_PIXEL_CLOCK_MAX) ||
      (DisplayMode->HActive < VOP_HORIZONTAL_RES_MIN) ||
      (DisplayMode->HActive > VOP_HORIZONTAL_RES_MAX) ||
      (DisplayMode->VActive < VOP_VERTICAL_RES_MIN) ||
      (DisplayMode->VActive > VOP_VERTICAL_RES_MAX))
  {
    return FALSE;
  }

  if (ConnectorState->Type == DRM_MODE_CONNECTOR_HDMIA) {
    if ((DisplayMode->OscFreq > 340000) &&
        (!SinkInfo->HdmiInfo.Hdmi20Supported ||
         SinkInfo->HdmiInfo.Hdmi20SpeedLimited ||
         !SinkInfo->HdmiInfo.ScdcSupported))
    {
      return FALSE;
    }
  }

  return TRUE;
}
