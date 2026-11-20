/** @file
 *
 *  OnScreenKeyboardDxe: what the three source files share.
 *
 *    OnScreenKeyboardDxe.c  pointers in, keys out, the open/close logic
 *    Keyboard.c             key layout, drawing of keys, toggle and cursor
 *    Overlay.c              putting all of that on the screen through GOP
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ON_SCREEN_KEYBOARD_H_
#define ON_SCREEN_KEYBOARD_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>

typedef EFI_GRAPHICS_OUTPUT_BLT_PIXEL OSK_PIXEL;

typedef struct {
  INT32    X;
  INT32    Y;
  INT32    W;     // 0 or less: empty
  INT32    H;
} OSK_RECT;

//
// What is shown at the top layer, under the mouse cursor: nothing, the
// small key that opens the keyboard, or the keyboard.  Never both of the
// last two.
//
#define OSK_PANEL_NONE      0
#define OSK_PANEL_TOGGLE    1
#define OSK_PANEL_KEYBOARD  2

#define OSK_ACT_KEY    0
#define OSK_ACT_SHIFT  1
#define OSK_ACT_CAPS   2
#define OSK_ACT_HIDE   3
#define OSK_ACT_MOVE   4
#define OSK_ACT_FLOAT  5      // docked full width <-> floating
#define OSK_ACT_ALPHA  6      // next opacity level

//
// Opacity levels, out of 256.  Labels are always drawn opaque.
//
#define OSK_ALPHA_LEVELS  4

//
// Pixels of the keyboard image carry a flag in Reserved: OSK_OPAQUE is
// drawn as it is at every opacity (labels and their outline, the pressed
// key, the grip), 0 is blended with the picture underneath.  The flag
// never reaches the screen.
//
#define OSK_OPAQUE  0xFF

typedef struct {
  UINT8           Quarters;     // width, in quarters of a letter key
  UINT8           Action;       // OSK_ACT_*
  BOOLEAN         Repeats;      // repeats while held
  UINT16          ScanCode;
  CHAR16          Char;
  CHAR16          ShiftChar;
  CONST CHAR16    *Label;       // NULL: the character itself
  CONST CHAR16    *AltLabel;    // used when the font lacks a glyph of Label
} OSK_KEY;

#define OSK_MAX_KEYS  96

typedef struct {
  CONST OSK_KEY    *Key;
  OSK_RECT         Rect;        // in the keyboard image
} OSK_PLACE;

#define OSK_CURSOR_W  12
#define OSK_CURSOR_H  19

//
// Keyboard.c
//
extern OSK_PLACE  mOskPlace[OSK_MAX_KEYS];
extern UINTN      mOskPlaceCount;
extern BOOLEAN    mOskShift;
extern BOOLEAN    mOskCaps;
extern INTN       mOskPressed;      // index into mOskPlace, or -1
extern BOOLEAN    mOskFloating;     // as laid out now
extern UINTN      mOskAlphaLevel;   // 0 .. OSK_ALPHA_LEVELS - 1

UINT32
OskAlpha (
  VOID
  );

/**
  Where the keys go.  Docked: as wide as the screen.  Floating: narrower,
  with a handle row on top to drag it by.
**/
VOID
OskLayout (
  IN  UINT32    ScreenW,
  IN  UINT32    ScreenH,
  IN  BOOLEAN   Floating,
  OUT UINT32    *KeyboardW,
  OUT UINT32    *KeyboardH,
  OUT OSK_RECT  *Toggle,
  OUT UINT32    *CursorScale
  );

/**
  Whether a point of the keyboard image is on the handle (floating only).
**/
BOOLEAN
OskHitHandle (
  IN INT32  X,
  IN INT32  Y
  );

VOID
OskRenderKeyboard (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height
  );

VOID
OskRenderKey (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height,
  IN UINTN      Index
  );

VOID
OskRenderToggle (
  IN OSK_PIXEL  *Image,
  IN UINT32     Width,
  IN UINT32     Height
  );

/**
  0 transparent, 1 outline, 2 fill.
**/
UINT8
OskCursorPixel (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Scale
  );

INTN
OskHitKey (
  IN INT32  X,
  IN INT32  Y
  );

BOOLEAN
OskRectIntersect (
  IN  CONST OSK_RECT  *A,
  IN  CONST OSK_RECT  *B,
  OUT OSK_RECT        *Out
  );

BOOLEAN
OskRectContains (
  IN CONST OSK_RECT  *R,
  IN INT32           X,
  IN INT32           Y
  );

//
// Overlay.c
//
// What OnScreenKeyboardDxe.c wants on the screen.  OskOverlaySync() makes
// the screen match.
//
extern UINT8    mOskWantPanel;
extern BOOLEAN  mOskWantTop;
extern BOOLEAN  mOskWantFloating;
extern UINT32   mOskFloatX;         // floating position, 16.16 fraction of the free space
extern UINT32   mOskFloatY;
extern BOOLEAN  mOskWantCursor;
extern INT32    mOskCursorX;
extern INT32    mOskCursorY;
extern UINTN    mOskDirect;         // depth of images that may write the framebuffer themselves
extern BOOLEAN  mOskGone;           // ExitBootServices has happened

extern UINT32   mOskScreenW;
extern UINT32   mOskScreenH;
extern UINT32   mOskKeyboardW;
extern UINT32   mOskKeyboardH;

BOOLEAN
OskOverlayPrepare (
  VOID
  );

VOID
OskOverlaySync (
  VOID
  );

/**
  What the pointer is on, as shown now.  ImageX/ImageY: the point in the
  keyboard image when the keyboard is hit.
**/
UINT8
OskOverlayHit (
  IN  INT32  X,
  IN  INT32  Y,
  OUT INT32  *ImageX,
  OUT INT32  *ImageY
  );

/**
  The keyboard image changed: all of it (Index -1) or one key.  Re-render
  and put it on the screen.
**/
VOID
OskOverlayKeyboardChanged (
  IN INTN  Index
  );

/**
  The panel as it is on the screen; FALSE when it is not the keyboard.
**/
BOOLEAN
OskOverlayKeyboardRect (
  OUT OSK_RECT  *Rect
  );

/**
  The opacity changed: draw the keyboard again over the picture.
**/
VOID
OskOverlayAlphaChanged (
  VOID
  );

VOID
OskOverlayPeriodic (
  VOID
  );

VOID
OskOverlayDetach (
  IN BOOLEAN  Restore
  );

#endif
