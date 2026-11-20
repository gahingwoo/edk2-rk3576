/** @file
 *
 *  The on-screen keyboard (OnScreenKeyboardDxe), as seen by a touch screen
 *  driver.
 *
 *  The keyboard reads mice itself, through the Simple Pointer and Absolute
 *  Pointer protocols.  A touch screen is different: its driver already turns
 *  touches into keys (gestures), and one touch must not end up as both a
 *  gesture key and a keyboard key.  So a touch screen driver tells the
 *  keyboard about its pointer, which the keyboard then leaves alone, and
 *  hands it every report instead.  Touch() says whether the keyboard took
 *  the touch; when it did, the driver produces no gesture for it.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ROCKCHIP_ON_SCREEN_KEYBOARD_H_
#define ROCKCHIP_ON_SCREEN_KEYBOARD_H_

#include <Protocol/AbsolutePointer.h>

#define ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL_GUID \
  { 0x9f66333f, 0xfa49, 0x4162, { 0x83, 0xa3, 0x69, 0x48, 0x04, 0x1e, 0xad, 0x83 } }

typedef struct _ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL;

/**
  Name the absolute pointer of a touch screen whose reports will come
  through Touch().  The keyboard does not read that pointer itself.  Call it
  before the pointer is installed.
**/
typedef
EFI_STATUS
(EFIAPI *ROCKCHIP_OSK_ADD_TOUCH_SCREEN)(
  IN ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  *This,
  IN EFI_ABSOLUTE_POINTER_PROTOCOL         *Pointer
  );

/**
  One touch report.

  @param  Down     TRUE while at least one finger is on the screen; FALSE
                   once, when the last finger has left.
  @param  X, Y     Where the first finger is, in the coordinates of the
                   picture as it is drawn now (0 .. MaxX, 0 .. MaxY).  Not
                   looked at when Down is FALSE.
  @param  Fingers  How many fingers are down.

  @retval TRUE   The touch belongs to the keyboard: it started on the
                 keyboard or on its toggle key.  The answer stays the same
                 for every report of one touch.
  @retval FALSE  It does not; treat it as a gesture.
**/
typedef
BOOLEAN
(EFIAPI *ROCKCHIP_OSK_TOUCH)(
  IN ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  *This,
  IN BOOLEAN                               Down,
  IN UINT64                                X,
  IN UINT64                                Y,
  IN UINT64                                MaxX,
  IN UINT64                                MaxY,
  IN UINTN                                 Fingers
  );

struct _ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL {
  ROCKCHIP_OSK_ADD_TOUCH_SCREEN    AddTouchScreen;
  ROCKCHIP_OSK_TOUCH               Touch;
};

extern EFI_GUID  gRockchipOnScreenKeyboardProtocolGuid;

//
// Mouse clicks for the setup browser, as text cells.
//
// The display engine (patches/0013) cannot tell where a pointer is; the
// keyboard driver can.  So the keyboard driver installs this protocol, and
// while the display engine waits for input it calls SetReading (TRUE): a
// left click off the keyboard then comes to it as the text cell it landed
// on, instead of as Enter.  At any other time (the Shell, GRUB, the boot
// menu application) a click stays Enter.
//
// This is a copy of DISPLAY_POINTER_CLICK_PROTOCOL in
// MdeModulePkg/Universal/DisplayEngineDxe/PointerClick.h, which the patch
// adds; the GUID and the layout must stay the same on both sides.
//
#define DISPLAY_POINTER_CLICK_PROTOCOL_GUID \
  { 0x5b0d2c6e, 0x8e41, 0x4b7a, { 0x9f, 0x13, 0x2a, 0x6c, 0x70, 0x4e, 0xd1, 0x95 } }

#define DISPLAY_POINTER_CLICK_REVISION  1

typedef struct _DISPLAY_POINTER_CLICK_PROTOCOL DISPLAY_POINTER_CLICK_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *DISPLAY_POINTER_GET_CLICK)(
  IN  DISPLAY_POINTER_CLICK_PROTOCOL  *This,
  OUT UINTN                           *Column,
  OUT UINTN                           *Row
  );

typedef
VOID
(EFIAPI *DISPLAY_POINTER_SET_READING)(
  IN DISPLAY_POINTER_CLICK_PROTOCOL  *This,
  IN BOOLEAN                         Reading
  );

struct _DISPLAY_POINTER_CLICK_PROTOCOL {
  UINT32                         Revision;
  EFI_EVENT                      WaitForClick;
  DISPLAY_POINTER_GET_CLICK      GetClick;
  DISPLAY_POINTER_SET_READING    SetReading;
};

#endif
