/** @file
 *
 *  Touch test for the TaishanPi 3M's MIPI DSI screen.
 *
 *  Draws a dot where the touch controller says a finger is and prints the
 *  raw coordinates, on the screen and on the serial console.  The four
 *  corners of the panel are marked so that the controller's axes can be
 *  compared with the panel's:
 *
 *      red    (0, 0)            green  (max X, 0)
 *      blue   (0, max Y)        white  (max X, max Y)
 *
 *  Touch the red square: the numbers should be near 0, 0, and the yellow dot
 *  should be under the finger.  If the dot shows up in another corner, the
 *  controller's origin is not the panel's.
 *
 *  It is started as a boot option, so the firmware's picture is already back
 *  in the panel's native orientation and coordinates map one to one.
 *
 *  Leave with Q on a keyboard, or by putting three fingers down.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>

#include "TouchKeys.h"

#define CORNER  36
#define DOT     9

STATIC
VOID
Fill (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN INTN                          X,
  IN INTN                          Y,
  IN UINTN                         W,
  IN UINTN                         H,
  IN UINT8                         R,
  IN UINT8                         G,
  IN UINT8                         B
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;
  INTN                           MaxX = Gop->Mode->Info->HorizontalResolution;
  INTN                           MaxY = Gop->Mode->Info->VerticalResolution;

  if (X < 0) {
    X = 0;
  }

  if (Y < 0) {
    Y = 0;
  }

  if ((X >= MaxX) || (Y >= MaxY)) {
    return;
  }

  if (X + (INTN)W > MaxX) {
    W = MaxX - X;
  }

  if (Y + (INTN)H > MaxY) {
    H = MaxY - Y;
  }

  Pixel.Red      = R;
  Pixel.Green    = G;
  Pixel.Blue     = B;
  Pixel.Reserved = 0;
  Gop->Blt (Gop, &Pixel, EfiBltVideoFill, 0, 0, X, Y, W, H, 0);
}

STATIC
EFI_ABSOLUTE_POINTER_PROTOCOL *
FindTouch (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  EFI_DEVICE_PATH_PROTOCOL       *Path;
  EFI_ABSOLUTE_POINTER_PROTOCOL  *Pointer;
  EFI_GUID                       Guid = TOUCH_KEYS_DEVICE_GUID;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  Pointer = NULL;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (EFI_ERROR (Status) ||
        (Path->Type != HARDWARE_DEVICE_PATH) || (Path->SubType != HW_VENDOR_DP) ||
        !CompareGuid (&((VENDOR_DEVICE_PATH *)Path)->Guid, &Guid))
    {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Pointer))) {
      Pointer = NULL;
    }

    break;
  }

  gBS->FreePool (Handles);
  return Pointer;
}

EFI_STATUS
EFIAPI
TouchTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_ABSOLUTE_POINTER_PROTOCOL  *Touch;
  EFI_ABSOLUTE_POINTER_STATE     State;
  EFI_EVENT                      Events[2];
  UINTN                          Which;
  EFI_INPUT_KEY                  Key;
  UINTN                          W, H;
  UINT64                         MaxX, MaxY;
  INTN                           X, Y;
  UINTN                          Reports = 0;

  Touch = FindTouch ();
  if (Touch == NULL) {
    Print (L"Touch Test: no touch controller (TouchKeysDxe did not find one).\n");
    gBS->Stall (4000000);
    return EFI_NOT_FOUND;
  }

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  }

  if (EFI_ERROR (Status)) {
    Print (L"Touch Test: no graphics output.\n");
    gBS->Stall (4000000);
    return Status;
  }

  W    = Gop->Mode->Info->HorizontalResolution;
  H    = Gop->Mode->Info->VerticalResolution;
  MaxX = Touch->Mode->AbsoluteMaxX;
  MaxY = Touch->Mode->AbsoluteMaxY;

  gST->ConOut->ClearScreen (gST->ConOut);
  Fill (Gop, 0, 0, W, H, 0, 0, 0);
  Fill (Gop, 0, 0, CORNER, CORNER, 255, 0, 0);                              // red:   0, 0
  Fill (Gop, W - CORNER, 0, CORNER, CORNER, 0, 255, 0);                     // green: max X, 0
  Fill (Gop, 0, H - CORNER, CORNER, CORNER, 0, 0, 255);                     // blue:  0, max Y
  Fill (Gop, W - CORNER, H - CORNER, CORNER, CORNER, 255, 255, 255);        // white: max X, max Y

  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 4);
  Print (L"  Touch Test\n\n");
  Print (L"  screen %u x %u, touch 0..%lu x 0..%lu\n", W, H, MaxX, MaxY);
  Print (L"  red = (0,0)  green = (maxX,0)\n  blue = (0,maxY)  white = (maxX,maxY)\n\n");
  Print (L"  Touch the red corner: expect about 0, 0.\n");
  Print (L"  Q or three fingers to leave.\n\n");
  if ((W != MaxX + 1) || (H != MaxY + 1)) {
    Print (L"  Screen and touch sizes differ: dots are scaled.\n\n");
  }

  DEBUG ((DEBUG_INFO, "TouchTest: screen %ux%u, touch max %lu,%lu\n", W, H, MaxX, MaxY));

  Events[0] = Touch->WaitForInput;
  Events[1] = gST->ConIn->WaitForKey;

  while (TRUE) {
    Status = gBS->WaitForEvent (2, Events, &Which);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Which == 1) {
      // Touches also arrive as keys (TouchKeysDxe); only Q leaves.
      if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key)) &&
          ((Key.UnicodeChar == L'q') || (Key.UnicodeChar == L'Q')))
      {
        break;
      }

      continue;
    }

    if (EFI_ERROR (Touch->GetState (Touch, &State))) {
      continue;
    }

    if (State.CurrentZ >= 3) {
      break;
    }

    if ((State.ActiveButtons & EFI_ABSP_TouchActive) == 0) {
      gST->ConOut->SetCursorPosition (gST->ConOut, 0, 15);
      Print (L"  released                              ");
      DEBUG ((DEBUG_INFO, "TouchTest: released\n"));
      continue;
    }

    X = (INTN)((State.CurrentX * (W - 1)) / (MaxX ? MaxX : 1));
    Y = (INTN)((State.CurrentY * (H - 1)) / (MaxY ? MaxY : 1));
    Fill (Gop, X - DOT / 2, Y - DOT / 2, DOT, DOT, 255, 255, 0);

    Reports++;
    gST->ConOut->SetCursorPosition (gST->ConOut, 0, 15);
    Print (L"  X = %4lu  Y = %4lu  fingers = %lu      ", State.CurrentX, State.CurrentY, State.CurrentZ);
    if ((Reports % 5) == 1) {
      DEBUG ((DEBUG_INFO, "TouchTest: X=%lu Y=%lu fingers=%lu\n", State.CurrentX, State.CurrentY, State.CurrentZ));
    }
  }

  gST->ConOut->ClearScreen (gST->ConOut);
  return EFI_SUCCESS;
}
