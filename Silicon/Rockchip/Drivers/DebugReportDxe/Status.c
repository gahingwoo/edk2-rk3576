/** @file
 *
 *  A line of text in the top left corner while a debug report is being
 *  collected, and what came of it afterwards.
 *
 *  Drawn with the firmware's own font (HII GetGlyph, so the Chinese
 *  glyphs of ZhHansFontDxe too), in the language set in setup.  What is
 *  under it is read first and put back when it goes.
 *
 *  It must not be in the screenshots the report takes.  ScreenshotDxe
 *  signals its START group just before it reads the picture and END just
 *  after; at START the box comes off, at END it goes back up, over
 *  whatever is on the screen by then.  The browser redraws the whole
 *  screen for each setup page, so the box may already be gone when START
 *  comes: what is under it is only put back while the box is still there,
 *  pixel for pixel, so that stale pixels never land on a new page.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiFont.h>
#include <Protocol/RockchipScreenshot.h>

#include "DebugReport.h"

#define BOX_X    8
#define BOX_Y    8
#define BOX_PAD  6

STATIC EFI_GUID  mStartGroup = ROCKCHIP_SCREENSHOT_START_GROUP_GUID;
STATIC EFI_GUID  mEndGroup   = ROCKCHIP_SCREENSHOT_END_GROUP_GUID;

STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBack   = { 0x38, 0x30, 0x2a, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBorder = { 0xa0, 0xa0, 0xa0, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mInk    = { 0xff, 0xff, 0xff, 0 };

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL   *mGop;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *mBox;      // the box as drawn
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *mUnder;    // what it covers
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *mCheck;    // the screen there now
STATIC UINTN                          mBoxW;
STATIC UINTN                          mBoxH;
STATIC UINT32                         mScreenW;
STATIC UINT32                         mScreenH;
STATIC BOOLEAN                        mShown;
STATIC BOOLEAN                        mOffForShot;

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
StatusFindGop (
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

STATIC
VOID
StatusFreeBuffers (
  VOID
  )
{
  if (mBox != NULL) {
    FreePool (mBox);
  }

  if (mUnder != NULL) {
    FreePool (mUnder);
  }

  if (mCheck != NULL) {
    FreePool (mCheck);
  }

  mBox   = NULL;
  mUnder = NULL;
  mCheck = NULL;
}

/**
  The box with the text in it, in mBox.
**/
STATIC
BOOLEAN
StatusRender (
  IN CONST CHAR16  *Text
  )
{
  EFI_HII_FONT_PROTOCOL          *Font;
  EFI_IMAGE_OUTPUT               *Glyph;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Paper;
  CONST CHAR16                   *C;
  UINTN                          TextW;
  UINTN                          X, Y, Gx, Gy;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&Font))) {
    return FALSE;
  }

  // the width first: a narrow glyph is 8 pixels, a wide one 16
  TextW = 0;
  for (C = Text; *C != 0; C++) {
    Glyph = NULL;
    if (!EFI_ERROR (Font->GetGlyph (Font, *C, NULL, &Glyph, NULL)) && (Glyph != NULL)) {
      TextW += Glyph->Width;
    }

    if (Glyph != NULL) {
      if (Glyph->Image.Bitmap != NULL) {
        FreePool (Glyph->Image.Bitmap);
      }

      FreePool (Glyph);
    }
  }

  mBoxW = MIN (TextW + 2 * BOX_PAD, (UINTN)mScreenW - BOX_X);
  mBoxH = MIN ((UINTN)EFI_GLYPH_HEIGHT + 2 * BOX_PAD, (UINTN)mScreenH - BOX_Y);
  mBox  = AllocatePool (mBoxW * mBoxH * sizeof (*mBox));
  mUnder = AllocatePool (mBoxW * mBoxH * sizeof (*mUnder));
  mCheck = AllocatePool (mBoxW * mBoxH * sizeof (*mCheck));
  if ((mBox == NULL) || (mUnder == NULL) || (mCheck == NULL)) {
    StatusFreeBuffers ();
    return FALSE;
  }

  for (Y = 0; Y < mBoxH; Y++) {
    for (X = 0; X < mBoxW; X++) {
      mBox[Y * mBoxW + X] = ((X == 0) || (Y == 0) || (X == mBoxW - 1) || (Y == mBoxH - 1)) ? mBorder : mBack;
    }
  }

  X = BOX_PAD;
  for (C = Text; *C != 0; C++) {
    Glyph = NULL;
    if (!EFI_ERROR (Font->GetGlyph (Font, *C, NULL, &Glyph, NULL)) && (Glyph != NULL) && (Glyph->Image.Bitmap != NULL)) {
      //
      // The glyph comes in the system colours; its top left pixel is
      // paper, the rest that differs is ink.
      //
      Paper = Glyph->Image.Bitmap[0];
      for (Gy = 0; (Gy < Glyph->Height) && (BOX_PAD + Gy < mBoxH - 1); Gy++) {
        for (Gx = 0; (Gx < Glyph->Width) && (X + Gx < mBoxW - 1); Gx++) {
          if (CompareMem (&Glyph->Image.Bitmap[Gy * Glyph->Width + Gx], &Paper, 3) != 0) {
            mBox[(BOX_PAD + Gy) * mBoxW + X + Gx] = mInk;
          }
        }
      }

      X += Glyph->Width;
    }

    if (Glyph != NULL) {
      if (Glyph->Image.Bitmap != NULL) {
        FreePool (Glyph->Image.Bitmap);
      }

      FreePool (Glyph);
    }
  }

  return TRUE;
}

/**
  Read what is under the box and draw it.  No allocation: it runs at
  TPL_NOTIFY too, after a screenshot.
**/
STATIC
VOID
StatusDraw (
  VOID
  )
{
  if ((mGop == NULL) || (mBox == NULL) ||
      (mGop->Mode->Info->HorizontalResolution != mScreenW) || (mGop->Mode->Info->VerticalResolution != mScreenH))
  {
    return;
  }

  if (EFI_ERROR (mGop->Blt (mGop, mUnder, EfiBltVideoToBltBuffer, BOX_X, BOX_Y, 0, 0, mBoxW, mBoxH, 0))) {
    return;
  }

  mGop->Blt (mGop, mBox, EfiBltBufferToVideo, 0, 0, BOX_X, BOX_Y, mBoxW, mBoxH, 0);
  mShown = TRUE;
}

/**
  Take the box off: put back what was under it, if the box is still on
  the screen as it was drawn.
**/
STATIC
VOID
StatusErase (
  VOID
  )
{
  UINTN  Index;

  if (!mShown) {
    return;
  }

  mShown = FALSE;
  if ((mGop == NULL) ||
      (mGop->Mode->Info->HorizontalResolution != mScreenW) || (mGop->Mode->Info->VerticalResolution != mScreenH) ||
      EFI_ERROR (mGop->Blt (mGop, mCheck, EfiBltVideoToBltBuffer, BOX_X, BOX_Y, 0, 0, mBoxW, mBoxH, 0)))
  {
    return;
  }

  for (Index = 0; Index < mBoxW * mBoxH; Index++) {
    if (CompareMem (&mCheck[Index], &mBox[Index], 3) != 0) {
      return;     // drawn over since: what was under it is stale
    }
  }

  mGop->Blt (mGop, mUnder, EfiBltBufferToVideo, 0, 0, BOX_X, BOX_Y, mBoxW, mBoxH, 0);
}

VOID
StatusShow (
  IN EFI_HII_HANDLE  Handle,
  IN EFI_STRING_ID   Token
  )
{
  CHAR16  *Text;

  StatusHide ();

  mGop = StatusFindGop ();
  if (mGop == NULL) {
    return;
  }

  mScreenW = mGop->Mode->Info->HorizontalResolution;
  mScreenH = mGop->Mode->Info->VerticalResolution;
  if ((mScreenW <= BOX_X + 2 * BOX_PAD) || (mScreenH <= BOX_Y + 2 * BOX_PAD)) {
    return;
  }

  Text = HiiGetString (Handle, Token, NULL);
  if (Text == NULL) {
    return;
  }

  if (StatusRender (Text)) {
    StatusDraw ();
  }

  FreePool (Text);
}

VOID
StatusHide (
  VOID
  )
{
  StatusErase ();
  mOffForShot = FALSE;
  StatusFreeBuffers ();
}

STATIC
VOID
EFIAPI
StatusShotStart (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mShown) {
    StatusErase ();
    mOffForShot = TRUE;
  }
}

STATIC
VOID
EFIAPI
StatusShotEnd (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mOffForShot) {
    mOffForShot = FALSE;
    StatusDraw ();
  }
}

EFI_STATUS
StatusInit (
  VOID
  )
{
  EFI_EVENT  Event;

  //
  // TPL_NOTIFY: ScreenshotDxe signals from TPL_CALLBACK and reads the
  // picture right after; these have to run before that.
  //
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, StatusShotStart, NULL, &mStartGroup, &Event);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, StatusShotEnd, NULL, &mEndGroup, &Event);
  return EFI_SUCCESS;
}
