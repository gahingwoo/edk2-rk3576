/** @file
 *
 *  Putting the keyboard, its toggle key and the mouse cursor on the screen.
 *
 *  Two layers sit over the picture: a panel (the toggle key or the
 *  keyboard) and, above it, the mouse cursor.  The picture underneath goes
 *  on being drawn by whoever draws it, and how that is handled depends on
 *  who that is.
 *
 *  While the firmware's own code runs (menus, setup, the Shell, anything
 *  started from the firmware volume), everything is drawn through the GOP's
 *  Blt.  The GOP's Blt is then replaced by OskBlt, which keeps a copy of
 *  the whole picture as it would be without the layers (the shadow).  A
 *  write lands in the shadow and goes to the screen only where no layer
 *  covers it; a read is answered from the shadow.  So text the console
 *  writes under the keyboard is not lost and not drawn over it: it is
 *  there when the keyboard closes, and a console that scrolls moves only
 *  the picture, never the keyboard.  The Blt is the GOP's rotated one on
 *  the TaishanPi 3M; going through the shadow does not cost more than
 *  before, as reads now come from memory instead of the framebuffer.
 *
 *  An application that does not come from the firmware volume (GRUB, the
 *  Windows boot manager, anything run from a disk) may write the
 *  framebuffer itself, which no Blt replacement sees.  While one runs the
 *  Blt is left alone: what is under a layer is read back when the layer is
 *  shown, put back when it goes away, and the layers are drawn again every
 *  half second in case the application drew over them.  Whatever such an
 *  application draws under a layer while it is up is lost when the layer
 *  goes away; it redraws its own screen soon enough.
 *
 *  A see-through keyboard (the opacity key) is blended over the shadow, one
 *  pixel at a time; labels, their outline and the pressed key stay opaque.
 *  Where the console writes under it, only that part is blended again.
 *  Without the Blt replacement there is no shadow of the whole picture, so
 *  the keyboard is blended over what was read back when it went up; on
 *  every half second redraw, the pixels that are no longer what was put
 *  there are taken as what the application drew, and become the picture
 *  under the keyboard.
 *
 *  The GOP changes under us: GraphicsConsole sets a mode when it starts,
 *  and on the TaishanPi 3M the rotation comes off and on around those
 *  applications, which also clears the screen.  Either replaces the GOP's
 *  Blt.  So the GOP is checked before anything is drawn, and when it is not
 *  what it was, everything is set up again from the screen as it is.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>

#include "OnScreenKeyboard.h"

#define MAX_PIECES  16

UINT8    mOskWantPanel = OSK_PANEL_NONE;
BOOLEAN  mOskWantTop;
BOOLEAN  mOskWantFloating;
UINT32   mOskFloatX = 0x8000;       // centred
UINT32   mOskFloatY = 0x10000;      // at the bottom
BOOLEAN  mOskWantCursor;
INT32    mOskCursorX;
INT32    mOskCursorY;
UINTN    mOskDirect;
BOOLEAN  mOskGone;
BOOLEAN  mOskHiddenForShot;

UINT32  mOskScreenW;
UINT32  mOskScreenH;
UINT32  mOskKeyboardW;
UINT32  mOskKeyboardH;

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL      *mGop;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT  mOrigBlt;
STATIC BOOLEAN                           mHooked;
STATIC BOOLEAN                           mReady;
STATIC UINT32                            mModeNumber;
STATIC EFI_PHYSICAL_ADDRESS              mFrameBufferBase;

STATIC OSK_PIXEL  *mShadow;
STATIC UINTN      mShadowPixels;
STATIC OSK_PIXEL  *mScratch;
STATIC UINTN      mScratchPixels;
STATIC OSK_PIXEL  *mKbdImage;
STATIC UINTN      mKbdImagePixels;
STATIC OSK_PIXEL  *mToggleImage;
STATIC UINTN      mToggleImagePixels;
STATIC OSK_PIXEL  *mPanelOut;          // what was last put on the screen over the panel
STATIC UINTN      mPanelOutPixels;
STATIC BOOLEAN    mPanelOutValid;
STATIC OSK_RECT   mToggleRect;
STATIC UINT32     mCursorScale = 1;

// what is on the screen now
STATIC UINT8     mPanelOn = OSK_PANEL_NONE;
STATIC OSK_RECT  mPanelRect;
STATIC BOOLEAN   mCursorOn;
STATIC OSK_RECT  mCursorRect;        // clipped to the screen
STATIC INT32     mCursorDrawnX;      // the hot spot it was drawn at
STATIC INT32     mCursorDrawnY;

STATIC
EFI_STATUS
EFIAPI
OskBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta  OPTIONAL
  );

/**
  Raise to TPL_CALLBACK, where the poll and the touch reports run, unless
  the caller is there or higher already.
**/
STATIC
EFI_TPL
OskRaise (
  VOID
  )
{
  EFI_TPL  Old;

  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (Old);
  if (Old < TPL_CALLBACK) {
    gBS->RaiseTPL (TPL_CALLBACK);
  }

  return Old;
}

STATIC
VOID
OskRestore (
  IN EFI_TPL  Old
  )
{
  if (Old < TPL_CALLBACK) {
    gBS->RestoreTPL (Old);
  }
}

STATIC
EFI_STATUS
OskDraw (
  IN OSK_PIXEL                          *Buffer,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  Operation,
  IN INT32                              Sx,
  IN INT32                              Sy,
  IN INT32                              Dx,
  IN INT32                              Dy,
  IN INT32                              W,
  IN INT32                              H,
  IN UINTN                              Delta
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT  Blt;

  Blt = mHooked ? mOrigBlt : mGop->Blt;
  if ((Blt == NULL) || (Blt == OskBlt) || (W <= 0) || (H <= 0)) {
    return EFI_NOT_READY;
  }

  return Blt (mGop, Buffer, Operation, (UINTN)Sx, (UINTN)Sy, (UINTN)Dx, (UINTN)Dy, (UINTN)W, (UINTN)H, Delta);
}

STATIC
UINTN
OskSubtract (
  IN  CONST OSK_RECT  *A,
  IN  CONST OSK_RECT  *B,
  OUT OSK_RECT        *Out
  )
{
  OSK_RECT  I;
  UINTN     N;

  if (!OskRectIntersect (A, B, &I)) {
    Out[0] = *A;
    return 1;
  }

  N = 0;
  if (I.Y > A->Y) {
    Out[N].X = A->X;
    Out[N].Y = A->Y;
    Out[N].W = A->W;
    Out[N].H = I.Y - A->Y;
    N++;
  }

  if (I.Y + I.H < A->Y + A->H) {
    Out[N].X = A->X;
    Out[N].Y = I.Y + I.H;
    Out[N].W = A->W;
    Out[N].H = A->Y + A->H - (I.Y + I.H);
    N++;
  }

  if (I.X > A->X) {
    Out[N].X = A->X;
    Out[N].Y = I.Y;
    Out[N].W = I.X - A->X;
    Out[N].H = I.H;
    N++;
  }

  if (I.X + I.W < A->X + A->W) {
    Out[N].X = I.X + I.W;
    Out[N].Y = I.Y;
    Out[N].W = A->X + A->W - (I.X + I.W);
    Out[N].H = I.H;
    N++;
  }

  return N;
}

/**
  R without the layers that are on the screen.
**/
STATIC
UINTN
OskOutsideLayers (
  IN  CONST OSK_RECT  *R,
  OUT OSK_RECT        *Pieces
  )
{
  OSK_RECT  Tmp[MAX_PIECES];
  OSK_RECT  Out[4];
  UINTN     Count, NewCount, Index, N, K;

  Pieces[0] = *R;
  Count     = 1;

  if (mPanelOn != OSK_PANEL_NONE) {
    NewCount = 0;
    for (Index = 0; Index < Count; Index++) {
      N = OskSubtract (&Pieces[Index], &mPanelRect, Out);
      for (K = 0; K < N; K++) {
        Tmp[NewCount++] = Out[K];
      }
    }

    CopyMem (Pieces, Tmp, NewCount * sizeof (OSK_RECT));
    Count = NewCount;
  }

  if (mCursorOn) {
    NewCount = 0;
    for (Index = 0; Index < Count; Index++) {
      N = OskSubtract (&Pieces[Index], &mCursorRect, Out);
      for (K = 0; K < N; K++) {
        Tmp[NewCount++] = Out[K];
      }
    }

    CopyMem (Pieces, Tmp, NewCount * sizeof (OSK_RECT));
    Count = NewCount;
  }

  return Count;
}

STATIC
VOID
OskClipToScreen (
  IN OUT OSK_RECT  *R
  )
{
  OSK_RECT  Screen = { 0, 0, (INT32)mOskScreenW, (INT32)mOskScreenH };

  OskRectIntersect (R, &Screen, R);
}

/**
  The layers over I, on the shadow, into the scratch buffer, and that to
  the screen.  I lies inside one layer, so it fits the scratch buffer.
**/
STATIC
VOID
OskComposeOne (
  IN CONST OSK_RECT  *I
  )
{
  INT32      Row, Col;
  OSK_RECT   P;
  OSK_PIXEL  *Image;
  UINT32     ImageW;
  OSK_PIXEL  *Line;
  OSK_PIXEL  *Src;
  UINT32     Alpha;
  UINT8      C;

  STATIC CONST OSK_PIXEL  Black = { 0x00, 0x00, 0x00, 0 };
  STATIC CONST OSK_PIXEL  White = { 0xff, 0xff, 0xff, 0 };

  if ((I->W <= 0) || (I->H <= 0) || ((UINTN)I->W * (UINTN)I->H > mScratchPixels)) {
    return;
  }

  for (Row = 0; Row < I->H; Row++) {
    CopyMem (
      mScratch + (UINTN)Row * I->W,
      mShadow + (UINTN)(I->Y + Row) * mOskScreenW + I->X,
      (UINTN)I->W * sizeof (OSK_PIXEL)
      );
  }

  if ((mPanelOn != OSK_PANEL_NONE) && OskRectIntersect (I, &mPanelRect, &P)) {
    Image  = (mPanelOn == OSK_PANEL_KEYBOARD) ? mKbdImage : mToggleImage;
    ImageW = (UINT32)mPanelRect.W;
    Alpha  = (mPanelOn == OSK_PANEL_KEYBOARD) ? OskAlpha () : 256;
    for (Row = 0; Row < P.H; Row++) {
      Line = mScratch + (UINTN)(P.Y - I->Y + Row) * I->W + (P.X - I->X);
      Src  = Image + (UINTN)(P.Y - mPanelRect.Y + Row) * ImageW + (P.X - mPanelRect.X);
      for (Col = 0; Col < P.W; Col++) {
        //
        // Over the picture as it is without us (the shadow, already in
        // the scratch buffer), except the pixels marked opaque.
        //
        if ((Alpha >= 256) || (Src[Col].Reserved == OSK_OPAQUE)) {
          Line[Col] = Src[Col];
        } else {
          Line[Col].Blue  = (UINT8)((Src[Col].Blue * Alpha + Line[Col].Blue * (256 - Alpha)) >> 8);
          Line[Col].Green = (UINT8)((Src[Col].Green * Alpha + Line[Col].Green * (256 - Alpha)) >> 8);
          Line[Col].Red   = (UINT8)((Src[Col].Red * Alpha + Line[Col].Red * (256 - Alpha)) >> 8);
        }

        Line[Col].Reserved = 0;
      }
    }
  }

  if (mCursorOn && OskRectIntersect (I, &mCursorRect, &P)) {
    for (Row = 0; Row < P.H; Row++) {
      Line = mScratch + (UINTN)(P.Y - I->Y + Row) * I->W + (P.X - I->X);
      for (Col = 0; Col < P.W; Col++) {
        C = OskCursorPixel (
              (UINT32)(P.X + Col - mCursorDrawnX),
              (UINT32)(P.Y + Row - mCursorDrawnY),
              mCursorScale
              );
        if (C == 1) {
          Line[Col] = Black;
        } else if (C == 2) {
          Line[Col] = White;
        }
      }
    }
  }

  OskDraw (mScratch, EfiBltBufferToVideo, 0, 0, I->X, I->Y, I->W, I->H, (UINTN)I->W * sizeof (OSK_PIXEL));

  //
  // Without the Blt replacement, keep what went on the screen over the
  // panel: the next look at the screen tells from it what the
  // application drew there since.
  //
  if (!mHooked && (mPanelOn != OSK_PANEL_NONE) && (mPanelOut != NULL) &&
      OskRectIntersect (I, &mPanelRect, &P))
  {
    for (Row = 0; Row < P.H; Row++) {
      CopyMem (
        mPanelOut + (UINTN)(P.Y - mPanelRect.Y + Row) * mPanelRect.W + (P.X - mPanelRect.X),
        mScratch + (UINTN)(P.Y - I->Y + Row) * I->W + (P.X - I->X),
        (UINTN)P.W * sizeof (OSK_PIXEL)
        );
    }
  }
}

STATIC
VOID
OskCompose (
  IN CONST OSK_RECT  *R
  )
{
  OSK_RECT  I;

  if ((mPanelOn != OSK_PANEL_NONE) && OskRectIntersect (R, &mPanelRect, &I)) {
    OskComposeOne (&I);
  }

  if (mCursorOn && OskRectIntersect (R, &mCursorRect, &I)) {
    OskComposeOne (&I);
  }
}

/**
  The screen in R from the shadow where no layer is, and the layers over
  the rest.
**/
STATIC
VOID
OskRefresh (
  IN CONST OSK_RECT  *R
  )
{
  OSK_RECT  Pieces[MAX_PIECES];
  UINTN     Count, Index;

  Count = OskOutsideLayers (R, Pieces);
  for (Index = 0; Index < Count; Index++) {
    OskDraw (
      mShadow,
      EfiBltBufferToVideo,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].W,
      Pieces[Index].H,
      mOskScreenW * sizeof (OSK_PIXEL)
      );
  }

  OskCompose (R);
}

/**
  Without the Blt replacement: read what is under R from the screen into
  the shadow, except where a layer already covers it (the shadow has that
  part from when that layer went up).
**/
STATIC
VOID
OskCapture (
  IN CONST OSK_RECT  *R
  )
{
  OSK_RECT  Pieces[MAX_PIECES];
  UINTN     Count, Index;

  if (mHooked) {
    return;
  }

  Count = OskOutsideLayers (R, Pieces);
  for (Index = 0; Index < Count; Index++) {
    OskDraw (
      mShadow,
      EfiBltVideoToBltBuffer,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].W,
      Pieces[Index].H,
      mOskScreenW * sizeof (OSK_PIXEL)
      );
  }
}

STATIC
BOOLEAN
OskGeometryMatches (
  VOID
  )
{
  return (mGop != NULL) && (mGop->Mode != NULL) && (mGop->Mode->Info != NULL) &&
         (mGop->Mode->Mode == mModeNumber) &&
         (mGop->Mode->Info->HorizontalResolution == mOskScreenW) &&
         (mGop->Mode->Info->VerticalResolution == mOskScreenH) &&
         (mGop->Mode->FrameBufferBase == mFrameBufferBase);
}

STATIC
BOOLEAN
OskStillValid (
  VOID
  )
{
  if (!mReady || !OskGeometryMatches ()) {
    return FALSE;
  }

  if (mHooked != (mOskDirect == 0)) {
    return FALSE;
  }

  return !mHooked || (mGop->Blt == OskBlt);
}

/**
  Forget the screen: it is not what we left it as.  Nothing is drawn.
**/
STATIC
VOID
OskLost (
  VOID
  )
{
  if ((mGop != NULL) && (mGop->Blt == OskBlt)) {
    mGop->Blt = mOrigBlt;
  }

  mHooked   = FALSE;
  mReady    = FALSE;
  mPanelOn  = OSK_PANEL_NONE;
  mCursorOn = FALSE;
}

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
OskFindGop (
  VOID
  )
{
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  VOID                          *Path;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Found;

  //
  // The display's own GOP, not the console splitter's: that one has no
  // device path and calls this one anyway.
  //
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }

  Found = NULL;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, &Path)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) ||
        (Gop->Mode == NULL) || (Gop->Mode->Info == NULL) ||
        (Gop->Mode->Mode >= Gop->Mode->MaxMode))
    {
      continue;
    }

    Found = Gop;
    break;
  }

  FreePool (Handles);
  return Found;
}

STATIC
BOOLEAN
OskGrow (
  IN OUT OSK_PIXEL  **Buffer,
  IN OUT UINTN      *Have,
  IN     UINTN      Need
  )
{
  if ((*Buffer != NULL) && (*Have >= Need)) {
    return TRUE;
  }

  if (*Buffer != NULL) {
    FreePool (*Buffer);
  }

  *Buffer = AllocatePool (Need * sizeof (OSK_PIXEL));
  *Have   = (*Buffer != NULL) ? Need : 0;
  return *Buffer != NULL;
}

/**
  Lay the keyboard out for the screen and the mode (docked or floating)
  wanted now, and draw its image.  Nothing of ours may be on the screen.
**/
STATIC
BOOLEAN
OskRelayout (
  VOID
  )
{
  UINTN  Need;

  OskLayout (mOskScreenW, mOskScreenH, mOskWantFloating, &mOskKeyboardW, &mOskKeyboardH, &mToggleRect, &mCursorScale);

  Need = MAX ((UINTN)mOskKeyboardW * mOskKeyboardH, (UINTN)mToggleRect.W * mToggleRect.H);
  Need = MAX (Need, (UINTN)OSK_CURSOR_W * OSK_CURSOR_H * mCursorScale * mCursorScale);
  if (!OskGrow (&mScratch, &mScratchPixels, Need) ||
      !OskGrow (&mPanelOut, &mPanelOutPixels, Need) ||
      !OskGrow (&mKbdImage, &mKbdImagePixels, (UINTN)mOskKeyboardW * mOskKeyboardH) ||
      !OskGrow (&mToggleImage, &mToggleImagePixels, (UINTN)mToggleRect.W * mToggleRect.H))
  {
    return FALSE;
  }

  OskRenderKeyboard (mKbdImage, mOskKeyboardW, mOskKeyboardH);
  OskRenderToggle (mToggleImage, (UINT32)mToggleRect.W, (UINT32)mToggleRect.H);
  return TRUE;
}

STATIC
BOOLEAN
OskInit (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      W, H;

  mGop = OskFindGop ();
  if (mGop == NULL) {
    return FALSE;
  }

  W = mGop->Mode->Info->HorizontalResolution;
  H = mGop->Mode->Info->VerticalResolution;
  if ((W < 320) || (H < 200)) {
    return FALSE;
  }

  mPanelOn    = OSK_PANEL_NONE;
  mCursorOn   = FALSE;
  mOskScreenW = W;
  mOskScreenH = H;
  if (!OskGrow (&mShadow, &mShadowPixels, (UINTN)W * H) || !OskRelayout ()) {
    return FALSE;
  }

  mModeNumber      = mGop->Mode->Mode;
  mFrameBufferBase = mGop->Mode->FrameBufferBase;

  if (mOskDirect == 0) {
    //
    // The picture as it is, then take the Blt over.  Nothing of ours is
    // on the screen at this point.
    //
    mHooked = FALSE;
    Status  = mGop->Blt (mGop, mShadow, EfiBltVideoToBltBuffer, 0, 0, 0, 0, W, H, 0);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "OnScreenKeyboard: cannot read the screen: %r\n", Status));
      return FALSE;
    }

    mOrigBlt  = mGop->Blt;
    mGop->Blt = OskBlt;
    mHooked   = TRUE;
  } else {
    mHooked = FALSE;
  }

  mReady = TRUE;
  DEBUG ((
    DEBUG_INFO,
    "OnScreenKeyboard: screen %ux%u, keyboard %u high, %a\n",
    W,
    H,
    mOskKeyboardH,
    mHooked ? "drawing through Blt" : "an application owns the framebuffer"
    ));
  return TRUE;
}

BOOLEAN
OskOverlayPrepare (
  VOID
  )
{
  if (mOskGone || mOskHiddenForShot) {
    return FALSE;
  }

  if (mReady && !OskStillValid ()) {
    OskLost ();
  }

  if (!mReady) {
    return OskInit ();
  }

  return TRUE;
}

/**
  OskCompose, and how long it took, for the record: the keyboard drawn
  in full is the most expensive thing done here.
**/
STATIC
VOID
OskTimedCompose (
  IN CONST OSK_RECT  *R,
  IN CONST CHAR8     *Why
  )
{
  UINT64  Start;
  UINT64  Ns;

  Start = GetPerformanceCounter ();
  OskCompose (R);
  Ns = GetTimeInNanoSecond (GetPerformanceCounter () - Start);
  if ((mPanelOn == OSK_PANEL_KEYBOARD) && (R->W * R->H == mPanelRect.W * mPanelRect.H)) {
    // DEBUG_VERBOSE: it would land in the middle of the menus on the serial console
    DEBUG ((
      DEBUG_VERBOSE,
      "OnScreenKeyboard: keyboard %dx%d %a at %u%%, %lu us\n",
      R->W,
      R->H,
      Why,
      (OskAlpha () * 100 + 128) / 256,
      Ns / 1000
      ));
  }
}

STATIC
VOID
OskPanelOff (
  VOID
  )
{
  OSK_RECT  Old;

  if (mPanelOn == OSK_PANEL_NONE) {
    return;
  }

  Old            = mPanelRect;
  mPanelOn       = OSK_PANEL_NONE;
  mPanelOutValid = FALSE;
  OskRefresh (&Old);
}

STATIC
VOID
OskPanelOnAt (
  IN UINT8           Kind,
  IN CONST OSK_RECT  *Rect
  )
{
  OskCapture (Rect);
  mPanelOn   = Kind;
  mPanelRect = *Rect;
  OskTimedCompose (Rect, "shown");
  mPanelOutValid = TRUE;
}

/**
  The keyboard to another place, same size (a drag).  What it leaves
  uncovered comes back from the shadow, without drawing the part that
  stays covered twice.
**/
STATIC
VOID
OskPanelMove (
  IN CONST OSK_RECT  *New
  )
{
  OSK_RECT  Old;
  OSK_RECT  Pieces[MAX_PIECES];
  OSK_RECT  I;
  UINTN     Count, Index;

  Old = mPanelRect;
  OskCapture (New);           // without the Blt replacement only
  mPanelRect = *New;

  Count = OskOutsideLayers (&Old, Pieces);
  for (Index = 0; Index < Count; Index++) {
    OskDraw (
      mShadow,
      EfiBltBufferToVideo,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].W,
      Pieces[Index].H,
      mOskScreenW * sizeof (OSK_PIXEL)
      );
  }

  OskCompose (New);
  if (mCursorOn && OskRectIntersect (&Old, &mCursorRect, &I)) {
    OskComposeOne (&I);
  }
}

STATIC
VOID
OskCursorOff (
  VOID
  )
{
  OSK_RECT  Old;

  if (!mCursorOn) {
    return;
  }

  Old       = mCursorRect;
  mCursorOn = FALSE;
  OskRefresh (&Old);
}

STATIC
VOID
OskCursorOnAt (
  IN INT32  X,
  IN INT32  Y
  )
{
  OSK_RECT  R;

  R.X = X;
  R.Y = Y;
  R.W = (INT32)(OSK_CURSOR_W * mCursorScale);
  R.H = (INT32)(OSK_CURSOR_H * mCursorScale);
  OskClipToScreen (&R);
  if ((R.W <= 0) || (R.H <= 0)) {
    return;
  }

  OskCapture (&R);
  mCursorRect   = R;
  mCursorDrawnX = X;
  mCursorDrawnY = Y;
  mCursorOn     = TRUE;
  OskCompose (&R);
}

VOID
OskOverlaySync (
  VOID
  )
{
  OSK_RECT  Want;

  //
  // Nothing to show: no Blt replacement either, and nothing set up.  The
  // same while a screenshot is taken.
  //
  if (((mOskWantPanel == OSK_PANEL_NONE) && !mOskWantCursor) || mOskHiddenForShot) {
    if (mReady) {
      OskOverlayDetach (TRUE);
    }

    return;
  }

  if (!OskOverlayPrepare ()) {
    return;
  }

  //
  // Docked and floating are two layouts.  Changing over takes everything
  // down first, as the keyboard changes size.
  //
  if ((mOskWantFloating != mOskFloating) ||
      ((mOskWantSize != OSK_SIZE_AUTO) && (mOskWantSize != mOskSize)))
  {
    OskCursorOff ();
    OskPanelOff ();
    if (!OskRelayout ()) {
      OskLost ();
      return;
    }
  }

  Want.X = Want.Y = Want.W = Want.H = 0;
  if (mOskWantPanel == OSK_PANEL_TOGGLE) {
    Want = mToggleRect;
  } else if (mOskWantPanel == OSK_PANEL_KEYBOARD) {
    Want.W = (INT32)mOskKeyboardW;
    Want.H = (INT32)mOskKeyboardH;
    if (mOskFloating) {
      Want.X = (INT32)(((UINT64)mOskFloatX * (mOskScreenW - mOskKeyboardW) + 0x8000) >> 16);
      Want.Y = (INT32)(((UINT64)mOskFloatY * (mOskScreenH - mOskKeyboardH) + 0x8000) >> 16);
    } else {
      // centred when it is narrower than the screen
      Want.X = (INT32)(mOskScreenW - mOskKeyboardW) / 2;
      Want.Y = mOskWantTop ? 0 : (INT32)(mOskScreenH - mOskKeyboardH);
    }
  }

  if ((mPanelOn == OSK_PANEL_KEYBOARD) && (mOskWantPanel == OSK_PANEL_KEYBOARD) &&
      (Want.W == mPanelRect.W) && (Want.H == mPanelRect.H) &&
      ((Want.X != mPanelRect.X) || (Want.Y != mPanelRect.Y)))
  {
    OskPanelMove (&Want);
  }

  if ((mPanelOn != mOskWantPanel) ||
      ((mPanelOn != OSK_PANEL_NONE) && (CompareMem (&Want, &mPanelRect, sizeof (Want)) != 0)))
  {
    //
    // The cursor comes off first, so that what is read back under the new
    // panel is the picture and not the cursor.
    //
    OskCursorOff ();
    OskPanelOff ();
    if (mOskWantPanel != OSK_PANEL_NONE) {
      OskPanelOnAt (mOskWantPanel, &Want);
    }
  }

  if (mCursorOn && (!mOskWantCursor || (mCursorDrawnX != mOskCursorX) || (mCursorDrawnY != mOskCursorY))) {
    OskCursorOff ();
  }

  if (!mCursorOn && mOskWantCursor) {
    OskCursorOnAt (mOskCursorX, mOskCursorY);
  }
}

UINT8
OskOverlayHit (
  IN  INT32  X,
  IN  INT32  Y,
  OUT INT32  *ImageX,
  OUT INT32  *ImageY
  )
{
  if (!mReady || (mPanelOn == OSK_PANEL_NONE) || !OskRectContains (&mPanelRect, X, Y)) {
    return OSK_PANEL_NONE;
  }

  *ImageX = X - mPanelRect.X;
  *ImageY = Y - mPanelRect.Y;
  return mPanelOn;
}

VOID
OskOverlayKeyboardChanged (
  IN INTN  Index
  )
{
  OSK_RECT  R;

  if (!mReady || (mKbdImage == NULL)) {
    return;
  }

  if (Index < 0) {
    OskRenderKeyboard (mKbdImage, mOskKeyboardW, mOskKeyboardH);
    R.X = 0;
    R.Y = 0;
    R.W = (INT32)mOskKeyboardW;
    R.H = (INT32)mOskKeyboardH;
  } else {
    if ((UINTN)Index >= mOskPlaceCount) {
      return;
    }

    OskRenderKey (mKbdImage, mOskKeyboardW, mOskKeyboardH, (UINTN)Index);
    R = mOskPlace[Index].Rect;
  }

  if ((mPanelOn != OSK_PANEL_KEYBOARD) || !OskStillValid ()) {
    return;
  }

  R.X += mPanelRect.X;
  R.Y += mPanelRect.Y;
  OskTimedCompose (&R, "redrawn");
}

BOOLEAN
OskOverlayKeyboardRect (
  OUT OSK_RECT  *Rect
  )
{
  if (!mReady || (mPanelOn != OSK_PANEL_KEYBOARD)) {
    return FALSE;
  }

  *Rect = mPanelRect;
  return TRUE;
}

VOID
OskOverlayAlphaChanged (
  VOID
  )
{
  // the opacity key shows the level, so the image is drawn again too
  OskOverlayKeyboardChanged (-1);
}

/**
  While an application owns the framebuffer, draw the layers again now
  and then, in case it drew over them.
**/
VOID
OskOverlayPeriodic (
  VOID
  )
{
  UINTN      Count;
  INT32      Row, Col;
  OSK_PIXEL  *Now;
  OSK_PIXEL  *Was;
  OSK_PIXEL  *Base;

  if (!mReady || mHooked || !OskStillValid ()) {
    return;
  }

  if (mPanelOn != OSK_PANEL_NONE) {
    //
    // Where the screen is not what we put there, the application drew:
    // that is the picture under the panel now.  So see-through keys show
    // what it drew, half a second late, and closing puts back its latest
    // picture rather than the one from when the keyboard opened.
    //
    Count = (UINTN)mPanelRect.W * mPanelRect.H;
    if (mPanelOutValid && (Count <= mScratchPixels) &&
        !EFI_ERROR (
           OskDraw (
             mScratch,
             EfiBltVideoToBltBuffer,
             mPanelRect.X,
             mPanelRect.Y,
             0,
             0,
             mPanelRect.W,
             mPanelRect.H,
             (UINTN)mPanelRect.W * sizeof (OSK_PIXEL)
             )
           ))
    {
      for (Row = 0; Row < mPanelRect.H; Row++) {
        Now  = mScratch + (UINTN)Row * mPanelRect.W;
        Was  = mPanelOut + (UINTN)Row * mPanelRect.W;
        Base = mShadow + (UINTN)(mPanelRect.Y + Row) * mOskScreenW + mPanelRect.X;
        for (Col = 0; Col < mPanelRect.W; Col++) {
          if ((Now[Col].Red != Was[Col].Red) || (Now[Col].Green != Was[Col].Green) || (Now[Col].Blue != Was[Col].Blue)) {
            Base[Col] = Now[Col];
          }
        }
      }
    }

    OskCompose (&mPanelRect);
  }

  if (mCursorOn) {
    OskCompose (&mCursorRect);
  }
}

/**
  Take everything off the screen (when Restore and the screen is still the
  one it was drawn on) and give the GOP its own Blt back.
**/
VOID
OskOverlayDetach (
  IN BOOLEAN  Restore
  )
{
  if (Restore && OskStillValid ()) {
    OskCursorOff ();
    OskPanelOff ();
  }

  OskLost ();
}

//
// The Blt replacement.
//

STATIC
BOOLEAN
OskRectOnScreen (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  W,
  IN UINTN  H
  )
{
  return (X < mOskScreenW) && (Y < mOskScreenH) &&
         (W <= mOskScreenW - X) && (H <= mOskScreenH - Y);
}

/**
  After the shadow changed in R: show the change where no layer is, and
  redraw the cursor where it is over R, as it lets the picture through
  around the arrow.  An opaque panel was not written, so it is still on
  the screen as it was; a see-through one is blended again over R.
**/
STATIC
VOID
OskShadowChanged (
  IN CONST OSK_RECT  *R
  )
{
  OSK_RECT  Pieces[MAX_PIECES];
  OSK_RECT  I;
  UINTN     Count, Index;

  Count = OskOutsideLayers (R, Pieces);
  for (Index = 0; Index < Count; Index++) {
    OskDraw (
      mShadow,
      EfiBltBufferToVideo,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].X,
      Pieces[Index].Y,
      Pieces[Index].W,
      Pieces[Index].H,
      mOskScreenW * sizeof (OSK_PIXEL)
      );
  }

  //
  // A see-through keyboard shows the change too: blend that part again.
  // Only what changed, so a character costs a character.
  //
  if ((mPanelOn == OSK_PANEL_KEYBOARD) && (OskAlpha () < 256) &&
      OskRectIntersect (R, &mPanelRect, &I))
  {
    OskComposeOne (&I);
  }

  if (mCursorOn && OskRectIntersect (R, &mCursorRect, &I)) {
    OskComposeOne (&I);
  }
}

STATIC
EFI_STATUS
EFIAPI
OskBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta  OPTIONAL
  )
{
  EFI_STATUS  Status;
  EFI_TPL     Tpl;
  OSK_RECT    R;
  UINTN       Row, Col;
  UINTN       Stride;
  OSK_PIXEL   *Line;
  UINT8       *Buffer;

  if (mOrigBlt == NULL) {
    return EFI_DEVICE_ERROR;
  }

  Tpl = OskRaise ();

  if (!mHooked || (This != mGop) || !OskGeometryMatches ()) {
    //
    // The mode is changing under us; the poll notices and starts over.
    //
    Status = mOrigBlt (This, BltBuffer, BltOperation, SourceX, SourceY, DestinationX, DestinationY, Width, Height, Delta);
    goto Done;
  }

  Status = EFI_INVALID_PARAMETER;
  if ((Width == 0) || (Height == 0) || ((UINTN)BltOperation >= EfiGraphicsOutputBltOperationMax) ||
      ((BltOperation != EfiBltVideoToVideo) && (BltBuffer == NULL)))
  {
    goto Done;
  }

  if (Delta == 0) {
    Delta = Width * sizeof (OSK_PIXEL);
  }

  Stride = mOskScreenW;
  Buffer = (UINT8 *)BltBuffer;

  switch (BltOperation) {
    case EfiBltVideoFill:
      if (!OskRectOnScreen (DestinationX, DestinationY, Width, Height)) {
        goto Done;
      }

      for (Row = 0; Row < Height; Row++) {
        Line = mShadow + (DestinationY + Row) * Stride + DestinationX;
        if (Row == 0) {
          for (Col = 0; Col < Width; Col++) {
            Line[Col] = *BltBuffer;
          }
        } else {
          CopyMem (Line, mShadow + DestinationY * Stride + DestinationX, Width * sizeof (OSK_PIXEL));
        }
      }

      break;

    case EfiBltBufferToVideo:
      if (!OskRectOnScreen (DestinationX, DestinationY, Width, Height)) {
        goto Done;
      }

      for (Row = 0; Row < Height; Row++) {
        CopyMem (
          mShadow + (DestinationY + Row) * Stride + DestinationX,
          Buffer + (SourceY + Row) * Delta + SourceX * sizeof (OSK_PIXEL),
          Width * sizeof (OSK_PIXEL)
          );
      }

      break;

    case EfiBltVideoToBltBuffer:
      if (!OskRectOnScreen (SourceX, SourceY, Width, Height)) {
        goto Done;
      }

      for (Row = 0; Row < Height; Row++) {
        CopyMem (
          Buffer + (DestinationY + Row) * Delta + DestinationX * sizeof (OSK_PIXEL),
          mShadow + (SourceY + Row) * Stride + SourceX,
          Width * sizeof (OSK_PIXEL)
          );
      }

      Status = EFI_SUCCESS;
      goto Done;

    case EfiBltVideoToVideo:
      if (!OskRectOnScreen (SourceX, SourceY, Width, Height) ||
          !OskRectOnScreen (DestinationX, DestinationY, Width, Height))
      {
        goto Done;
      }

      // CopyMem copies overlapping lines correctly; the order of the lines
      // is what keeps an overlapping move right.
      if (SourceY < DestinationY) {
        for (Row = Height; Row-- > 0;) {
          CopyMem (
            mShadow + (DestinationY + Row) * Stride + DestinationX,
            mShadow + (SourceY + Row) * Stride + SourceX,
            Width * sizeof (OSK_PIXEL)
            );
        }
      } else {
        for (Row = 0; Row < Height; Row++) {
          CopyMem (
            mShadow + (DestinationY + Row) * Stride + DestinationX,
            mShadow + (SourceY + Row) * Stride + SourceX,
            Width * sizeof (OSK_PIXEL)
            );
        }
      }

      break;

    default:
      goto Done;
  }

  R.X = (INT32)DestinationX;
  R.Y = (INT32)DestinationY;
  R.W = (INT32)Width;
  R.H = (INT32)Height;
  OskShadowChanged (&R);
  Status = EFI_SUCCESS;

Done:
  OskRestore (Tpl);
  return Status;
}
