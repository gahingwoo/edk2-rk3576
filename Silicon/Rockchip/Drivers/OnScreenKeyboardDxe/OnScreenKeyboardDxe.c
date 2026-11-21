/** @file
 *
 *  An on-screen keyboard, for a board with a touch screen or a mouse and
 *  no keyboard.
 *
 *  It is a console input device: what is typed on it arrives through its
 *  own Simple Text Input (Ex), which the console splitter merges into
 *  ConIn with every other keyboard.  So setup's text fields and passwords,
 *  the UEFI Shell, GRUB and the Windows boot manager all get the keys.
 *
 *  Opening and closing: while there is at least one pointer device, a small
 *  key with a keyboard drawn on it sits near the top right corner of the
 *  picture.  A tap or a click on it opens the keyboard; Hide on the
 *  keyboard closes it.  With no pointer device nothing is drawn at all.
 *
 *  Where the input comes from:
 *
 *    mice     every Simple Pointer (UsbMouseDxe) and every Absolute Pointer
 *             (UsbMouseAbsolutePointerDxe) with a device path, read here
 *             on a timer; devices that come later (USB) are picked up
 *             through protocol notifications.  A mouse gets a cursor.
 *             Outside the keyboard a mouse also stands in for the menu keys:
 *             the wheel is Up and Down, a left click Enter, a right click
 *             Esc.  While the setup browser waits for input (patch 0013),
 *             a left click goes to it instead, as the text cell under the
 *             pointer, and it picks the statement there.  A middle click
 *             (patches 0014 and 0015) picks it and selects it at once; in
 *             the Shell or GRUB a middle click is Enter, like a left one.
 *
 *    touch    a touch screen driver that already turns touches into keys
 *             (TouchKeysDxe on the TaishanPi 3M) names its pointer through
 *             ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL and hands every report
 *             over.  A touch that starts on the toggle or on the keyboard is
 *             the keyboard's, from start to end; any other touch is the
 *             driver's, as a gesture.  No cursor for touch.
 *
 *  A key is pressed when the finger or the button comes up, on the key it
 *  came up on (sliding off a key cancels it).  Backspace, Delete, Space and
 *  the arrows repeat while held.  Shift applies to the next key; Caps
 *  stays.
 *
 *  The controls are icons at the right end of a bar along the top of the
 *  keyboard, always in the same place: float or dock, opacity, move and
 *  hide.
 *
 *    float/dock  docked is as wide as the screen, at the bottom or the top
 *                edge; floating is about two thirds as wide, and the rest
 *                of the bar is a handle to drag it anywhere by.  The mode
 *                and the place are kept for the rest of the boot, the place
 *                as a share of the room there is, so it survives a mode or
 *                rotation change.
 *    opacity     steps through 100, 80, 60 and 40 percent; the icon is a
 *                square filled to the level.  A button rather than a
 *                slider: one tap or click, no aim, and it cannot be dragged
 *                by mistake while typing.
 *    move        docked: the other edge; floating: the other half.
 *    hide        closes it.
 *    camera      a screenshot (ScreenshotDxe), without the keyboard in it;
 *                only shown when ScreenshotDxe is in the image.
 *
 *  Whenever ScreenshotDxe takes a screenshot, by the camera or by its hot
 *  key, the keyboard, its toggle and the mouse cursor come off the screen
 *  first and come back afterwards.
 *
 *  Docked, the keyboard starts at the bottom and moves to the other edge by
 *  itself when the console's text cursor would end up under it.  Once the
 *  move icon has been used, the edge chosen there stays, for the rest of
 *  the boot: the keyboard does not move by itself any more.
 *
 *  Everything stops at ExitBootServices.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/RockchipOnScreenKeyboard.h>
#include <Protocol/RockchipScreenshot.h>
#include <Protocol/SimplePointer.h>

#include "OnScreenKeyboard.h"

#define POLL_100NS          (20 * 10000)  // 20 ms
#define REPEAT_DELAY_POLLS  25            // 0.5 s
#define REPEAT_EVERY_POLLS  4             // then 12 a second
#define REDRAW_POLLS        25            // layers again, while an application owns the framebuffer
#define FOLLOW_POLLS        10            // look where the text cursor is
#define KEY_QUEUE_SIZE      32
#define MAX_NOTIFY          16
#define MAX_MICE            8
#define MAX_TOUCH_SCREENS   2
#define MOUSE_DIVISOR       1200          // counts x screen width / this = pixels

#define ON_SCREEN_KEYBOARD_DEVICE_GUID \
  { 0xc78761aa, 0x3ac6, 0x4918, { 0x99, 0x3e, 0x58, 0x39, 0x96, 0x57, 0x51, 0x48 } }

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} OSK_DEVICE_PATH;

typedef struct {
  BOOLEAN                    Used;
  EFI_KEY_DATA               Key;
  EFI_KEY_NOTIFY_FUNCTION    Function;
} OSK_NOTIFY;

typedef struct {
  EFI_HANDLE    Handle;
  VOID          *Interface;
  BOOLEAN       Absolute;
  BOOLEAN       Left;
  BOOLEAN       Right;
  BOOLEAN       Middle;
  USB_MOUSE_BUTTONS_PROTOCOL  *Buttons;    // the middle button of a Simple Pointer mouse, if UsbMouseDxe has it
  BOOLEAN       HaveZ;
  UINT64        LastZ;
  INT32         AccX;
  INT32         AccY;
} OSK_MOUSE;

#define TARGET_NONE    0
#define TARGET_TOGGLE  1
#define TARGET_KEY     2
#define TARGET_HANDLE  3

STATIC OSK_DEVICE_PATH  mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    ON_SCREEN_KEYBOARD_DEVICE_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_HANDLE       mHandle;
STATIC EFI_EVENT        mWaitEvent;
STATIC EFI_EVENT        mPollEvent;
STATIC BOOLEAN          mPolling;
STATIC UINTN            mTicks;
STATIC EFI_IMAGE_START  mOriginalStartImage;

STATIC EFI_INPUT_KEY  mQueue[KEY_QUEUE_SIZE];
STATIC UINTN          mQueueHead;
STATIC UINTN          mQueueCount;
STATIC OSK_NOTIFY     mNotify[MAX_NOTIFY];

STATIC OSK_MOUSE                      mMice[MAX_MICE];
STATIC UINTN                          mMouseCount;
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  *mTouchScreen[MAX_TOUCH_SCREENS];
STATIC UINTN                          mTouchScreenCount;
STATIC BOOLEAN                        mRescan;

STATIC BOOLEAN  mOpen;

// the press under way, by touch or by the left button
STATIC BOOLEAN  mPressActive;
STATIC UINT8    mPressTarget;
STATIC BOOLEAN  mPressInside;      // still on the toggle
STATIC UINTN    mPressPolls;
STATIC INT32    mDragFromX;        // a drag by the handle: where the pointer
STATIC INT32    mDragFromY;        // went down, and where the keyboard was
STATIC OSK_RECT mDragStart;

// touch, as reported by the touch screen driver
STATIC BOOLEAN  mTouchDown;
STATIC BOOLEAN  mTouchOwned;

// text cursor following
STATIC INT32  mLastCursorRow = -1;

STATIC BOOLEAN  mCursorPlaced;
STATIC BOOLEAN  mUserEdge;         // the docked edge was chosen with the move icon

//
// The size chosen with the size icon, kept across boots.  Under the
// keyboard's own device GUID; not part of any setup form.
//
#define OSK_SIZE_VARIABLE  L"OnScreenKeyboardSize"
STATIC EFI_GUID  mOskVariableGuid = ON_SCREEN_KEYBOARD_DEVICE_GUID;

STATIC EFI_GUID  mScreenshotGuid      = ROCKCHIP_SCREENSHOT_PROTOCOL_GUID;
STATIC EFI_GUID  mScreenshotStartGuid = ROCKCHIP_SCREENSHOT_START_GROUP_GUID;
STATIC EFI_GUID  mScreenshotEndGuid   = ROCKCHIP_SCREENSHOT_END_GROUP_GUID;

// clicks for the setup browser
STATIC EFI_EVENT  mClickEvent;
STATIC BOOLEAN    mClickReading;
STATIC BOOLEAN    mClickPending;
STATIC BOOLEAN    mClickPendingActivates;   // the click waiting is a middle click
STATIC BOOLEAN    mClickActivates;          // the click taken last was; asked once
STATIC UINTN      mClickColumn;
STATIC UINTN      mClickRow;

STATIC
VOID
OskStartPolling (
  VOID
  );

//
// Keys out
//

STATIC
VOID
OskQueueKey (
  IN UINT16  ScanCode,
  IN CHAR16  UnicodeChar
  )
{
  EFI_KEY_DATA  KeyData;
  UINTN         Index;

  if (mQueueCount < KEY_QUEUE_SIZE) {
    Index                     = (mQueueHead + mQueueCount) % KEY_QUEUE_SIZE;
    mQueue[Index].ScanCode    = ScanCode;
    mQueue[Index].UnicodeChar = UnicodeChar;
    mQueueCount++;
  }

  ZeroMem (&KeyData, sizeof (KeyData));
  KeyData.Key.ScanCode    = ScanCode;
  KeyData.Key.UnicodeChar = UnicodeChar;

  for (Index = 0; Index < MAX_NOTIFY; Index++) {
    if (mNotify[Index].Used &&
        (mNotify[Index].Key.Key.ScanCode == ScanCode) &&
        (mNotify[Index].Key.Key.UnicodeChar == UnicodeChar) &&
        ((mNotify[Index].Key.KeyState.KeyShiftState & ~EFI_SHIFT_STATE_VALID) == 0))
    {
      mNotify[Index].Function (&KeyData);
    }
  }

  gBS->SignalEvent (mWaitEvent);
}

STATIC
EFI_STATUS
EFIAPI
OskReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  mQueueHead  = 0;
  mQueueCount = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
OskReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  EFI_TPL  Tpl;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mQueueCount == 0) {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }

  *Key       = mQueue[mQueueHead];
  mQueueHead = (mQueueHead + 1) % KEY_QUEUE_SIZE;
  mQueueCount--;
  gBS->RestoreTPL (Tpl);

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
OskWaitForKey (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mQueueCount > 0) {
    gBS->SignalEvent (Event);
  }
}

STATIC
EFI_STATUS
EFIAPI
OskResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  return OskReset (NULL, ExtendedVerification);
}

STATIC
EFI_STATUS
EFIAPI
OskReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (KeyData, sizeof (*KeyData));
  return OskReadKeyStroke (NULL, &KeyData->Key);
}

STATIC
EFI_STATUS
EFIAPI
OskSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
OskRegisterKeyNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  UINTN  Index;

  if ((KeyData == NULL) || (KeyNotificationFunction == NULL) || (NotifyHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < MAX_NOTIFY; Index++) {
    if (!mNotify[Index].Used) {
      mNotify[Index].Used     = TRUE;
      mNotify[Index].Key      = *KeyData;
      mNotify[Index].Function = KeyNotificationFunction;
      *NotifyHandle           = &mNotify[Index];
      return EFI_SUCCESS;
    }
  }

  return EFI_OUT_OF_RESOURCES;
}

STATIC
EFI_STATUS
EFIAPI
OskUnregisterKeyNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  UINTN  Index;

  for (Index = 0; Index < MAX_NOTIFY; Index++) {
    if (NotificationHandle == &mNotify[Index]) {
      mNotify[Index].Used = FALSE;
      return EFI_SUCCESS;
    }
  }

  return EFI_INVALID_PARAMETER;
}

STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL  mTextIn = {
  OskReset,
  OskReadKeyStroke,
  NULL
};

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mTextInEx = {
  OskResetEx,
  OskReadKeyStrokeEx,
  NULL,
  OskSetState,
  OskRegisterKeyNotify,
  OskUnregisterKeyNotify
};

//
// What is shown
//

STATIC
BOOLEAN
OskHavePointer (
  VOID
  )
{
  return (mMouseCount + mTouchScreenCount) > 0;
}

STATIC
VOID
OskSync (
  VOID
  )
{
  if (!OskHavePointer ()) {
    mOpen          = FALSE;
    mOskWantPanel  = OSK_PANEL_NONE;
    mOskWantCursor = FALSE;
  } else {
    mOskWantPanel = mOpen ? OSK_PANEL_KEYBOARD : OSK_PANEL_TOGGLE;
  }

  if (mMouseCount == 0) {
    mOskWantCursor = FALSE;
  }

  OskOverlaySync ();
}

/**
  The row of the console's text cursor, in pixels, if it is shown and the
  console is the graphics one; -1 otherwise.  GraphicsConsole centres its
  text in the picture, 19 pixels a line.
**/
STATIC
INT32
OskTextCursorY (
  VOID
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  UINTN                            Columns, Rows;

  Out = gST->ConOut;
  if ((Out == NULL) || (Out->Mode == NULL) || !Out->Mode->CursorVisible) {
    return -1;
  }

  if (EFI_ERROR (Out->QueryMode (Out, (UINTN)Out->Mode->Mode, &Columns, &Rows)) ||
      (Rows == 0) || (Rows * 19 > mOskScreenH))
  {
    return -1;
  }

  return (INT32)((mOskScreenH - Rows * 19) / 2 + (UINTN)Out->Mode->CursorRow * 19);
}

STATIC
BOOLEAN
OskCovers (
  IN BOOLEAN  Top,
  IN INT32    Y
  )
{
  INT32  KbdY = Top ? 0 : (INT32)(mOskScreenH - mOskKeyboardH);

  return (Y + 19 > KbdY) && (Y < KbdY + (INT32)mOskKeyboardH);
}

/**
  Keep the text cursor in sight: when it moves under the keyboard, and the
  keyboard at the other edge would not cover it, move the keyboard there.
  Only when the cursor has moved, so that Move is not undone.
**/
STATIC
VOID
OskFollowTextCursor (
  IN BOOLEAN  Opening
  )
{
  INT32  Y;

  // a floating keyboard stays where it was put, and so does a docked one
  // whose edge was chosen
  if (!mOpen || mOskWantFloating || mUserEdge || (mOskDirect != 0) || (mOskKeyboardH == 0)) {
    return;
  }

  Y = OskTextCursorY ();
  if ((Y < 0) || (!Opening && (Y == mLastCursorRow))) {
    mLastCursorRow = Y;
    return;
  }

  mLastCursorRow = Y;
  if (OskCovers (mOskWantTop, Y) && !OskCovers (!mOskWantTop, Y)) {
    mOskWantTop = !mOskWantTop;
  }
}

STATIC
VOID
OskSetOpen (
  IN BOOLEAN  Open
  )
{
  mOpen       = Open;
  mOskShift   = FALSE;
  mOskPressed = -1;
  if (Open) {
    //
    // The keys as they are now (the font may have arrived since the
    // keyboard was last drawn), at the bottom unless the text cursor is
    // there.
    //
    if (!mUserEdge) {
      mOskWantTop = FALSE;
    }

    if (OskOverlayPrepare ()) {
      OskOverlayKeyboardChanged (-1);
      OskFollowTextCursor (TRUE);
    }
  }

  OskSync ();
}

STATIC
VOID
OskSetPressed (
  IN INTN  Index
  )
{
  INTN  Old;

  if (Index == mOskPressed) {
    return;
  }

  Old         = mOskPressed;
  mOskPressed = Index;
  if (Old >= 0) {
    OskOverlayKeyboardChanged (Old);
  }

  if (Index >= 0) {
    OskOverlayKeyboardChanged (Index);
  }
}

STATIC
VOID
OskActivate (
  IN INTN  Index
  )
{
  CONST OSK_KEY                 *Key;
  CHAR16                        Char;
  BOOLEAN                       Shifted;
  ROCKCHIP_SCREENSHOT_PROTOCOL  *Screenshot;

  if ((Index < 0) || ((UINTN)Index >= mOskPlaceCount)) {
    return;
  }

  Key = mOskPlace[Index].Key;
  switch (Key->Action) {
    case OSK_ACT_KEY:
      Char = Key->Char;
      if ((Char != 0) && (Key->ShiftChar != 0)) {
        Shifted = mOskShift;
        if ((Char >= L'a') && (Char <= L'z')) {
          Shifted = (BOOLEAN)(Shifted != mOskCaps);
        }

        if (Shifted) {
          Char = Key->ShiftChar;
        }
      }

      OskQueueKey (Key->ScanCode, Char);
      if (mOskShift) {
        mOskShift = FALSE;
        OskOverlayKeyboardChanged (-1);
      }

      break;

    case OSK_ACT_SHIFT:
      mOskShift = (BOOLEAN)!mOskShift;
      OskOverlayKeyboardChanged (-1);
      break;

    case OSK_ACT_CAPS:
      mOskCaps = (BOOLEAN)!mOskCaps;
      OskOverlayKeyboardChanged (-1);
      break;

    case OSK_ACT_HIDE:
      OskSetOpen (FALSE);
      break;

    case OSK_ACT_MOVE:
      // docked: the other edge; floating: the other half, up or down
      if (mOskWantFloating) {
        mOskFloatY = 0x10000 - mOskFloatY;
      } else {
        mOskWantTop = (BOOLEAN)!mOskWantTop;
        mUserEdge   = TRUE;
      }

      OskSync ();
      break;

    case OSK_ACT_FLOAT:
      mOskWantFloating = (BOOLEAN)!mOskWantFloating;
      mOskPressed      = -1;
      OskSync ();
      break;

    case OSK_ACT_ALPHA:
      mOskAlphaLevel = (mOskAlphaLevel + 1) % OSK_ALPHA_LEVELS;
      OskOverlayAlphaChanged ();
      break;

    case OSK_ACT_SIZE:
      //
      // Small, Medium, Large and round again, from the size in use (the
      // default one included).  Kept in a variable, so it is the size
      // next boot as well.
      //
      mOskWantSize = (UINT8)((mOskSize + 1) % OSK_SIZES);
      mOskPressed  = -1;
      gRT->SetVariable (
             OSK_SIZE_VARIABLE,
             &mOskVariableGuid,
             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
             sizeof (mOskWantSize),
             &mOskWantSize
             );
      OskSync ();
      break;

    case OSK_ACT_SHOT:
      //
      // Taken once this TPL is left; the keyboard goes off the screen for
      // it on its own (OskScreenshotStart).
      //
      if (EFI_ERROR (gBS->LocateProtocol (&mScreenshotGuid, NULL, (VOID **)&Screenshot)) ||
          EFI_ERROR (Screenshot->Take (Screenshot)))
      {
        DEBUG ((DEBUG_ERROR, "OnScreenKeyboard: no screenshot driver\n"));
      }

      break;

    default:
      break;
  }
}

//
// A press, by touch or by the left button, in picture coordinates.
//

STATIC
INTN
OskKeyAt (
  IN INT32  X,
  IN INT32  Y
  )
{
  INT32  Ix, Iy;

  if (OskOverlayHit (X, Y, &Ix, &Iy) != OSK_PANEL_KEYBOARD) {
    return -1;
  }

  return OskHitKey (Ix, Iy);
}

/**
  @retval TRUE  the press is ours
**/
STATIC
BOOLEAN
OskPress (
  IN INT32  X,
  IN INT32  Y
  )
{
  INT32  Ix, Iy;
  UINT8  Hit;

  mPressActive = FALSE;
  OskSetPressed (-1);
  if (!OskOverlayPrepare ()) {
    return FALSE;
  }

  Hit = OskOverlayHit (X, Y, &Ix, &Iy);
  if (Hit == OSK_PANEL_NONE) {
    return FALSE;
  }

  mPressActive = TRUE;
  mPressPolls  = 0;
  mPressInside = TRUE;
  if (Hit == OSK_PANEL_TOGGLE) {
    mPressTarget = TARGET_TOGGLE;
  } else if (OskHitHandle (Ix, Iy) && OskOverlayKeyboardRect (&mDragStart)) {
    mPressTarget = TARGET_HANDLE;
    mDragFromX   = X;
    mDragFromY   = Y;
  } else {
    mPressTarget = TARGET_KEY;
    OskSetPressed (OskHitKey (Ix, Iy));
  }

  return TRUE;
}

/**
  Drag the floating keyboard: the pointer moved to X, Y.  Kept wholly on
  the screen, and kept as a fraction of the room there is, so that it
  stays in the same part of the picture when the mode or the rotation
  changes.
**/
STATIC
VOID
OskDragTo (
  IN INT32  X,
  IN INT32  Y
  )
{
  INT32  Nx, Ny;
  INT32  RoomX, RoomY;

  RoomX = (INT32)mOskScreenW - mDragStart.W;
  RoomY = (INT32)mOskScreenH - mDragStart.H;
  Nx    = MAX (0, MIN (mDragStart.X + X - mDragFromX, RoomX));
  Ny    = MAX (0, MIN (mDragStart.Y + Y - mDragFromY, RoomY));

  mOskFloatX = (RoomX > 0) ? (UINT32)(((UINT64)Nx << 16) / (UINT32)RoomX) : 0x8000;
  mOskFloatY = (RoomY > 0) ? (UINT32)(((UINT64)Ny << 16) / (UINT32)RoomY) : 0x8000;
  OskSync ();
}

STATIC
VOID
OskPressMove (
  IN INT32  X,
  IN INT32  Y
  )
{
  INT32  Ix, Iy;
  INTN   Key;

  if (!mPressActive) {
    return;
  }

  if (mPressTarget == TARGET_TOGGLE) {
    mPressInside = (BOOLEAN)(OskOverlayHit (X, Y, &Ix, &Iy) == OSK_PANEL_TOGGLE);
  } else if (mPressTarget == TARGET_HANDLE) {
    OskDragTo (X, Y);
  } else if (mPressTarget == TARGET_KEY) {
    Key = OskKeyAt (X, Y);
    if (Key != mOskPressed) {
      OskSetPressed (Key);
      mPressPolls = 0;
    }
  }
}

STATIC
VOID
OskPressCancel (
  VOID
  )
{
  if (mPressActive) {
    mPressTarget = TARGET_NONE;
    OskSetPressed (-1);
  }
}

STATIC
VOID
OskPressRelease (
  VOID
  )
{
  INTN  Key;

  if (!mPressActive) {
    return;
  }

  mPressActive = FALSE;
  if (mPressTarget == TARGET_TOGGLE) {
    if (mPressInside) {
      OskSetOpen (TRUE);
    }
  } else if (mPressTarget == TARGET_KEY) {
    Key = mOskPressed;
    OskSetPressed (-1);
    // a key that repeated has been typed already
    if ((Key >= 0) && (mPressPolls < REPEAT_DELAY_POLLS || !mOskPlace[Key].Key->Repeats)) {
      OskActivate (Key);
    }
  }

  mPressTarget = TARGET_NONE;
}

STATIC
VOID
OskPressRepeat (
  VOID
  )
{
  if (!mPressActive || (mPressTarget != TARGET_KEY) || (mOskPressed < 0)) {
    return;
  }

  mPressPolls++;
  if (!mOskPlace[mOskPressed].Key->Repeats || (mPressPolls < REPEAT_DELAY_POLLS)) {
    return;
  }

  if (((mPressPolls - REPEAT_DELAY_POLLS) % REPEAT_EVERY_POLLS) == 0) {
    OskActivate (mOskPressed);
  }
}

//
// Touch, from the touch screen driver
//

STATIC
EFI_STATUS
EFIAPI
OskAddTouchScreen (
  IN ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  *This,
  IN EFI_ABSOLUTE_POINTER_PROTOCOL         *Pointer
  )
{
  if ((Pointer == NULL) || mOskGone) {
    return EFI_INVALID_PARAMETER;
  }

  if (mTouchScreenCount >= MAX_TOUCH_SCREENS) {
    return EFI_OUT_OF_RESOURCES;
  }

  mTouchScreen[mTouchScreenCount++] = Pointer;
  mOskHasTouch                      = TRUE;
  DEBUG ((DEBUG_INFO, "OnScreenKeyboard: touch screen added\n"));
  OskStartPolling ();
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
OskTouch (
  IN ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  *This,
  IN BOOLEAN                               Down,
  IN UINT64                                X,
  IN UINT64                                Y,
  IN UINT64                                MaxX,
  IN UINT64                                MaxY,
  IN UINTN                                 Fingers
  )
{
  INT32  Sx, Sy;

  if (mOskGone) {
    return FALSE;
  }

  Sx = 0;
  Sy = 0;
  if (Down && (mOskScreenW != 0) && (mOskScreenH != 0)) {
    Sx = (INT32)DivU64x64Remainder (MultU64x32 (X, mOskScreenW), MaxX + 1, NULL);
    Sy = (INT32)DivU64x64Remainder (MultU64x32 (Y, mOskScreenH), MaxY + 1, NULL);
  }

  if (Down && !mTouchDown) {
    mTouchDown = TRUE;

    // a finger, so no mouse cursor
    if (mOskWantCursor) {
      mOskWantCursor = FALSE;
      OskSync ();
    }

    // the screen size may only be known now
    if (OskOverlayPrepare () && (mOskScreenW != 0)) {
      Sx = (INT32)DivU64x64Remainder (MultU64x32 (X, mOskScreenW), MaxX + 1, NULL);
      Sy = (INT32)DivU64x64Remainder (MultU64x32 (Y, mOskScreenH), MaxY + 1, NULL);
    }

    mTouchOwned = OskPress (Sx, Sy);
    if (mTouchOwned && (Fingers >= 2)) {
      OskPressCancel ();
    }

    return mTouchOwned;
  }

  if (!Down) {
    mTouchDown = FALSE;
    if (mTouchOwned) {
      mTouchOwned = FALSE;
      OskPressRelease ();
      return TRUE;
    }

    return FALSE;
  }

  if (!mTouchOwned) {
    return FALSE;
  }

  if (Fingers >= 2) {
    OskPressCancel ();
  } else {
    OskPressMove (Sx, Sy);
  }

  return TRUE;
}

STATIC ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  mOskProtocol = {
  OskAddTouchScreen,
  OskTouch
};

//
// Clicks for the setup browser
//

STATIC
EFI_STATUS
EFIAPI
OskGetClick (
  IN  DISPLAY_POINTER_CLICK_PROTOCOL  *This,
  OUT UINTN                           *Column,
  OUT UINTN                           *Row
  )
{
  EFI_TPL     Tpl;
  EFI_STATUS  Status;

  if ((Column == NULL) || (Row == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Tpl    = gBS->RaiseTPL (TPL_NOTIFY);
  Status = EFI_NOT_READY;
  if (mClickPending) {
    *Column         = mClickColumn;
    *Row            = mClickRow;
    mClickPending   = FALSE;
    mClickActivates = mClickPendingActivates;
    Status          = EFI_SUCCESS;
  }

  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC
BOOLEAN
EFIAPI
OskTakeActivate (
  IN DISPLAY_POINTER_CLICK_ACTIVATE_PROTOCOL  *This
  )
{
  EFI_TPL  Tpl;
  BOOLEAN  Activates;

  Tpl             = gBS->RaiseTPL (TPL_NOTIFY);
  Activates       = mClickActivates;
  mClickActivates = FALSE;
  gBS->RestoreTPL (Tpl);
  return Activates;
}

STATIC DISPLAY_POINTER_CLICK_ACTIVATE_PROTOCOL  mClickActivateProtocol = {
  1,
  OskTakeActivate
};

STATIC EFI_GUID  mClickActivateGuid = DISPLAY_POINTER_CLICK_ACTIVATE_PROTOCOL_GUID;
STATIC EFI_GUID  mUsbMouseButtonsGuid = USB_MOUSE_BUTTONS_PROTOCOL_GUID;

STATIC
VOID
EFIAPI
OskSetReading (
  IN DISPLAY_POINTER_CLICK_PROTOCOL  *This,
  IN BOOLEAN                         Reading
  )
{
  EFI_TPL  Tpl;

  Tpl           = gBS->RaiseTPL (TPL_NOTIFY);
  mClickReading = Reading;
  if (Reading) {
    // a click from before belongs to no one now
    mClickPending   = FALSE;
    mClickActivates = FALSE;
  }

  gBS->RestoreTPL (Tpl);
}

STATIC
VOID
EFIAPI
OskWaitForClick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mClickPending) {
    gBS->SignalEvent (Event);
  }
}

STATIC DISPLAY_POINTER_CLICK_PROTOCOL  mClickProtocol = {
  DISPLAY_POINTER_CLICK_REVISION,
  NULL,
  OskGetClick,
  OskSetReading
};

STATIC EFI_GUID  mClickProtocolGuid = DISPLAY_POINTER_CLICK_PROTOCOL_GUID;

/**
  The text cell under a point of the picture, as GraphicsConsole lays the
  console out: 8 x 19 cells, the grid centred in the picture.  The point
  is in the picture's own coordinates, so the rotation is already in it.
**/
STATIC
BOOLEAN
OskTextCell (
  IN  INT32  X,
  IN  INT32  Y,
  OUT UINTN  *Column,
  OUT UINTN  *Row
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  UINTN                            Columns, Rows;
  INT32                            X0, Y0;

  Out = gST->ConOut;
  if ((Out == NULL) || (Out->Mode == NULL) ||
      EFI_ERROR (Out->QueryMode (Out, (UINTN)Out->Mode->Mode, &Columns, &Rows)) ||
      (Columns * 8 > mOskScreenW) || (Rows * 19 > mOskScreenH))
  {
    return FALSE;
  }

  X0 = (INT32)((mOskScreenW - Columns * 8) / 2);
  Y0 = (INT32)((mOskScreenH - Rows * 19) / 2);
  if ((X < X0) || (Y < Y0) || (X >= X0 + (INT32)(Columns * 8)) || (Y >= Y0 + (INT32)(Rows * 19))) {
    return FALSE;
  }

  *Column = (UINTN)(X - X0) / 8;
  *Row    = (UINTN)(Y - Y0) / 19;
  return TRUE;
}

/**
  A left click off the keyboard: to the setup browser while it reads
  input, as Enter otherwise.
**/
STATIC
VOID
OskClickOffKeyboard (
  IN INT32    X,
  IN INT32    Y,
  IN BOOLEAN  Activates
  )
{
  UINTN  Column, Row;

  if (!mClickReading) {
    OskQueueKey (SCAN_NULL, CHAR_CARRIAGE_RETURN);
    return;
  }

  // outside the text grid there is nothing to click on
  if (OskTextCell (X, Y, &Column, &Row)) {
    mClickColumn           = Column;
    mClickRow              = Row;
    mClickPending          = TRUE;
    mClickPendingActivates = Activates;
    gBS->SignalEvent (mClickEvent);
  }
}

//
// Mice
//

STATIC
BOOLEAN
OskIsTouchScreen (
  IN VOID  *Interface
  )
{
  UINTN  Index;

  for (Index = 0; Index < mTouchScreenCount; Index++) {
    if ((VOID *)mTouchScreen[Index] == Interface) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
OskScanProtocol (
  IN EFI_GUID  *Guid,
  IN BOOLEAN   Absolute
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  UINTN       Known;
  VOID        *Interface;
  VOID        *Path;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, Guid, NULL, &Count, &Handles))) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    //
    // The console splitter's handle has no device path; it reads the
    // devices themselves when asked, and it must not be read as one more.
    //
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, &Path)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], Guid, &Interface)) ||
        OskIsTouchScreen (Interface))
    {
      continue;
    }

    for (Known = 0; Known < mMouseCount; Known++) {
      if ((mMice[Known].Handle == Handles[Index]) && (mMice[Known].Interface == Interface)) {
        break;
      }
    }

    if ((Known < mMouseCount) || (mMouseCount >= MAX_MICE)) {
      continue;
    }

    ZeroMem (&mMice[mMouseCount], sizeof (mMice[0]));
    mMice[mMouseCount].Handle    = Handles[Index];
    mMice[mMouseCount].Interface = Interface;
    mMice[mMouseCount].Absolute  = Absolute;
    if (!Absolute &&
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &mUsbMouseButtonsGuid, (VOID **)&mMice[mMouseCount].Buttons)))
    {
      mMice[mMouseCount].Buttons = NULL;
    }
    mMouseCount++;
    DEBUG ((DEBUG_INFO, "OnScreenKeyboard: %a pointer added\n", Absolute ? "absolute" : "relative"));
  }

  FreePool (Handles);
}

STATIC
VOID
OskScanPointers (
  VOID
  )
{
  OskScanProtocol (&gEfiSimplePointerProtocolGuid, FALSE);
  OskScanProtocol (&gEfiAbsolutePointerProtocolGuid, TRUE);
}

/**
  Drop the mice that went away (USB unplugged): their protocol is no
  longer there, or no longer the same.
**/
STATIC
VOID
OskCheckMice (
  VOID
  )
{
  UINTN  Index;
  VOID   *Interface;

  for (Index = 0; Index < mMouseCount;) {
    if (EFI_ERROR (
          gBS->HandleProtocol (
                 mMice[Index].Handle,
                 mMice[Index].Absolute ? &gEfiAbsolutePointerProtocolGuid : &gEfiSimplePointerProtocolGuid,
                 &Interface
                 )
          ) ||
        (Interface != mMice[Index].Interface))
    {
      DEBUG ((DEBUG_INFO, "OnScreenKeyboard: pointer removed\n"));
      mMice[Index] = mMice[mMouseCount - 1];
      mMouseCount--;
      continue;
    }

    Index++;
  }
}

STATIC
VOID
OskMoveCursorTo (
  IN INT32  X,
  IN INT32  Y
  )
{
  X              = MAX (0, MIN (X, (INT32)mOskScreenW - 1));
  Y              = MAX (0, MIN (Y, (INT32)mOskScreenH - 1));
  mOskCursorX    = X;
  mOskCursorY    = Y;
  mOskWantCursor = TRUE;
}

STATIC
VOID
OskMouseButtons (
  IN OSK_MOUSE  *Mouse,
  IN BOOLEAN    Left,
  IN BOOLEAN    Right,
  IN BOOLEAN    Middle
  )
{
  INT32  Ix, Iy;

  if (Left && !Mouse->Left) {
    if (!OskPress (mOskCursorX, mOskCursorY)) {
      OskClickOffKeyboard (mOskCursorX, mOskCursorY, FALSE);
    }
  } else if (!Left && Mouse->Left) {
    OskPressMove (mOskCursorX, mOskCursorY);
    OskPressRelease ();
  }

  if (Right && !Mouse->Right &&
      (OskOverlayHit (mOskCursorX, mOskCursorY, &Ix, &Iy) == OSK_PANEL_NONE))
  {
    OskQueueKey (SCAN_ESC, 0);
  }

  // middle: off the keyboard only; on it, it does nothing
  if (Middle && !Mouse->Middle &&
      (OskOverlayHit (mOskCursorX, mOskCursorY, &Ix, &Iy) == OSK_PANEL_NONE))
  {
    OskClickOffKeyboard (mOskCursorX, mOskCursorY, TRUE);
  }

  Mouse->Left   = Left;
  Mouse->Right  = Right;
  Mouse->Middle = Middle;
}

STATIC
VOID
OskWheel (
  IN INT64  Z
  )
{
  UINTN  Count;

  // away from the user scrolls up, as in a menu
  for (Count = 0; (Count < 3) && (Z != 0); Count++) {
    OskQueueKey ((Z > 0) ? SCAN_UP : SCAN_DOWN, 0);
    Z += (Z > 0) ? -1 : 1;
  }
}

STATIC
VOID
OskPollMouse (
  IN OSK_MOUSE  *Mouse
  )
{
  EFI_SIMPLE_POINTER_PROTOCOL    *Simple;
  EFI_SIMPLE_POINTER_STATE       SimpleState;
  EFI_ABSOLUTE_POINTER_PROTOCOL  *Abs;
  EFI_ABSOLUTE_POINTER_STATE     AbsState;
  INT32                          Dx, Dy;
  UINT64                         RangeX, RangeY;
  BOOLEAN                        Moved;

  Moved = FALSE;
  if (!Mouse->Absolute) {
    //
    // UsbMouseDxe installs the buttons protocol just after the Simple
    // Pointer one, so it may not have been there when the mouse was found.
    //
    if ((Mouse->Buttons == NULL) &&
        EFI_ERROR (gBS->HandleProtocol (Mouse->Handle, &mUsbMouseButtonsGuid, (VOID **)&Mouse->Buttons)))
    {
      Mouse->Buttons = NULL;
    }

    Simple = Mouse->Interface;
    if (EFI_ERROR (Simple->GetState (Simple, &SimpleState))) {
      return;
    }

    //
    // UsbMouseDxe reports raw counts.  Scaled to the screen width, so that
    // a hand's movement crosses about the same part of any screen.
    //
    Mouse->AccX += SimpleState.RelativeMovementX * (INT32)mOskScreenW;
    Mouse->AccY += SimpleState.RelativeMovementY * (INT32)mOskScreenW;
    Dx           = Mouse->AccX / MOUSE_DIVISOR;
    Dy           = Mouse->AccY / MOUSE_DIVISOR;
    Mouse->AccX -= Dx * MOUSE_DIVISOR;
    Mouse->AccY -= Dy * MOUSE_DIVISOR;
    if ((Dx != 0) || (Dy != 0) || !mOskWantCursor) {
      OskMoveCursorTo (mOskCursorX + Dx, mOskCursorY + Dy);
      Moved = TRUE;
    }

    if (Moved) {
      OskSync ();
      OskPressMove (mOskCursorX, mOskCursorY);
    }

    OskWheel (SimpleState.RelativeMovementZ);
    OskMouseButtons (
      Mouse,
      SimpleState.LeftButton,
      SimpleState.RightButton,
      (BOOLEAN)((Mouse->Buttons != NULL) && ((Mouse->Buttons->GetButtons (Mouse->Buttons) & BIT2) != 0))
      );
    return;
  }

  Abs = Mouse->Interface;
  if (EFI_ERROR (Abs->GetState (Abs, &AbsState)) || (Abs->Mode == NULL)) {
    return;
  }

  RangeX = (Abs->Mode->AbsoluteMaxX > Abs->Mode->AbsoluteMinX) ? Abs->Mode->AbsoluteMaxX - Abs->Mode->AbsoluteMinX : 1;
  RangeY = (Abs->Mode->AbsoluteMaxY > Abs->Mode->AbsoluteMinY) ? Abs->Mode->AbsoluteMaxY - Abs->Mode->AbsoluteMinY : 1;
  if ((AbsState.CurrentX >= Abs->Mode->AbsoluteMinX) && (AbsState.CurrentY >= Abs->Mode->AbsoluteMinY)) {
    OskMoveCursorTo (
      (INT32)DivU64x64Remainder (MultU64x32 (AbsState.CurrentX - Abs->Mode->AbsoluteMinX, mOskScreenW - 1), RangeX, NULL),
      (INT32)DivU64x64Remainder (MultU64x32 (AbsState.CurrentY - Abs->Mode->AbsoluteMinY, mOskScreenH - 1), RangeY, NULL)
      );
    OskSync ();
    OskPressMove (mOskCursorX, mOskCursorY);
  }

  // the wheel is an absolute position too (UsbMouseAbsolutePointerDxe)
  if (Mouse->HaveZ && (AbsState.CurrentZ != Mouse->LastZ)) {
    OskWheel ((INT64)(AbsState.CurrentZ - Mouse->LastZ));
  }

  Mouse->HaveZ = TRUE;
  Mouse->LastZ = AbsState.CurrentZ;

  OskMouseButtons (
    Mouse,
    (BOOLEAN)((AbsState.ActiveButtons & EFI_ABSP_TouchActive) != 0),
    (BOOLEAN)((AbsState.ActiveButtons & EFI_ABS_AltActive) != 0),
    (BOOLEAN)((AbsState.ActiveButtons & BIT2) != 0)    // UsbMouseAbsolutePointerDxe passes the middle button on
    );
}

STATIC
VOID
EFIAPI
OskPoll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN  Index;

  if (mOskGone) {
    return;
  }

  mTicks++;

  if (mRescan) {
    mRescan = FALSE;
    OskScanPointers ();
  }

  OskCheckMice ();

  if (!OskHavePointer ()) {
    // the last mouse went: take everything down, and stop
    OskSync ();
    gBS->SetTimer (mPollEvent, TimerCancel, 0);
    mPolling = FALSE;
    return;
  }

  // the screen first, so that the mice know its size
  OskSync ();
  if (mOskScreenW == 0) {
    return;
  }

  if (!mCursorPlaced) {
    mCursorPlaced = TRUE;
    mOskCursorX   = (INT32)mOskScreenW / 2;
    mOskCursorY   = (INT32)mOskScreenH / 2;
  }

  for (Index = 0; Index < mMouseCount; Index++) {
    OskPollMouse (&mMice[Index]);
  }

  OskPressRepeat ();

  if ((mTicks % FOLLOW_POLLS) == 0) {
    OskFollowTextCursor (FALSE);
    OskSync ();
  }

  if ((mOskDirect != 0) && ((mTicks % REDRAW_POLLS) == 0)) {
    OskOverlayPeriodic ();
  }
}

STATIC
VOID
OskStartPolling (
  VOID
  )
{
  if (mPolling || mOskGone || (mPollEvent == NULL)) {
    return;
  }

  if (!EFI_ERROR (gBS->SetTimer (mPollEvent, TimerPeriodic, POLL_100NS))) {
    mPolling = TRUE;
  }
}

STATIC
VOID
EFIAPI
OskPointerNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mOskGone) {
    return;
  }

  //
  // Look now, and poll only if something was found: the console splitter's
  // own pointer handle notifies too, on boards with no pointer at all.
  //
  OskScanPointers ();
  if (OskHavePointer ()) {
    mRescan = TRUE;
    OskStartPolling ();
  }
}

//
// Images that own the framebuffer, and the end of boot services
//

/**
  The same test as LcdGraphicsOutputDxe's: an application that was not
  loaded from a firmware volume may write the framebuffer directly.
**/
STATIC
BOOLEAN
OskImageOwnsFramebuffer (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  VOID                       *Fv;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image)) ||
      (Image->ImageCodeType != EfiLoaderCode))
  {
    return FALSE;
  }

  return (Image->DeviceHandle == NULL) ||
         EFI_ERROR (gBS->HandleProtocol (Image->DeviceHandle, &gEfiFirmwareVolume2ProtocolGuid, &Fv));
}

STATIC
EFI_STATUS
EFIAPI
OskStartImage (
  IN  EFI_HANDLE  ImageHandle,
  OUT UINTN       *ExitDataSize,
  OUT CHAR16      **ExitData  OPTIONAL
  )
{
  EFI_STATUS  Status;
  EFI_TPL     Tpl;
  BOOLEAN     Owns;

  Owns = !mOskGone && OskImageOwnsFramebuffer (ImageHandle);
  if (Owns) {
    Tpl = gBS->RaiseTPL (TPL_CALLBACK);
    OskOverlayDetach (TRUE);
    mOskDirect++;
    gBS->RestoreTPL (Tpl);
  }

  Status = mOriginalStartImage (ImageHandle, ExitDataSize, ExitData);

  if (Owns && !mOskGone) {
    Tpl = gBS->RaiseTPL (TPL_CALLBACK);
    OskOverlayDetach (TRUE);
    mOskDirect--;
    gBS->RestoreTPL (Tpl);
  }

  return Status;
}

/**
  A screenshot is about to be read (ScreenshotDxe, at TPL_CALLBACK; this
  runs at TPL_NOTIFY, before it reads).  Everything off the screen, the
  picture under it put back, and the Blt left alone until it is done.
**/
STATIC
VOID
EFIAPI
OskScreenshotStart (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mOskGone) {
    return;
  }

  mOskHiddenForShot = TRUE;
  OskOverlayDetach (TRUE);
}

/**
  The screenshot is read.  The next poll puts everything back.
**/
STATIC
VOID
EFIAPI
OskScreenshotEnd (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mOskHiddenForShot = FALSE;
}

STATIC
VOID
EFIAPI
OskExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mOskGone) {
    return;
  }

  //
  // The OS takes the screen over.  Put back what is under the layers (no
  // memory is allocated for that), give the GOP its Blt back, and stop
  // polling.
  //
  if (mPollEvent != NULL) {
    gBS->SetTimer (mPollEvent, TimerCancel, 0);
  }

  OskOverlayDetach (TRUE);
  mOskGone = TRUE;
  mPolling = FALSE;
}

STATIC
VOID
EFIAPI
OskEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  gBS->CloseEvent (Event);

  // the size chosen last time, if it is one
  Size = sizeof (mOskWantSize);
  if (EFI_ERROR (gRT->GetVariable (OSK_SIZE_VARIABLE, &mOskVariableGuid, NULL, &Size, &mOskWantSize)) ||
      (mOskWantSize >= OSK_SIZES))
  {
    mOskWantSize = OSK_SIZE_AUTO;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &mTextIn,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &mTextInEx,
                  &gEfiConsoleInDeviceGuid,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OnScreenKeyboard: could not install the text input: %r\n", Status));
    return;
  }

  // Let the console splitter take it now; BDS connects it again later.
  gBS->ConnectController (mHandle, NULL, NULL, TRUE);

  OskPointerNotify (NULL, NULL);
}

EFI_STATUS
EFIAPI
OnScreenKeyboardEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;
  VOID        *Registration;
  EFI_HANDLE  Handle;
  EFI_TPL     Tpl;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, OskWaitForKey, NULL, &mWaitEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mTextIn.WaitForKey     = mWaitEvent;
  mTextInEx.WaitForKeyEx = mWaitEvent;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, OskWaitForClick, NULL, &mClickEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mClickProtocol.WaitForClick = mClickEvent;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OskPoll, NULL, &mPollEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // touch screen drivers look for this at EndOfDxe
  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gRockchipOnScreenKeyboardProtocolGuid,
                  &mOskProtocol,
                  &mClickProtocolGuid,
                  &mClickProtocol,
                  &mClickActivateGuid,
                  &mClickActivateProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OskPointerNotify, NULL, &Event);
  if (!EFI_ERROR (Status)) {
    gBS->RegisterProtocolNotify (&gEfiSimplePointerProtocolGuid, Event, &Registration);
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OskPointerNotify, NULL, &Event);
  if (!EFI_ERROR (Status)) {
    gBS->RegisterProtocolNotify (&gEfiAbsolutePointerProtocolGuid, Event, &Registration);
  }

  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_NOTIFY,
         OskExitBootServices,
         NULL,
         &gEfiEventExitBootServicesGuid,
         &Event
         );

  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OskScreenshotStart, NULL, &mScreenshotStartGuid, &Event);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, OskScreenshotEnd, NULL, &mScreenshotEndGuid, &Event);

  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_CALLBACK,
         OskEndOfDxe,
         NULL,
         &gEfiEndOfDxeEventGroupGuid,
         &Event
         );

  //
  // gBS->StartImage, wrapped, to know when an application that owns the
  // framebuffer runs.  The table carries a CRC, so that is redone.
  //
  Tpl                 = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  mOriginalStartImage = gBS->StartImage;
  gBS->StartImage     = OskStartImage;
  gBS->Hdr.CRC32      = 0;
  gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &gBS->Hdr.CRC32);
  gBS->RestoreTPL (Tpl);

  return EFI_SUCCESS;
}
