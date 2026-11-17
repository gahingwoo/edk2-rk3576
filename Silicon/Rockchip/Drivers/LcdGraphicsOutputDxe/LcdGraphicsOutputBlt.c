/** @file

  Copyright (c) 2025, Mario Bălănică <mariobalanica02@gmail.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseMemoryLib.h>

#include "LcdGraphicsOutputDxe.h"

STATIC
EFI_STATUS
LcdGraphicsBltCheckParameters (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta       OPTIONAL
  )
{
  UINT32  HorizontalResolution = This->Mode->Info->HorizontalResolution;
  UINT32  VerticalResolution   = This->Mode->Info->VerticalResolution;

  if ((Width == 0) || (Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BltBuffer == NULL) &&
      ((BltOperation == EfiBltVideoFill) ||
       (BltOperation == EfiBltBufferToVideo) ||
       (BltOperation == EfiBltVideoToBltBuffer)))
  {
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  //
  // If we are reading data out of the video buffer,
  // check that the source area is within the display limits.
  //
  if ((BltOperation == EfiBltVideoToBltBuffer) ||
      (BltOperation == EfiBltVideoToVideo))
  {
    if ((SourceY + Height > VerticalResolution) ||
        (SourceX + Width > HorizontalResolution))
    {
      return EFI_INVALID_PARAMETER;
    }
  }

  //
  // If we are writing data into the video buffer,
  // check that the destination area is within the display limits.
  //
  if ((BltOperation == EfiBltVideoFill) ||
      (BltOperation == EfiBltBufferToVideo) ||
      (BltOperation == EfiBltVideoToVideo))
  {
    if ((DestinationY + Height > VerticalResolution) ||
        (DestinationX + Width > HorizontalResolution))
    {
      return EFI_INVALID_PARAMETER;
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
LcdGraphicsBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta       OPTIONAL
  )
{
  EFI_STATUS  Status;
  UINT32      *FrameBuffer;
  UINT32      HorizontalResolution;
  UINTN       WidthInBytes;
  UINT32      *SourceBuffer;
  UINT32      *DestinationBuffer;
  UINTN       Y;

  FrameBuffer          = (UINT32 *)This->Mode->FrameBufferBase;
  HorizontalResolution = This->Mode->Info->HorizontalResolution;
  WidthInBytes         = Width * RK_BYTES_PER_PIXEL;

  Status = LcdGraphicsBltCheckParameters (
             This,
             BltBuffer,
             BltOperation,
             SourceX,
             SourceY,
             DestinationX,
             DestinationY,
             Width,
             Height,
             Delta
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (BltOperation) {
    case EfiBltVideoFill:
      SourceBuffer = (UINT32 *)BltBuffer;

      for (Y = 0; Y < Height; Y++) {
        DestinationBuffer = FrameBuffer +
                            (DestinationY + Y) * HorizontalResolution +
                            DestinationX;

        SetMem32 (DestinationBuffer, WidthInBytes, *SourceBuffer);
      }

      break;

    case EfiBltVideoToBltBuffer:
      if (Delta == 0) {
        Delta = WidthInBytes;
      }

      for (Y = 0; Y < Height; Y++) {
        SourceBuffer = FrameBuffer +
                       (SourceY + Y) * HorizontalResolution +
                       SourceX;

        DestinationBuffer = (UINT32 *)((UINTN)BltBuffer +
                                       (DestinationY + Y) * Delta +
                                       DestinationX * RK_BYTES_PER_PIXEL);

        CopyMem (DestinationBuffer, SourceBuffer, WidthInBytes);
      }

      break;

    case EfiBltBufferToVideo:
      if (Delta == 0) {
        Delta = WidthInBytes;
      }

      for (Y = 0; Y < Height; Y++) {
        SourceBuffer = (UINT32 *)((UINTN)BltBuffer +
                                  (SourceY + Y) * Delta +
                                  SourceX * RK_BYTES_PER_PIXEL);

        DestinationBuffer = FrameBuffer +
                            (DestinationY + Y) * HorizontalResolution +
                            DestinationX;

        CopyMem (DestinationBuffer, SourceBuffer, WidthInBytes);
      }

      break;

    case EfiBltVideoToVideo:
      if (SourceY < DestinationY) {
        for (Y = Height; Y-- > 0;) {
          SourceBuffer = FrameBuffer +
                         (SourceY + Y) * HorizontalResolution +
                         SourceX;

          DestinationBuffer = FrameBuffer +
                              (DestinationY + Y) * HorizontalResolution +
                              DestinationX;

          CopyMem (DestinationBuffer, SourceBuffer, WidthInBytes);
        }
      } else {
        for (Y = 0; Y < Height; Y++) {
          SourceBuffer = FrameBuffer +
                         (SourceY + Y) * HorizontalResolution +
                         SourceX;

          DestinationBuffer = FrameBuffer +
                              (DestinationY + Y) * HorizontalResolution +
                              DestinationX;

          CopyMem (DestinationBuffer, SourceBuffer, WidthInBytes);
        }
      }

      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Address in the framebuffer of the pixel that is (X, Y) on the rotated
  screen.  The framebuffer is the panel's native scan, NativeWidth wide; the
  mode the GOP reports has the two dimensions swapped.

  90:  the rotated picture's top edge is the panel's right edge.
  270: the rotated picture's top edge is the panel's left edge.

  The geometry is read once per Blt call into LCD_ROTATION, not per pixel.
**/
typedef struct {
  UINT32     *FrameBuffer;
  UINTN      NativeWidth;
  UINTN      NativeHeight;
  BOOLEAN    Is270;
} LCD_ROTATION;

#define LcdRotatedPixel(This, X, Y)                                              \
  (Rot.Is270 ?                                                                   \
   Rot.FrameBuffer + (Y) + (Rot.NativeHeight - 1 - (X)) * Rot.NativeWidth :      \
   Rot.FrameBuffer + (Rot.NativeWidth - 1 - (Y)) + (X) * Rot.NativeWidth)

EFI_STATUS
EFIAPI
LcdGraphicsBltRotated (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta       OPTIONAL
  )
{
  EFI_STATUS  Status;
  LCD_ROTATION  Rot;
  UINTN       WidthInBytes;
  UINT32      *SourceBuffer;
  UINT32      *DestinationBuffer;
  UINTN       Y;
  UINTN       X;

  WidthInBytes = Width * RK_BYTES_PER_PIXEL;

  Rot.FrameBuffer  = (UINT32 *)This->Mode->FrameBufferBase;
  Rot.NativeWidth  = This->Mode->Info->VerticalResolution;
  Rot.NativeHeight = This->Mode->Info->HorizontalResolution;
  Rot.Is270        = (LcdGraphicsEffectiveRotation () == 270);

  Status = LcdGraphicsBltCheckParameters (
             This,
             BltBuffer,
             BltOperation,
             SourceX,
             SourceY,
             DestinationX,
             DestinationY,
             Width,
             Height,
             Delta
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (BltOperation) {
    case EfiBltVideoFill:
      SourceBuffer = (UINT32 *)BltBuffer;

      for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
          DestinationBuffer = LcdRotatedPixel (This, DestinationX + X, DestinationY + Y);

          *DestinationBuffer = *SourceBuffer;
        }
      }

      break;

    case EfiBltVideoToBltBuffer:
      if (Delta == 0) {
        Delta = WidthInBytes;
      }

      for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
          SourceBuffer = LcdRotatedPixel (This, SourceX + X, SourceY + Y);

          DestinationBuffer = (UINT32 *)((UINTN)BltBuffer +
                                         (DestinationY + Y) * Delta +
                                         (DestinationX + X) * RK_BYTES_PER_PIXEL);

          *DestinationBuffer = *SourceBuffer;
        }
      }

      break;

    case EfiBltBufferToVideo:
      if (Delta == 0) {
        Delta = WidthInBytes;
      }

      for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
          SourceBuffer = (UINT32 *)((UINTN)BltBuffer +
                                    (SourceY + Y) * Delta +
                                    (SourceX + X) * RK_BYTES_PER_PIXEL);

          DestinationBuffer = LcdRotatedPixel (This, DestinationX + X, DestinationY + Y);

          *DestinationBuffer = *SourceBuffer;
        }
      }

      break;

    case EfiBltVideoToVideo:
      if (SourceY < DestinationY) {
        for (Y = Height; Y-- > 0;) {
          for (X = 0; X < Width; X++) {
            SourceBuffer = LcdRotatedPixel (This, SourceX + X, SourceY + Y);

            DestinationBuffer = LcdRotatedPixel (This, DestinationX + X, DestinationY + Y);

            *DestinationBuffer = *SourceBuffer;
          }
        }
      } else {
        for (Y = 0; Y < Height; Y++) {
          for (X = 0; X < Width; X++) {
            SourceBuffer = LcdRotatedPixel (This, SourceX + X, SourceY + Y);

            DestinationBuffer = LcdRotatedPixel (This, DestinationX + X, DestinationY + Y);

            *DestinationBuffer = *SourceBuffer;
          }
        }
      }

      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}
