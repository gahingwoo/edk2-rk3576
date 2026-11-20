/** @file
 *
 *  Screenshots of what the firmware shows on the display, as PNG files.
 *
 *  Ctrl+Alt+F12 on a keyboard (either Ctrl, either Alt), or F12 on the
 *  serial console, saves the picture to the first writable FAT file
 *  system, as \screenshots\YYYYMMDD-HHMMSS.png:
 *
 *    1. a USB stick,
 *    2. any other removable medium (an SD card),
 *    3. anything else that is writable (the ESP on the eMMC or an NVMe).
 *
 *  When the clock has not been set (a year before 2025) the name is a
 *  counter instead, \screenshots\shot-NNNN.png.
 *
 *  The picture is the GOP's current mode, read with Blt, so it is what
 *  the firmware draws: on the TaishanPi 3M the rotated landscape menus come
 *  out landscape, and a picture drawn unrotated comes out as it is.  When
 *  it is saved, a green square shows for a moment in the top left corner
 *  (red: it failed, yellow: nowhere to write), and the path goes to the
 *  serial log.
 *
 *  Why F12 on serial works: the hot key is registered with the console
 *  splitter, which passes it on to every keyboard, the serial terminal
 *  too.  TerminalDxe matches a registered key on the key alone and ignores
 *  Ctrl and Alt, which a serial line cannot carry; F12 is ESC [ 2 4 ~,
 *  which every terminal emulator sends.  A USB keyboard matches Ctrl and
 *  Alt exactly, so plain F12 on it does nothing.
 *
 *  The work is not done in the key notification, which runs at the TPL of
 *  the keyboard driver: that only signals an event at TPL_CALLBACK, where
 *  the file system and the block devices may be used.  Nothing is taken
 *  after ExitBootServices.
 *
 *  The idea and the PNG encoder come from LongSoft's CrScreenshotDxe
 *  (github.com/LongSoft/CrScreenshotDxe, BSD-2-Clause, Nikolaj Schlej),
 *  whose LodePNG build for UEFI is used here as it is (lodepng.c,
 *  lodepng.h, zlib licence).  The driver around it is new.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextInEx.h>

#include "lodepng.h"

#define SHOT_DIRECTORY      L"screenshots"
#define STATUS_SIDE         32
#define STATUS_SHOW_US      (400 * 1000)
#define MAX_COUNTER         9999

STATIC EFI_EVENT                          mShotEvent;
STATIC BOOLEAN                            mGone;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *mSplitter;
STATIC VOID                               *mTextInRegistration;

/**
  The display's own GOP: the first one with a device path (the console
  splitter's has none and only passes calls on), else whatever is there.
**/
STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
ShotFindGop (
  VOID
  )
{
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  VOID                          *Path;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Any;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }

  Any = NULL;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) ||
        (Gop->Mode == NULL) || (Gop->Mode->Info == NULL))
    {
      continue;
    }

    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, &Path))) {
      FreePool (Handles);
      return Gop;
    }

    if (Any == NULL) {
      Any = Gop;
    }
  }

  FreePool (Handles);
  return Any;
}

/**
  A square in the top left corner for a moment, to say how it went.  What
  was there is put back.
**/
STATIC
VOID
ShotShowStatus (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT8                         Red,
  IN UINT8                         Green,
  IN UINT8                         Blue
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Square;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Saved;
  UINTN                          Side;

  if (Gop == NULL) {
    return;
  }

  Side = MIN (STATUS_SIDE, MIN (Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution));
  if (Side == 0) {
    return;
  }

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

/**
  How much a file system is preferred: 0 a USB stick, 1 other removable
  media, 2 the rest.  MAX_UINTN: read only, do not use.
**/
STATIC
UINTN
ShotRank (
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
      if ((DevicePathType (Path) == MESSAGING_DEVICE_PATH) &&
          ((DevicePathSubType (Path) == MSG_USB_DP) || (DevicePathSubType (Path) == MSG_USB_CLASS_DP)))
      {
        return 0;
      }
    }
  }

  return Removable ? 1 : 2;
}

/**
  The file name to use in the screenshot directory: the date and time, or
  the first free counter when the clock is not set.
**/
STATIC
VOID
ShotFileName (
  IN  EFI_FILE_PROTOCOL  *Dir,
  OUT CHAR16             *Name,
  IN  UINTN              NameSize
  )
{
  EFI_TIME           Time;
  EFI_FILE_PROTOCOL  *Probe;
  UINTN              Index;
  BOOLEAN            Clock;

  Clock = !EFI_ERROR (gRT->GetTime (&Time, NULL)) && (Time.Year >= 2025) && (Time.Year < 2100);

  for (Index = 1; Index <= MAX_COUNTER; Index++) {
    if (Clock) {
      if (Index == 1) {
        UnicodeSPrint (
          Name,
          NameSize,
          L"%04u%02u%02u-%02u%02u%02u.png",
          Time.Year,
          Time.Month,
          Time.Day,
          Time.Hour,
          Time.Minute,
          Time.Second
          );
      } else {
        // two in the same second
        UnicodeSPrint (
          Name,
          NameSize,
          L"%04u%02u%02u-%02u%02u%02u-%u.png",
          Time.Year,
          Time.Month,
          Time.Day,
          Time.Hour,
          Time.Minute,
          Time.Second,
          Index
          );
      }
    } else {
      UnicodeSPrint (Name, NameSize, L"shot-%04u.png", Index);
    }

    if (EFI_ERROR (Dir->Open (Dir, &Probe, Name, EFI_FILE_MODE_READ, 0))) {
      return;
    }

    Probe->Close (Probe);
  }
}

/**
  Write the PNG to the best writable file system.

  @retval EFI_NOT_FOUND  No writable file system.
**/
STATIC
EFI_STATUS
ShotSave (
  IN  UINT8   *Png,
  IN  UINTN   PngSize,
  OUT CHAR16  *Where,
  IN  UINTN   WhereSize
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Rank;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *Dir;
  EFI_FILE_PROTOCOL                *File;
  CHAR16                           Name[40];
  UINTN                            Size;
  CHAR16                           *PathText;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (Rank = 0; Rank <= 2; Rank++) {
    for (Index = 0; Index < Count; Index++) {
      if (ShotRank (Handles[Index]) != Rank) {
        continue;
      }

      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
          EFI_ERROR (Fs->OpenVolume (Fs, &Root)))
      {
        continue;
      }

      Dir = NULL;
      if (EFI_ERROR (
            Root->Open (
                    Root,
                    &Dir,
                    SHOT_DIRECTORY,
                    EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                    EFI_FILE_DIRECTORY
                    )
            ))
      {
        Root->Close (Root);
        continue;
      }

      ShotFileName (Dir, Name, sizeof (Name));
      File = NULL;
      if (EFI_ERROR (Dir->Open (Dir, &File, Name, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0))) {
        Dir->Close (Dir);
        Root->Close (Root);
        continue;
      }

      Size   = PngSize;
      Status = File->Write (File, &Size, Png);
      if (!EFI_ERROR (Status) && (Size != PngSize)) {
        Status = EFI_VOLUME_FULL;
      }

      if (EFI_ERROR (Status)) {
        File->Delete (File);      // closes it too
      } else {
        Status = File->Flush (File);
        File->Close (File);
      }

      Dir->Close (Dir);
      Root->Close (Root);

      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "Screenshot: writing %s failed: %r, trying the next file system\n", Name, Status));
        continue;
      }

      PathText = ConvertDevicePathToText (DevicePathFromHandle (Handles[Index]), TRUE, TRUE);
      UnicodeSPrint (Where, WhereSize, L"\\%s\\%s on %s", SHOT_DIRECTORY, Name, (PathText != NULL) ? PathText : L"?");
      if (PathText != NULL) {
        FreePool (PathText);
      }

      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  return Status;
}

STATIC
VOID
EFIAPI
ShotTake (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Pixels;
  UINT8                          *Rgb;
  UINT32                         Width;
  UINT32                         Height;
  UINTN                          Index;
  UINTN                          Count;
  unsigned char                  *Png;
  size_t                         PngSize;
  unsigned                       Error;
  UINT64                         Start;
  UINT64                         Captured;
  CHAR16                         Where[256];

  if (mGone) {
    return;
  }

  Start = GetPerformanceCounter ();
  Gop   = ShotFindGop ();
  if (Gop == NULL) {
    DEBUG ((DEBUG_ERROR, "Screenshot: no graphics output\n"));
    return;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;
  Count  = (UINTN)Width * Height;
  Pixels = AllocatePool (Count * sizeof (*Pixels));
  if (Pixels == NULL) {
    DEBUG ((DEBUG_ERROR, "Screenshot: no memory for %ux%u\n", Width, Height));
    ShotShowStatus (Gop, 0xff, 0x00, 0x00);
    return;
  }

  //
  // The whole picture of the current mode, before anything else is drawn.
  //
  Status = Gop->Blt (Gop, Pixels, EfiBltVideoToBltBuffer, 0, 0, 0, 0, Width, Height, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Screenshot: reading the screen failed: %r\n", Status));
    FreePool (Pixels);
    ShotShowStatus (Gop, 0xff, 0x00, 0x00);
    return;
  }

  Captured = GetPerformanceCounter ();

  // blue, green, red, reserved -> red, green, blue, in place
  Rgb = (UINT8 *)Pixels;
  for (Index = 0; Index < Count; Index++) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL  P = Pixels[Index];

    Rgb[Index * 3]     = P.Red;
    Rgb[Index * 3 + 1] = P.Green;
    Rgb[Index * 3 + 2] = P.Blue;
  }

  Png     = NULL;
  PngSize = 0;
  Error   = lodepng_encode24 (&Png, &PngSize, Rgb, Width, Height);
  FreePool (Pixels);
  if ((Error != 0) || (Png == NULL)) {
    DEBUG ((DEBUG_ERROR, "Screenshot: PNG encoding failed (lodepng error %u)\n", Error));
    ShotShowStatus (Gop, 0xff, 0x00, 0x00);
    return;
  }

  Status = ShotSave (Png, PngSize, Where, sizeof (Where));
  lodepng_free (Png);

  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_ERROR, "Screenshot: no writable FAT file system (plug in a USB stick)\n"));
    ShotShowStatus (Gop, 0xff, 0xff, 0x00);
    return;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Screenshot: saving failed: %r\n", Status));
    ShotShowStatus (Gop, 0xff, 0x00, 0x00);
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "Screenshot: %s, %ux%u, %u bytes, read in %lu ms, all in %lu ms\n",
    Where,
    Width,
    Height,
    (UINT32)PngSize,
    GetTimeInNanoSecond (Captured - Start) / 1000000,
    GetTimeInNanoSecond (GetPerformanceCounter () - Start) / 1000000
    ));
  ShotShowStatus (Gop, 0x00, 0xff, 0x00);
}

/**
  The hot key.  This runs at whatever TPL the keyboard driver calls it at,
  so it only asks for the screenshot.
**/
STATIC
EFI_STATUS
EFIAPI
ShotKey (
  IN EFI_KEY_DATA  *KeyData
  )
{
  if (!mGone) {
    gBS->SignalEvent (mShotEvent);
  }

  return EFI_SUCCESS;
}

/**
  Register Ctrl+Alt+F12 with the console splitter: the one text input
  without a device path.  It passes the registration on to every keyboard,
  present and future.  Each Ctrl and each Alt, left or right.
**/
STATIC
VOID
EFIAPI
ShotTextInNotify (
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
  UINTN                              Registered;

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

    Registered = 0;
    for (Combo = 0; Combo < ARRAY_SIZE (Shift); Combo++) {
      ZeroMem (&Key, sizeof (Key));
      Key.Key.ScanCode               = SCAN_F12;
      Key.KeyState.KeyShiftState     = EFI_SHIFT_STATE_VALID | Shift[Combo];
      if (!EFI_ERROR (TextInEx->RegisterKeyNotify (TextInEx, &Key, ShotKey, &NotifyHandle))) {
        Registered++;
      }
    }

    if (Registered != 0) {
      mSplitter = TextInEx;
      DEBUG ((DEBUG_INFO, "Screenshot: Ctrl+Alt+F12 (F12 on serial) saves the screen as PNG\n"));
      break;
    }
  }

  FreePool (Handles);
}

STATIC
VOID
EFIAPI
ShotExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mGone = TRUE;
}

EFI_STATUS
EFIAPI
ScreenshotDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ShotTake, NULL, &mShotEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_NOTIFY,
         ShotExitBootServices,
         NULL,
         &gEfiEventExitBootServicesGuid,
         &Event
         );

  //
  // The console splitter may come before or after this driver.
  //
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ShotTextInNotify, NULL, &Event);
  if (!EFI_ERROR (Status)) {
    gBS->RegisterProtocolNotify (&gEfiSimpleTextInputExProtocolGuid, Event, &mTextInRegistration);
    ShotTextInNotify (NULL, NULL);
  }

  return EFI_SUCCESS;
}
