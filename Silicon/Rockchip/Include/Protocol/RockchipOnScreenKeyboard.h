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

#endif
