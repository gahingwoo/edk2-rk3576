/** @file
 *
 *  The keyboard itself: which keys, where they are, and how they look.
 *
 *  A plain US PC layout in six rows, 15 letter keys wide.  Shift gives the
 *  second character of every key, so all printable ASCII is there without a
 *  separate symbols page.  The labels are the firmware's own font (the
 *  narrow glyphs GraphicsConsole registers with HII), scaled up by whole
 *  pixels on big screens.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/HiiFont.h>

#include "OnScreenKeyboard.h"

#define CH(Q, C, S)           { Q, OSK_ACT_KEY, FALSE, SCAN_NULL, C, S, NULL, NULL }
#define CTL(Q, C, L, R)       { Q, OSK_ACT_KEY, R, SCAN_NULL, C, 0, L, NULL }
#define SCN(Q, S, L, A, R)    { Q, OSK_ACT_KEY, R, S, 0, 0, L, A }
#define ACT(Q, A, L)          { Q, A, FALSE, SCAN_NULL, 0, 0, L, NULL }
#define ROW_END               { 0, 0, FALSE, 0, 0, 0, NULL, NULL }

#define ROWS       6
#define ROW_UNITS  60       // quarters across every row

STATIC CONST CHAR16  mArrowUp[]    = { 0x2191, 0 };
STATIC CONST CHAR16  mArrowDown[]  = { 0x2193, 0 };
STATIC CONST CHAR16  mArrowLeft[]  = { 0x2190, 0 };
STATIC CONST CHAR16  mArrowRight[] = { 0x2192, 0 };

STATIC CONST OSK_KEY  mRow0[] = {
  SCN (4, SCAN_ESC,       L"Esc", NULL, FALSE),
  SCN (4, SCAN_F1,        L"F1",  NULL, FALSE),
  SCN (4, SCAN_F2,        L"F2",  NULL, FALSE),
  SCN (4, SCAN_F3,        L"F3",  NULL, FALSE),
  SCN (4, SCAN_F4,        L"F4",  NULL, FALSE),
  SCN (4, SCAN_F5,        L"F5",  NULL, FALSE),
  SCN (4, SCAN_F6,        L"F6",  NULL, FALSE),
  SCN (4, SCAN_F7,        L"F7",  NULL, FALSE),
  SCN (4, SCAN_F8,        L"F8",  NULL, FALSE),
  SCN (4, SCAN_F9,        L"F9",  NULL, FALSE),
  SCN (4, SCAN_F10,       L"F10", NULL, FALSE),
  SCN (4, SCAN_F11,       L"F11", NULL, FALSE),
  SCN (4, SCAN_F12,       L"F12", NULL, FALSE),
  SCN (4, SCAN_DELETE,    L"Del", NULL, TRUE),
  ACT (4, OSK_ACT_HIDE,   L"Hide"),
  ROW_END
};

STATIC CONST OSK_KEY  mRow1[] = {
  CH (4, L'`', L'~'),
  CH (4, L'1', L'!'),
  CH (4, L'2', L'@'),
  CH (4, L'3', L'#'),
  CH (4, L'4', L'$'),
  CH (4, L'5', L'%'),
  CH (4, L'6', L'^'),
  CH (4, L'7', L'&'),
  CH (4, L'8', L'*'),
  CH (4, L'9', L'('),
  CH (4, L'0', L')'),
  CH (4, L'-', L'_'),
  CH (4, L'=', L'+'),
  CTL (8, CHAR_BACKSPACE, L"Bksp", TRUE),
  ROW_END
};

STATIC CONST OSK_KEY  mRow2[] = {
  CTL (6, CHAR_TAB, L"Tab", FALSE),
  CH (4, L'q', L'Q'),
  CH (4, L'w', L'W'),
  CH (4, L'e', L'E'),
  CH (4, L'r', L'R'),
  CH (4, L't', L'T'),
  CH (4, L'y', L'Y'),
  CH (4, L'u', L'U'),
  CH (4, L'i', L'I'),
  CH (4, L'o', L'O'),
  CH (4, L'p', L'P'),
  CH (4, L'[', L'{'),
  CH (4, L']', L'}'),
  CH (6, L'\\', L'|'),
  ROW_END
};

STATIC CONST OSK_KEY  mRow3[] = {
  ACT (7, OSK_ACT_CAPS, L"Caps"),
  CH (4, L'a', L'A'),
  CH (4, L's', L'S'),
  CH (4, L'd', L'D'),
  CH (4, L'f', L'F'),
  CH (4, L'g', L'G'),
  CH (4, L'h', L'H'),
  CH (4, L'j', L'J'),
  CH (4, L'k', L'K'),
  CH (4, L'l', L'L'),
  CH (4, L';', L':'),
  CH (4, L'\'', L'"'),
  CTL (9, CHAR_CARRIAGE_RETURN, L"Enter", FALSE),
  ROW_END
};

STATIC CONST OSK_KEY  mRow4[] = {
  ACT (8, OSK_ACT_SHIFT, L"Shift"),
  CH (4, L'z', L'Z'),
  CH (4, L'x', L'X'),
  CH (4, L'c', L'C'),
  CH (4, L'v', L'V'),
  CH (4, L'b', L'B'),
  CH (4, L'n', L'N'),
  CH (4, L'm', L'M'),
  CH (4, L',', L'<'),
  CH (4, L'.', L'>'),
  CH (4, L'/', L'?'),
  ACT (4, OSK_ACT_SHIFT, L"Shift"),
  SCN (4, SCAN_UP,       mArrowUp, L"Up", TRUE),
  SCN (4, SCAN_INSERT,   L"Ins", NULL, FALSE),
  ROW_END
};

STATIC CONST OSK_KEY  mRow5[] = {
  ACT (6, OSK_ACT_MOVE, L"Move"),
  SCN (5, SCAN_HOME,      L"Home", NULL, FALSE),
  SCN (5, SCAN_END,       L"End",  NULL, FALSE),
  CTL (24, L' ', L"", TRUE),
  SCN (4, SCAN_PAGE_UP,   L"PgUp", NULL, TRUE),
  SCN (4, SCAN_PAGE_DOWN, L"PgDn", NULL, TRUE),
  SCN (4, SCAN_LEFT,      mArrowLeft,  L"<", TRUE),
  SCN (4, SCAN_DOWN,      mArrowDown,  L"Dn", TRUE),
  SCN (4, SCAN_RIGHT,     mArrowRight, L">", TRUE),
  ROW_END
};

STATIC CONST OSK_KEY  *mRows[ROWS] = { mRow0, mRow1, mRow2, mRow3, mRow4, mRow5 };

OSK_PLACE  mOskPlace[OSK_MAX_KEYS];
UINTN      mOskPlaceCount;
BOOLEAN    mOskShift;
BOOLEAN    mOskCaps;
INTN       mOskPressed = -1;

STATIC UINT32  mGap;
STATIC UINT32  mLabelScale = 1;

//
// Colours, as EFI_GRAPHICS_OUTPUT_BLT_PIXEL: blue, green, red.
//
STATIC CONST OSK_PIXEL  mColorBack    = { 0x1c, 0x1a, 0x18, 0 };
STATIC CONST OSK_PIXEL  mColorKey     = { 0x50, 0x4a, 0x46, 0 };
STATIC CONST OSK_PIXEL  mColorSpecial = { 0x36, 0x32, 0x30, 0 };
STATIC CONST OSK_PIXEL  mColorPressed = { 0xd8, 0x98, 0x30, 0 };
STATIC CONST OSK_PIXEL  mColorLatched = { 0x90, 0x60, 0x20, 0 };
STATIC CONST OSK_PIXEL  mColorText    = { 0xf4, 0xf4, 0xf4, 0 };
STATIC CONST OSK_PIXEL  mColorBorder  = { 0x90, 0x90, 0x90, 0 };

//
// Glyphs, taken from HII once and kept as bit rows.
//
#define GLYPH_MAX_W  16
#define GLYPH_MAX_H  24
#define GLYPH_SLOTS  100

typedef struct {
  CHAR16    Char;
  UINT8     Width;
  UINT8     Height;
  UINT16    Rows[GLYPH_MAX_H];
} OSK_GLYPH;

STATIC OSK_GLYPH  mGlyph[GLYPH_SLOTS];
STATIC UINTN      mGlyphCount;
STATIC BOOLEAN    mGlyphsLoaded;

STATIC
VOID
OskLoadGlyph (
  IN EFI_HII_FONT_PROTOCOL  *Font,
  IN CHAR16                 Char
  )
{
  EFI_STATUS        Status;
  EFI_IMAGE_OUTPUT  *Image;
  OSK_GLYPH         *Glyph;
  OSK_PIXEL         Back;
  OSK_PIXEL         *Pixel;
  UINTN             X, Y;

  if (mGlyphCount >= GLYPH_SLOTS) {
    return;
  }

  Image  = NULL;
  Status = Font->GetGlyph (Font, Char, NULL, &Image, NULL);
  if ((Status == EFI_SUCCESS) && (Image != NULL) && (Image->Image.Bitmap != NULL) &&
      (Image->Width > 0) && (Image->Height > 0))
  {
    Glyph         = &mGlyph[mGlyphCount++];
    Glyph->Char   = Char;
    Glyph->Width  = (UINT8)MIN (Image->Width, GLYPH_MAX_W);
    Glyph->Height = (UINT8)MIN (Image->Height, GLYPH_MAX_H);
    ZeroMem (Glyph->Rows, sizeof (Glyph->Rows));

    //
    // The glyph comes in the system default colours, which are whatever the
    // text attribute is.  The top left pixel of every character used here is
    // background; anything else is ink.
    //
    Back = Image->Image.Bitmap[0];
    for (Y = 0; Y < Glyph->Height; Y++) {
      for (X = 0; X < Glyph->Width; X++) {
        Pixel = &Image->Image.Bitmap[Y * Image->Width + X];
        if ((Pixel->Red != Back.Red) || (Pixel->Green != Back.Green) || (Pixel->Blue != Back.Blue)) {
          Glyph->Rows[Y] |= (UINT16)(1 << X);
        }
      }
    }
  }

  if (Image != NULL) {
    if (Image->Image.Bitmap != NULL) {
      FreePool (Image->Image.Bitmap);
    }

    FreePool (Image);
  }
}

STATIC
VOID
OskLoadGlyphs (
  VOID
  )
{
  EFI_HII_FONT_PROTOCOL  *Font;
  CHAR16                 Char;

  if (mGlyphsLoaded) {
    return;
  }

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&Font))) {
    return;
  }

  mGlyphCount = 0;
  for (Char = 0x21; Char < 0x7f; Char++) {
    OskLoadGlyph (Font, Char);
  }

  OskLoadGlyph (Font, mArrowUp[0]);
  OskLoadGlyph (Font, mArrowDown[0]);
  OskLoadGlyph (Font, mArrowLeft[0]);
  OskLoadGlyph (Font, mArrowRight[0]);

  //
  // No font yet (GraphicsConsole registers it when it starts): try again
  // the next time the keyboard is drawn.
  //
  mGlyphsLoaded = (mGlyphCount >= 0x5e);
  DEBUG ((DEBUG_INFO, "OnScreenKeyboard: %u glyphs from HII\n", mGlyphCount));
}

STATIC
CONST OSK_GLYPH *
OskFindGlyph (
  IN CHAR16  Char
  )
{
  UINTN  Index;

  for (Index = 0; Index < mGlyphCount; Index++) {
    if (mGlyph[Index].Char == Char) {
      return &mGlyph[Index];
    }
  }

  return NULL;
}

BOOLEAN
OskRectIntersect (
  IN  CONST OSK_RECT  *A,
  IN  CONST OSK_RECT  *B,
  OUT OSK_RECT        *Out
  )
{
  INT32  X0, Y0, X1, Y1;

  X0 = MAX (A->X, B->X);
  Y0 = MAX (A->Y, B->Y);
  X1 = MIN (A->X + A->W, B->X + B->W);
  Y1 = MIN (A->Y + A->H, B->Y + B->H);
  if ((A->W <= 0) || (A->H <= 0) || (B->W <= 0) || (B->H <= 0) || (X1 <= X0) || (Y1 <= Y0)) {
    Out->X = Out->Y = Out->W = Out->H = 0;
    return FALSE;
  }

  Out->X = X0;
  Out->Y = Y0;
  Out->W = X1 - X0;
  Out->H = Y1 - Y0;
  return TRUE;
}

BOOLEAN
OskRectContains (
  IN CONST OSK_RECT  *R,
  IN INT32           X,
  IN INT32           Y
  )
{
  return (R->W > 0) && (R->H > 0) &&
         (X >= R->X) && (X < R->X + R->W) && (Y >= R->Y) && (Y < R->Y + R->H);
}

STATIC
VOID
OskFill (
  IN OSK_PIXEL        *Image,
  IN UINT32           Width,
  IN UINT32           Height,
  IN INT32            X,
  IN INT32            Y,
  IN INT32            W,
  IN INT32            H,
  IN CONST OSK_PIXEL  *Color
  )
{
  OSK_RECT  All = { 0, 0, (INT32)Width, (INT32)Height };
  OSK_RECT  R   = { X, Y, W, H };
  OSK_RECT  I;
  INT32     Row, Col;
  OSK_PIXEL *Line;

  if (!OskRectIntersect (&All, &R, &I)) {
    return;
  }

  for (Row = 0; Row < I.H; Row++) {
    Line = Image + (UINTN)(I.Y + Row) * Width + I.X;
    for (Col = 0; Col < I.W; Col++) {
      Line[Col] = *Color;
    }
  }
}

/**
  Width in pixels of a label at a scale; 0 if a character has no glyph.
**/
STATIC
UINT32
OskLabelWidth (
  IN CONST CHAR16  *Label,
  IN UINT32        Scale
  )
{
  CONST OSK_GLYPH  *Glyph;
  UINT32           Width = 0;

  for ( ; *Label != 0; Label++) {
    if (*Label == L' ') {
      Width += 8 * Scale;
      continue;
    }

    Glyph = OskFindGlyph (*Label);
    if (Glyph == NULL) {
      return 0;
    }

    Width += Glyph->Width * Scale;
  }

  return Width;
}

STATIC
VOID
OskDrawLabel (
  IN OSK_PIXEL       *Image,
  IN UINT32          Width,
  IN UINT32          Height,
  IN CONST OSK_RECT  *Face,
  IN CONST CHAR16    *Label
  )
{
  CONST OSK_GLYPH  *Glyph;
  UINT32           Scale;
  UINT32           TextW;
  INT32            X, Y;
  UINT32           Gx, Gy;

  Scale = mLabelScale;
  TextW = OskLabelWidth (Label, Scale);
  while ((Scale > 1) && (TextW + 4 > (UINT32)Face->W)) {
    Scale--;
    TextW = OskLabelWidth (Label, Scale);
  }

  if (TextW == 0) {
    return;
  }

  X = Face->X + (Face->W - (INT32)TextW) / 2;
  Y = Face->Y + (Face->H - 19 * (INT32)Scale) / 2;

  for ( ; *Label != 0; Label++) {
    if (*Label == L' ') {
      X += 8 * Scale;
      continue;
    }

    Glyph = OskFindGlyph (*Label);
    if (Glyph == NULL) {
      return;
    }

    for (Gy = 0; Gy < Glyph->Height; Gy++) {
      for (Gx = 0; Gx < Glyph->Width; Gx++) {
        if ((Glyph->Rows[Gy] & (1 << Gx)) != 0) {
          OskFill (
            Image,
            Width,
            Height,
            X + (INT32)(Gx * Scale),
            Y + (INT32)(Gy * Scale),
            (INT32)Scale,
            (INT32)Scale,
            &mColorText
            );
        }
      }
    }

    X += Glyph->Width * Scale;
  }
}

STATIC
BOOLEAN
OskIsLetter (
  IN CONST OSK_KEY  *Key
  )
{
  return (Key->Char >= L'a') && (Key->Char <= L'z');
}

VOID
OskLayout (
  IN  UINT32    ScreenW,
  IN  UINT32    ScreenH,
  OUT UINT32    *KeyboardH,
  OUT OSK_RECT  *Toggle,
  OUT UINT32    *CursorScale
  )
{
  UINT32         Quarter;
  UINT32         RowH;
  UINT32         Pad;
  UINT32         X0;
  UINTN          Row;
  UINT32         Start;
  CONST OSK_KEY  *Key;
  UINT32         ToggleH;
  UINT32         Margin;

  //
  // Keys as wide as the screen allows, and rows of at most a fifteenth of
  // the height: six rows then take 40% of it.  Landscape 1280x800 gives
  // keys of 84 x 53, portrait 800x1280 52 x 52, 1920x1080 128 x 72.
  //
  Quarter = MAX (ScreenW / ROW_UNITS, 4);
  RowH    = MIN (ScreenH / 15, 4 * Quarter);
  RowH    = MAX (RowH, 20);
  Pad     = MAX (RowH / 8, 2);
  X0      = (ScreenW > ROW_UNITS * Quarter) ? (ScreenW - ROW_UNITS * Quarter) / 2 : 0;
  mGap    = MAX (Quarter / 6, 1);

  mLabelScale = MAX ((RowH * 2 / 5) / 19, 1);

  mOskPlaceCount = 0;
  for (Row = 0; Row < ROWS; Row++) {
    Start = 0;
    for (Key = mRows[Row]; Key->Quarters != 0; Key++) {
      if (mOskPlaceCount < OSK_MAX_KEYS) {
        mOskPlace[mOskPlaceCount].Key    = Key;
        mOskPlace[mOskPlaceCount].Rect.X = (INT32)(X0 + Start * Quarter);
        mOskPlace[mOskPlaceCount].Rect.Y = (INT32)(Pad + Row * RowH);
        mOskPlace[mOskPlaceCount].Rect.W = (INT32)(Key->Quarters * Quarter);
        mOskPlace[mOskPlaceCount].Rect.H = (INT32)RowH;
        mOskPlaceCount++;
      }

      Start += Key->Quarters;
    }

    ASSERT (Start == ROW_UNITS);
  }

  *KeyboardH = 6 * RowH + 2 * Pad;

  //
  // The toggle: a small key in the top right corner, kept clear of the
  // corner itself, where the touch test puts a marker to be touched.
  //
  ToggleH   = MAX (RowH * 4 / 5, 24);
  Margin    = MAX (ScreenW / 32, 40);
  Toggle->W = (INT32)(ToggleH * 3 / 2);
  Toggle->H = (INT32)ToggleH;
  Toggle->X = (INT32)ScreenW - Toggle->W - (INT32)Margin;
  Toggle->Y = 2;
  if (Toggle->X < 0) {
    Toggle->X = 0;
  }

  *CursorScale = MAX (ScreenH / 900, 1);
}

INTN
OskHitKey (
  IN INT32  X,
  IN INT32  Y
  )
{
  UINTN  Index;

  for (Index = 0; Index < mOskPlaceCount; Index++) {
    if (OskRectContains (&mOskPlace[Index].Rect, X, Y)) {
      return (INTN)Index;
    }
  }

  return -1;
}

VOID
OskRenderKey (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height,
  IN UINTN      Index
  )
{
  CONST OSK_KEY    *Key;
  OSK_RECT         Face;
  CONST OSK_PIXEL  *Color;
  CHAR16           Text[2];
  CONST CHAR16     *Label;
  BOOLEAN          Shifted;

  if (Index >= mOskPlaceCount) {
    return;
  }

  Key = mOskPlace[Index].Key;

  // the cell, background first, then the face inside it
  OskFill (
    Image,
    Width,
    Height,
    mOskPlace[Index].Rect.X,
    mOskPlace[Index].Rect.Y,
    mOskPlace[Index].Rect.W,
    mOskPlace[Index].Rect.H,
    &mColorBack
    );

  Face.X = mOskPlace[Index].Rect.X + (INT32)mGap;
  Face.Y = mOskPlace[Index].Rect.Y + (INT32)mGap;
  Face.W = mOskPlace[Index].Rect.W - 2 * (INT32)mGap;
  Face.H = mOskPlace[Index].Rect.H - 2 * (INT32)mGap;

  if ((INTN)Index == mOskPressed) {
    Color = &mColorPressed;
  } else if (((Key->Action == OSK_ACT_SHIFT) && mOskShift) ||
             ((Key->Action == OSK_ACT_CAPS) && mOskCaps))
  {
    Color = &mColorLatched;
  } else if ((Key->Action != OSK_ACT_KEY) || (Key->Label != NULL)) {
    Color = &mColorSpecial;
  } else {
    Color = &mColorKey;
  }

  OskFill (Image, Width, Height, Face.X, Face.Y, Face.W, Face.H, Color);

  if (Key->Label != NULL) {
    Label = Key->Label;
    if ((OskLabelWidth (Label, 1) == 0) && (Key->AltLabel != NULL)) {
      Label = Key->AltLabel;
    }
  } else {
    Shifted = mOskShift;
    if (OskIsLetter (Key)) {
      Shifted = (BOOLEAN)(Shifted != mOskCaps);
    }

    Text[0] = (Shifted && (Key->ShiftChar != 0)) ? Key->ShiftChar : Key->Char;
    Text[1] = 0;
    Label   = Text;
  }

  OskDrawLabel (Image, Width, Height, &Face, Label);
}

VOID
OskRenderKeyboard (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height
  )
{
  UINTN  Index;

  OskLoadGlyphs ();

  OskFill (Image, Width, Height, 0, 0, (INT32)Width, (INT32)Height, &mColorBack);
  // a line along the edge that faces the rest of the picture, both edges
  OskFill (Image, Width, Height, 0, 0, (INT32)Width, 1, &mColorBorder);
  OskFill (Image, Width, Height, 0, (INT32)Height - 1, (INT32)Width, 1, &mColorBorder);

  for (Index = 0; Index < mOskPlaceCount; Index++) {
    OskRenderKey (Image, Width, Height, Index);
  }
}

/**
  The toggle: a little keyboard drawn in outline.
**/
VOID
OskRenderToggle (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height
  )
{
  INT32  W = (INT32)Width;
  INT32  H = (INT32)Height;
  INT32  Line;
  INT32  Ix, Iy, Iw, Ih;
  INT32  CellW, CellH;
  INT32  Row, Col;

  OskFill (Image, Width, Height, 0, 0, W, H, &mColorSpecial);
  OskFill (Image, Width, Height, 0, 0, W, 1, &mColorBorder);
  OskFill (Image, Width, Height, 0, H - 1, W, 1, &mColorBorder);
  OskFill (Image, Width, Height, 0, 0, 1, H, &mColorBorder);
  OskFill (Image, Width, Height, W - 1, 0, 1, H, &mColorBorder);

  Line = MAX (H / 20, 1);
  Iw   = W * 3 / 4;
  Ih   = H * 3 / 5;
  Ix   = (W - Iw) / 2;
  Iy   = (H - Ih) / 2;

  // outline
  OskFill (Image, Width, Height, Ix, Iy, Iw, Line, &mColorText);
  OskFill (Image, Width, Height, Ix, Iy + Ih - Line, Iw, Line, &mColorText);
  OskFill (Image, Width, Height, Ix, Iy, Line, Ih, &mColorText);
  OskFill (Image, Width, Height, Ix + Iw - Line, Iy, Line, Ih, &mColorText);

  // two rows of keys and a space bar
  CellW = (Iw - 2 * Line) / 8;
  CellH = (Ih - 2 * Line) / 4;
  if ((CellW < 2) || (CellH < 2)) {
    return;
  }

  for (Row = 0; Row < 2; Row++) {
    for (Col = 0; Col < 7; Col++) {
      OskFill (
        Image,
        Width,
        Height,
        Ix + Line + CellW / 2 + Col * CellW + 1,
        Iy + Line + CellH / 2 + Row * CellH + 1,
        CellW - 2,
        CellH - 2,
        &mColorText
        );
    }
  }

  OskFill (
    Image,
    Width,
    Height,
    Ix + Line + 2 * CellW + 1,
    Iy + Line + CellH / 2 + 2 * CellH + 1,
    4 * CellW - 2,
    CellH - 2,
    &mColorText
    );
}

//
// The mouse cursor: X outline, o fill, . transparent.
//
STATIC CONST CHAR8  *mCursor[OSK_CURSOR_H] = {
  "X...........",
  "XX..........",
  "XoX.........",
  "XooX........",
  "XoooX.......",
  "XooooX......",
  "XoooooX.....",
  "XooooooX....",
  "XoooooooX...",
  "XooooooooX..",
  "XoooooooooX.",
  "XooooooXXXXX",
  "XoooXooX....",
  "XooXXooX....",
  "XoX..XooX...",
  "XX...XooX...",
  "X.....XooX..",
  "......XooX..",
  ".......XX..."
};

UINT8
OskCursorPixel (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Scale
  )
{
  CHAR8  C;

  X /= Scale;
  Y /= Scale;
  if ((X >= OSK_CURSOR_W) || (Y >= OSK_CURSOR_H)) {
    return 0;
  }

  C = mCursor[Y][X];
  return (C == 'X') ? 1 : (C == 'o') ? 2 : 0;
}
