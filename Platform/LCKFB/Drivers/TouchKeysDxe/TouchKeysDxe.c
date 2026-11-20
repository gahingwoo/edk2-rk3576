/** @file
 *
 *  Touches on the TaishanPi 3M's MIPI DSI screen as key strokes.
 *
 *  The firmware's menus only take keys: the stock form browser and boot
 *  manager do not look at pointer devices.  So this is a keyboard, not a
 *  pointer.  A finger on the screen does what the four arrows, Enter and Esc
 *  do:
 *
 *    drag               an arrow key for every STEP pixels moved, in the
 *                       direction of the drag as the picture is turned
 *    tap                Enter
 *    long press         Esc
 *    two fingers        Esc
 *
 *  It is a console input device like any other, so an OS loader that reads
 *  keys from the firmware (GRUB, the Windows boot manager) gets them too.
 *
 *  The on-screen keyboard (OnScreenKeyboardDxe) gets every touch report
 *  first.  A touch that starts on the keyboard or on its toggle key is the
 *  keyboard's, and makes no gesture here; every other touch is a gesture,
 *  exactly as without the keyboard.
 *
 *  The controller is a Goodix GT9271 on I2C0 at 0x5d (the overlay
 *  rk3576-lckfb-tspi-3m-dsi-c41409463.dtsi).  It is polled: status at
 *  0x814E (bit 7 = a report is ready, low nibble = fingers down), the first
 *  finger's report at 0x814F (track id, X low, X high, Y low, Y high, ...),
 *  and 0x814E has to be written back to 0 after every report.  Coordinates
 *  are the panel's native ones, 800 x 1280.
 *
 *  When it finds the controller it also gets it ready for an OS driver: the
 *  interrupt line gets its pull-up, and the DSDT's touch screen device
 *  (\_SB.I2C0.TCH0) is made visible.  With no screen both are skipped.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/AcpiAml.h>
#include <Library/AcpiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/GpioLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipDsiPanel.h>
#include <Protocol/RockchipOnScreenKeyboard.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>

#include "TouchKeys.h"

#define TOUCH_I2C_BUS      0
#define TOUCH_I2C_ADDRESS  0x5d
#define GT_REG_STATUS      0x814E
#define GT_REG_POINT0      0x814F
#define GT_REG_ID          0x8140

#define POLL_100NS         (20 * 10000)      // 20 ms
#define STEP_PIXELS        70                // one arrow key per this much drag
#define TAP_SLOP_PIXELS    25                // movement still counted as a tap
#define LONG_PRESS_POLLS   35                // 0.7 s
#define KEY_QUEUE_SIZE     16
#define MAX_NOTIFY         16

typedef struct {
  UINTN                OperationCount;
  EFI_I2C_OPERATION    Operation[2];
} TOUCH_I2C_PACKET;

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} TOUCH_DEVICE_PATH;

typedef struct {
  BOOLEAN                    Used;
  EFI_KEY_DATA               Key;
  EFI_KEY_NOTIFY_FUNCTION    Function;
} TOUCH_NOTIFY;

STATIC TOUCH_DEVICE_PATH  mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH,                                                                                                                                             HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    TOUCH_KEYS_DEVICE_GUID
  },
  { END_DEVICE_PATH_TYPE,                                                                                                                                               END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_I2C_MASTER_PROTOCOL  *mMaster;
STATIC EFI_HANDLE               mHandle;
STATIC EFI_EVENT                mPollEvent;
STATIC EFI_EVENT                mWaitEvent;
STATIC UINT16                   mRotation;          // what the picture is turned by when it is turned
STATIC UINT16                   mTouchRotation;     // what it is turned by during the current touch
STATIC BOOLEAN                  mStopped;           // ExitBootServices

STATIC ROCKCHIP_ON_SCREEN_KEYBOARD_PROTOCOL  *mOsk;
STATIC BOOLEAN                               mOskOwned;  // the current touch is the keyboard's

STATIC EFI_INPUT_KEY  mQueue[KEY_QUEUE_SIZE];
STATIC UINTN          mQueueHead;
STATIC UINTN          mQueueCount;
STATIC TOUCH_NOTIFY   mNotify[MAX_NOTIFY];

// gesture state
STATIC BOOLEAN  mDown;
STATIC BOOLEAN  mMoved;
STATIC BOOLEAN  mTwoFingers;
STATIC INT32    mStartX, mStartY;
STATIC INT32    mAnchorX, mAnchorY;
STATIC UINTN    mDownPolls;

//
// The same touches as an absolute pointer, for TouchTest and for whatever
// wants a pointer later.  The coordinates are those of the picture: when
// the GOP is rotated the point is rotated with it and the two maxima are
// swapped, so that a pointer always lines up with what Blt draws.
// CurrentZ carries the number of fingers down.
//
STATIC EFI_ABSOLUTE_POINTER_MODE   mAbsMode = { 0, 0, 0, 799, 1279, 10, 0 };
STATIC UINT32                      mNativeMaxX = 799;
STATIC UINT32                      mNativeMaxY = 1279;
STATIC EFI_ABSOLUTE_POINTER_STATE  mAbsState;
STATIC BOOLEAN                     mAbsChanged;
STATIC EFI_EVENT                   mAbsEvent;

STATIC
UINT16
TouchCurrentRotation (
  VOID
  );

STATIC
EFI_STATUS
TouchTransfer (
  IN     UINT16   Register,
  IN OUT UINT8    *Buffer,
  IN     UINT32   Length,
  IN     BOOLEAN  Write
  )
{
  EFI_STATUS        Status;
  TOUCH_I2C_PACKET  Packet;
  UINT8             Out[4];

  if (mMaster == NULL) {
    return EFI_NOT_READY;
  }

  Out[0] = (UINT8)(Register >> 8);
  Out[1] = (UINT8)Register;

  if (Write) {
    if (Length > sizeof (Out) - 2) {
      return EFI_INVALID_PARAMETER;
    }

    CopyMem (&Out[2], Buffer, Length);
    Packet.OperationCount             = 1;
    Packet.Operation[0].Flags         = 0;
    Packet.Operation[0].LengthInBytes = 2 + Length;
    Packet.Operation[0].Buffer        = Out;
  } else {
    Packet.OperationCount             = 2;
    Packet.Operation[0].Flags         = 0;
    Packet.Operation[0].LengthInBytes = 2;
    Packet.Operation[0].Buffer        = Out;
    Packet.Operation[1].Flags         = I2C_FLAG_READ;
    Packet.Operation[1].LengthInBytes = Length;
    Packet.Operation[1].Buffer        = Buffer;
  }

  Status = mMaster->StartRequest (mMaster, TOUCH_I2C_ADDRESS, (EFI_I2C_REQUEST_PACKET *)&Packet, NULL, NULL);
  if (!EFI_ERROR (Status) && !Write && (Packet.Operation[1].LengthInBytes != Length)) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

STATIC
VOID
TouchQueueKey (
  IN UINT16  ScanCode,
  IN CHAR16  UnicodeChar
  )
{
  EFI_KEY_DATA  KeyData;
  UINTN         Index;

  if (mQueueCount < KEY_QUEUE_SIZE) {
    Index                        = (mQueueHead + mQueueCount) % KEY_QUEUE_SIZE;
    mQueue[Index].ScanCode       = ScanCode;
    mQueue[Index].UnicodeChar    = UnicodeChar;
    mQueueCount++;
  }

  //
  // Hot keys (Esc during the boot countdown and the like) are not read from
  // the queue; whoever wants one registers for it.
  //
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

/**
  A drag of (Dx, Dy) in the panel's native coordinates, as seen on the
  picture the firmware draws.  Same mapping as LcdGraphicsOutputDxe's rotated
  Blt: 270 puts the picture's top at the panel's left edge, 90 at its right.
**/
STATIC
VOID
TouchToPicture (
  IN  INT32  Dx,
  IN  INT32  Dy,
  OUT INT32  *PictureDx,
  OUT INT32  *PictureDy
  )
{
  switch (mTouchRotation) {
    case 270:
      *PictureDx = -Dy;
      *PictureDy = Dx;
      break;
    case 90:
      *PictureDx = Dy;
      *PictureDy = -Dx;
      break;
    default:
      *PictureDx = Dx;
      *PictureDy = Dy;
      break;
  }
}

STATIC
VOID
EFIAPI
TouchPoll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT8  Status;
  UINT8  Point[5];
  UINT8  Zero = 0;
  UINTN  Fingers;
  INT32  X, Y, Dx, Dy;
  BOOLEAN  First;

  if (mStopped) {
    return;
  }

  //
  // Between touches, follow the picture: the rotation goes off and on with
  // the images that are started, and the pointer's range goes with it.
  //
  if (!mDown) {
    mTouchRotation = TouchCurrentRotation ();
    if (mTouchRotation != 0) {
      mAbsMode.AbsoluteMaxX = mNativeMaxY;
      mAbsMode.AbsoluteMaxY = mNativeMaxX;
    } else {
      mAbsMode.AbsoluteMaxX = mNativeMaxX;
      mAbsMode.AbsoluteMaxY = mNativeMaxY;
    }
  }

  if (EFI_ERROR (TouchTransfer (GT_REG_STATUS, &Status, 1, FALSE))) {
    return;
  }

  if ((Status & 0x80) == 0) {
    // No new report.  A finger that stays down keeps reporting, so this is
    // only the gap between two reports.
    if (mDown) {
      mDownPolls++;
    }

    return;
  }

  Fingers = Status & 0x0F;

  if (Fingers > 0) {
    if (EFI_ERROR (TouchTransfer (GT_REG_POINT0, Point, sizeof (Point), FALSE))) {
      TouchTransfer (GT_REG_STATUS, &Zero, 1, TRUE);
      return;
    }

    X = Point[1] | (Point[2] << 8);
    Y = Point[3] | (Point[4] << 8);

    //
    // The inverse of LcdRotatedPixel(): at 270 the picture's top edge is
    // the panel's left edge, at 90 it is the panel's right edge.
    //
    switch (mTouchRotation) {
      case 270:
        mAbsState.CurrentX    = (UINT64)((INT32)mNativeMaxY - Y);
        mAbsState.CurrentY    = (UINT64)X;
        break;
      case 90:
        mAbsState.CurrentX    = (UINT64)Y;
        mAbsState.CurrentY    = (UINT64)((INT32)mNativeMaxX - X);
        break;
      default:
        mAbsState.CurrentX    = (UINT64)X;
        mAbsState.CurrentY    = (UINT64)Y;
        break;
    }

    mAbsState.CurrentZ      = Fingers;
    mAbsState.ActiveButtons = EFI_ABSP_TouchActive;
    mAbsChanged             = TRUE;
    gBS->SignalEvent (mAbsEvent);

    First = !mDown;
    if (!mDown) {
      mDown       = TRUE;
      mMoved      = FALSE;
      mTwoFingers = FALSE;
      mStartX     = mAnchorX = X;
      mStartY     = mAnchorY = Y;
      mDownPolls  = 0;
      mOskOwned   = FALSE;
    }

    mDownPolls++;
    if (Fingers >= 2) {
      mTwoFingers = TRUE;
    }

    //
    // The keyboard decides on the first report of a touch whether it is
    // its own; it keeps that answer to the end of the touch.
    //
    if (mOsk != NULL) {
      if (mOsk->Touch (mOsk, TRUE, mAbsState.CurrentX, mAbsState.CurrentY, mAbsMode.AbsoluteMaxX, mAbsMode.AbsoluteMaxY, Fingers) && First) {
        mOskOwned = TRUE;
      }
    }

    if (mOskOwned) {
      TouchTransfer (GT_REG_STATUS, &Zero, 1, TRUE);
      return;
    }

    TouchToPicture (X - mAnchorX, Y - mAnchorY, &Dx, &Dy);

    if (!mTwoFingers) {
      if ((ABS (Dy) >= STEP_PIXELS) && (ABS (Dy) >= ABS (Dx))) {
        TouchQueueKey ((Dy > 0) ? SCAN_DOWN : SCAN_UP, 0);
        mMoved   = TRUE;
        mAnchorX = X;
        mAnchorY = Y;
      } else if ((ABS (Dx) >= STEP_PIXELS) && (ABS (Dx) > ABS (Dy))) {
        TouchQueueKey ((Dx > 0) ? SCAN_RIGHT : SCAN_LEFT, 0);
        mMoved   = TRUE;
        mAnchorX = X;
        mAnchorY = Y;
      }
    }
  } else if (mDown) {
    // released
    mDown = FALSE;

    mAbsState.CurrentZ      = 0;
    mAbsState.ActiveButtons = 0;
    mAbsChanged             = TRUE;
    gBS->SignalEvent (mAbsEvent);

    if (mOsk != NULL) {
      mOsk->Touch (mOsk, FALSE, 0, 0, mAbsMode.AbsoluteMaxX, mAbsMode.AbsoluteMaxY, 0);
    }

    if (mOskOwned) {
      mOskOwned = FALSE;
    } else if (mTwoFingers) {
      TouchQueueKey (SCAN_ESC, 0);
    } else if (!mMoved) {
      if (mDownPolls >= LONG_PRESS_POLLS) {
        TouchQueueKey (SCAN_ESC, 0);
      } else {
        TouchQueueKey (SCAN_NULL, CHAR_CARRIAGE_RETURN);
      }
    }
  }

  TouchTransfer (GT_REG_STATUS, &Zero, 1, TRUE);
}

//
// Simple Text Input (and Ex)
//

STATIC
EFI_STATUS
EFIAPI
TouchReset (
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
TouchReadKeyStroke (
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
TouchWaitForKey (
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
TouchResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  return TouchReset (NULL, ExtendedVerification);
}

STATIC
EFI_STATUS
EFIAPI
TouchReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (KeyData, sizeof (*KeyData));
  return TouchReadKeyStroke (NULL, &KeyData->Key);
}

STATIC
EFI_STATUS
EFIAPI
TouchSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
TouchRegisterKeyNotify (
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
TouchUnregisterKeyNotify (
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
  TouchReset,
  TouchReadKeyStroke,
  NULL
};

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mTextInEx = {
  TouchResetEx,
  TouchReadKeyStrokeEx,
  NULL,
  TouchSetState,
  TouchRegisterKeyNotify,
  TouchUnregisterKeyNotify
};

//
// Absolute Pointer
//

STATIC
EFI_STATUS
EFIAPI
TouchAbsReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  IN BOOLEAN                        ExtendedVerification
  )
{
  mAbsChanged = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TouchAbsGetState (
  IN  EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  OUT EFI_ABSOLUTE_POINTER_STATE     *State
  )
{
  EFI_TPL  Tpl;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (!mAbsChanged) {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }

  *State      = mAbsState;
  mAbsChanged = FALSE;
  gBS->RestoreTPL (Tpl);

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
TouchAbsWait (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mAbsChanged) {
    gBS->SignalEvent (Event);
  }
}

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  mAbsPointer = {
  TouchAbsReset,
  TouchAbsGetState,
  NULL,
  &mAbsMode
};

/**
  Offer TouchTest in the boot manager.  It is an application in the same
  firmware volume as this driver; the option is only added on a board where
  the controller answered, and only once.
**/
STATIC
VOID
TouchAddTestOption (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                         Status;
  EFI_GUID                           FileGuid = TOUCH_TEST_FILE_GUID;
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL           *Path;
  EFI_BOOT_MANAGER_LOAD_OPTION       Option;
  EFI_BOOT_MANAGER_LOAD_OPTION       *Options;
  UINTN                              Count;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR (Status)) {
    return;
  }

  Path = DevicePathFromHandle (LoadedImage->DeviceHandle);
  if (Path == NULL) {
    return;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &FileGuid);
  Path = AppendDevicePathNode (Path, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  if (Path == NULL) {
    return;
  }

  Status = EfiBootManagerInitializeLoadOption (
             &Option,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP,
             L"Touch Test",
             Path,
             NULL,
             0
             );
  FreePool (Path);
  if (EFI_ERROR (Status)) {
    return;
  }

  Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  if (EfiBootManagerFindLoadOption (&Option, Options, Count) == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&Option, MAX_UINTN);
    DEBUG ((DEBUG_INFO, "TouchKeys: boot option \"Touch Test\" added: %r\n", Status));
  }

  EfiBootManagerFreeLoadOption (&Option);
  EfiBootManagerFreeLoadOptions (Options, Count);
}

/**
  Is the firmware's picture turned right now?  LcdGraphicsOutputDxe takes
  the rotation off while an application or an OS loader runs and puts it
  back afterwards; a rotated GOP is one that reports the panel's two
  dimensions swapped.  Asked at the start of every touch.
**/
STATIC
UINT16
TouchCurrentRotation (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  if ((mRotation == 0) ||
      EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop)) ||
      (Gop->Mode == NULL) || (Gop->Mode->Info == NULL))
  {
    return mRotation;
  }

  return (Gop->Mode->Info->HorizontalResolution == mNativeMaxY + 1) ? mRotation : 0;
}

STATIC
EFI_I2C_MASTER_PROTOCOL *
TouchFindMaster (
  VOID
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_I2C_MASTER_PROTOCOL   *Master;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiI2cMasterProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  Master = NULL;
  for (Index = 0; Index < Count; Index++) {
    // I2cDxe's bus path: VENDOR_DEVICE_PATH followed by the bus number.
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (EFI_ERROR (Status) ||
        (Path->Type != HARDWARE_DEVICE_PATH) || (Path->SubType != HW_VENDOR_DP) ||
        (*(UINTN *)((UINT8 *)Path + sizeof (VENDOR_DEVICE_PATH)) != TOUCH_I2C_BUS))
    {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiI2cMasterProtocolGuid, (VOID **)&Master))) {
      Master = NULL;
    }

    break;
  }

  gBS->FreePool (Handles);
  return Master;
}

//
// Windows gets the controller too, through \_SB.I2C0.TCH0 (RKCP9271) in the
// DSDT, whose _STA is 0 until \_SB.I2C0.TCH0.TCHP is 1.  Only this driver
// knows whether the screen is there, so it sets TCHP when it found the
// controller: with no screen, Windows sees no touch device at all.
//
// ReadyToBoot, because RK3576AcpiPlatformDxe installs the DSDT at EndOfDxe
// and the order of two EndOfDxe callbacks is not fixed.  TCHP is "Name (TCHP,
// 0)", a single ZeroOp after the name, so the patch is one byte, ZeroOp to
// OneOp, in the installed table, and then the checksum.  Nothing else in the
// table is touched, and if TCHP is not found nothing is changed at all.
//
#define TOUCH_ACPI_PRESENT_NAME  "TCHP"

STATIC
VOID
EFIAPI
TouchReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_SDT_PROTOCOL        *AcpiSdt;
  EFI_ACPI_DESCRIPTION_HEADER  *Dsdt;
  UINTN                        TableIndex;
  UINTN                        TableKey;
  UINT8                        *Aml;
  UINTN                        Index;

  gBS->CloseEvent (Event);

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&AcpiSdt);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "TouchKeys: no ACPI, nothing to tell the OS\n"));
    return;
  }

  TableIndex = 0;
  Status     = AcpiLocateTableBySignature (
                 AcpiSdt,
                 EFI_ACPI_6_3_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
                 &TableIndex,
                 &Dsdt,
                 &TableKey
                 );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "TouchKeys: no DSDT (%r)\n", Status));
    return;
  }

  Aml = (UINT8 *)Dsdt;
  for (Index = sizeof (EFI_ACPI_DESCRIPTION_HEADER);
       Index + 1 + AML_NAME_SEG_SIZE < Dsdt->Length;
       Index++)
  {
    if ((Aml[Index] != AML_NAME_OP) ||
        (CompareMem (&Aml[Index + 1], TOUCH_ACPI_PRESENT_NAME, AML_NAME_SEG_SIZE) != 0))
    {
      continue;
    }

    //
    // Not "Name (TCHP, 0)": either a byte that only looks like a NameOp (a
    // PkgLength of 8 before a reference to TCHP), or the name already set.
    //
    if (Aml[Index + 1 + AML_NAME_SEG_SIZE] != AML_ZERO_OP) {
      continue;
    }

    Aml[Index + 1 + AML_NAME_SEG_SIZE] = AML_ONE_OP;
    AcpiUpdateChecksum (Aml, Dsdt->Length);
    DEBUG ((DEBUG_INFO, "TouchKeys: DSDT %a = 1, touch screen shown to the OS\n", TOUCH_ACPI_PRESENT_NAME));
    return;
  }

  DEBUG ((DEBUG_INFO, "TouchKeys: DSDT has no \"Name (%a, 0)\", left alone\n", TOUCH_ACPI_PRESENT_NAME));
}

//
// The controller's interrupt line, GPIO0_C5, has no pull-up on the board,
// and an OS that takes the interrupt needs one (the touch-int pinctrl of
// rk3576-lckfb-tspi-3m-uefi.dts: RK_FUNC_GPIO, pcfg_pull_up).  RK3576
// GpioLib has no pull support, so the PMU1 IOC register is written here:
// GPIO0C_P at IOC + 0x202C (pinctrl-rockchip.c, rk3576_calc_pull_reg_and_bit:
// RK3576_PULL_GPIO0_BH_OFFSET - 4 + (21 / 8) * 4), two bits per pin, C5 at
// bits 11:10, 0b11 = pull-up, write-enable in the upper half.
//
// Only once the controller has answered at 0x5d: its address is latched by
// then, so the level of this line no longer selects it.
//
#define TOUCH_INT_PULL_REG  (0x26040000UL + 0x202CUL)
#define TOUCH_INT_PULL_UP   (0x3U << 10)
#define TOUCH_INT_PULL_WE   (0x3U << (10 + 16))

STATIC
VOID
TouchPrepareForOs (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  GpioPinSetFunction (0, GPIO_PIN_PC5, 0);
  MmioWrite32 (TOUCH_INT_PULL_REG, TOUCH_INT_PULL_WE | TOUCH_INT_PULL_UP);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  TouchReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "TouchKeys: no ReadyToBoot event, the OS will not see the touch screen: %r\n", Status));
  }
}

/**
  The OS takes over: no more I2C traffic, no more keys.
**/
STATIC
VOID
EFIAPI
TouchExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mStopped = TRUE;
  if (mPollEvent != NULL) {
    gBS->SetTimer (mPollEvent, TimerCancel, 0);
  }
}

STATIC
VOID
EFIAPI
TouchEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                   Status;
  UINT8                        Id[4] = { 0, 0, 0, 0 };
  UINTN                        Frequency;
  UINT16                       Setting;
  ROCKCHIP_DSI_PANEL_PROTOCOL  *Panel;
  EFI_EVENT                    ExitEvent;

  gBS->CloseEvent (Event);

  mMaster = TouchFindMaster ();
  if (mMaster == NULL) {
    DEBUG ((DEBUG_INFO, "TouchKeys: no I2C%u master\n", TOUCH_I2C_BUS));
    return;
  }

  Frequency = 100000;
  mMaster->SetBusFrequency (mMaster, &Frequency);

  Status = TouchTransfer (GT_REG_ID, Id, sizeof (Id), FALSE);
  if (EFI_ERROR (Status) || (Id[0] < '0') || (Id[0] > '9') || (Id[1] < '0') || (Id[1] > '9')) {
    DEBUG ((DEBUG_INFO, "TouchKeys: no Goodix controller at 0x%02x (%r)\n", TOUCH_I2C_ADDRESS, Status));
    mMaster = NULL;
    return;
  }

  TouchPrepareForOs ();

  //
  // The rotation of the picture, worked out as LcdGraphicsOutputDxe does:
  // the setting if it names one, else what the panel declares.
  //
  Setting   = PcdGet16 (PcdDisplayRotation);
  mRotation = 0;
  Panel     = NULL;
  gBS->LocateProtocol (&gRockchipDsiPanelProtocolGuid, NULL, (VOID **)&Panel);
  if ((Setting == 90) || (Setting == 270)) {
    mRotation = Setting;
  } else if ((Setting == 0) && (Panel != NULL) &&
             ((Panel->Rotation == 90) || (Panel->Rotation == 270)))
  {
    mRotation = Panel->Rotation;
  }

  if ((Panel != NULL) && (Panel->NativeMode.HActive != 0) && (Panel->NativeMode.VActive != 0)) {
    mNativeMaxX = Panel->NativeMode.HActive - 1;
    mNativeMaxY = Panel->NativeMode.VActive - 1;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, TouchWaitForKey, NULL, &mWaitEvent);
  if (EFI_ERROR (Status)) {
    return;
  }

  mTextIn.WaitForKey     = mWaitEvent;
  mTextInEx.WaitForKeyEx = mWaitEvent;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, TouchAbsWait, NULL, &mAbsEvent);
  if (EFI_ERROR (Status)) {
    return;
  }

  mAbsPointer.WaitForInput = mAbsEvent;

  //
  // Tell the on-screen keyboard about this screen before the pointer is
  // installed, so that it never reads the pointer itself.
  //
  if (EFI_ERROR (gBS->LocateProtocol (&gRockchipOnScreenKeyboardProtocolGuid, NULL, (VOID **)&mOsk)) ||
      EFI_ERROR (mOsk->AddTouchScreen (mOsk, &mAbsPointer)))
  {
    mOsk = NULL;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &mTextIn,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &mTextInEx,
                  &gEfiAbsolutePointerProtocolGuid,
                  &mAbsPointer,
                  &gEfiConsoleInDeviceGuid,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "TouchKeys: could not install the input protocols: %r\n", Status));
    return;
  }

  // Let the console splitter take it now; BDS connects it again later.
  gBS->ConnectController (mHandle, NULL, NULL, TRUE);

  TouchAddTestOption ((EFI_HANDLE)Context);

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TouchPoll, NULL, &mPollEvent);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mPollEvent, TimerPeriodic, POLL_100NS);
  }

  if (!EFI_ERROR (Status)) {
    gBS->CreateEventEx (
           EVT_NOTIFY_SIGNAL,
           TPL_NOTIFY,
           TouchExitBootServices,
           NULL,
           &gEfiEventExitBootServicesGuid,
           &ExitEvent
           );
  }

  DEBUG ((
    DEBUG_INFO,
    "TouchKeys: Goodix \"%c%c%c%c\" on I2C%u, picture rotation %u, polling %r, on-screen keyboard %a\n",
    Id[0], Id[1], Id[2], Id[3] ? Id[3] : ' ',
    TOUCH_I2C_BUS,
    mRotation,
    Status,
    (mOsk != NULL) ? "yes" : "no"
    ));
}

EFI_STATUS
EFIAPI
TouchKeysEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  Event;

  //
  // The I2C bus driver may be dispatched after this one; look for the
  // controller when every driver has had its turn.
  //
  return gBS->CreateEventEx (
                EVT_NOTIFY_SIGNAL,
                TPL_CALLBACK,
                TouchEndOfDxe,
                ImageHandle,
                &gEfiEndOfDxeEventGroupGuid,
                &Event
                );
}
