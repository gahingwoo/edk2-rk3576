/** @file

  Simple Framebuffer GOP Driver for RK3576.

  Reads the VOP2 framebuffer configuration established by the upstream
  U-Boot SPL/proper before EDK2 is loaded, and installs
  EFI_GRAPHICS_OUTPUT_PROTOCOL pointing at that pre-existing framebuffer.

  No VOP2 re-initialization is performed.  If the register snapshot is
  invalid (zero framebuffer address or degenerate resolution) the driver
  allocates a fresh framebuffer and writes its address into the VOP2
  WIN0_YRGB_MST register so the display keeps showing the correct data.

  Copyright (c) 2025, ROCK 4D RK3576 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/GraphicsOutput.h>

/* -------------------------------------------------------------------------
 * RK3576 VOP2 register addresses
 *
 * Primary set — from the Linux-kernel DRM driver for RK3576.
 * These are what U-Boot proper writes when it initialises HDMI via the
 * upstream DRM/KMS stack.
 *
 * Cluster0 WIN0 YRGB master address  (framebuffer physical address)
 *   VOP2 base 0x27D0_0000, WIN0_YRGB_MST at offset 0x2020
 *
 * VP0 DSP_INFO  (packed display size populated by the VP post-processor)
 *   VOP2 base 0x27D0_0000, VP0_DSP_INFO at offset 0xC02C
 *   Format: bits[15:0]  = horizontal active width  - 1
 *           bits[31:16] = vertical active height - 1
 *
 * Alternate set — from the edk2-rockchip Vop2Dxe driver (RK3568-compatible
 * register map reused for RK3576).  Checked as fallback when the primary
 * registers read back zero.
 * -------------------------------------------------------------------------
 */
#define RK3576_VOP2_BASE              0x27D00000UL

/* Primary (Linux-kernel / U-Boot mainline RK3576 DRM) */
#define RK3576_WIN0_YRGB_MST_ADDR    (RK3576_VOP2_BASE + 0x2020UL)
#define RK3576_VP0_DSP_INFO_ADDR     (RK3576_VOP2_BASE + 0xC02CUL)

/* Alternate (edk2-rockchip Vop2Dxe CLUSTER0 WIN0, WinOffset=0) */
#define RK3576_WIN0_YRGB_MST_ALT     (RK3576_VOP2_BASE + 0x1010UL)
#define RK3576_WIN0_DSP_INFO_ALT     (RK3576_VOP2_BASE + 0x1024UL)

/* Minimum/maximum sane display dimensions (pixels) */
#define FB_MIN_WIDTH    320U
#define FB_MAX_WIDTH   7680U
#define FB_MIN_HEIGHT   240U
#define FB_MAX_HEIGHT  4320U

/* Default fallback resolution when no valid VOP2 state is found */
#define FB_DEFAULT_WIDTH   1920U
#define FB_DEFAULT_HEIGHT  1080U

/* 32 bpp, pixel layout: [B][G][R][X] = PixelBlueGreenRedReserved8BitPerColor */
#define FB_BYTES_PER_PIXEL  4U

/* -------------------------------------------------------------------------
 * Module-level state
 * -------------------------------------------------------------------------
 */
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mModeInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mGopMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL          mGop;

/* -------------------------------------------------------------------------
 * Blt helpers
 * -------------------------------------------------------------------------
 */

/**
  Return the byte address of pixel (X, Y) in the framebuffer.
**/
STATIC
UINT32 *
FbPixel (
  IN EFI_PHYSICAL_ADDRESS  FbBase,
  IN UINT32                Stride,
  IN UINTN                 X,
  IN UINTN                 Y
  )
{
  return (UINT32 *)((UINTN)FbBase + Y * Stride + X * FB_BYTES_PER_PIXEL);
}

/* -------------------------------------------------------------------------
 * EFI_GRAPHICS_OUTPUT_PROTOCOL implementation
 * -------------------------------------------------------------------------
 */

STATIC
EFI_STATUS
EFIAPI
SimpleFbQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if ((Info == NULL) || (SizeOfInfo == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (
            sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
            &mModeInfo
            );
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SimpleFbSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  UINT32  *Pixel;
  UINTN    Row;
  UINTN    Col;

  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  /*
   * Clear the framebuffer to black so the caller gets a clean canvas.
   * We write each row explicitly to avoid relying on any DMB/cache flush
   * library that may not be available in all build configurations.
   */
  for (Row = 0; Row < mModeInfo.VerticalResolution; Row++) {
    Pixel = FbPixel (
              mGopMode.FrameBufferBase,
              mModeInfo.PixelsPerScanLine * FB_BYTES_PER_PIXEL,
              0,
              Row
              );
    for (Col = 0; Col < mModeInfo.HorizontalResolution; Col++) {
      Pixel[Col] = 0;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SimpleFbBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta
  )
{
  UINT32   Stride;
  UINTN    SrcRowDeltaBytes;
  UINTN    Row;
  UINTN    Col;
  UINT32  *SrcRow;
  UINT32  *DstRow;
  UINT32   FillColor;

  if (Width == 0 || Height == 0) {
    return EFI_SUCCESS;
  }

  Stride = mModeInfo.PixelsPerScanLine * FB_BYTES_PER_PIXEL;

  /* Delta == 0 means BltBuffer stride == Width * 4 bytes */
  SrcRowDeltaBytes = (Delta == 0) ? (Width * FB_BYTES_PER_PIXEL) : Delta;

  switch (BltOperation) {

    case EfiBltVideoFill:
      /*
       * Fill the rectangle [DestX, DestY, Width, Height] with a single
       * colour taken from BltBuffer[0].
       */
      FillColor = *(UINT32 *)BltBuffer;
      FillColor &= 0x00FFFFFFU;  /* zero the reserved byte */
      for (Row = 0; Row < Height; Row++) {
        DstRow = FbPixel (
                   mGopMode.FrameBufferBase,
                   Stride,
                   DestinationX,
                   DestinationY + Row
                   );
        for (Col = 0; Col < Width; Col++) {
          DstRow[Col] = FillColor;
        }
      }
      break;

    case EfiBltBufferToVideo:
      /*
       * Copy from BltBuffer[SourceX, SourceY] to video[DestX, DestY].
       */
      for (Row = 0; Row < Height; Row++) {
        SrcRow = (UINT32 *)((UINT8 *)BltBuffer +
                            (SourceY + Row) * SrcRowDeltaBytes +
                            SourceX * FB_BYTES_PER_PIXEL);
        DstRow = FbPixel (
                   mGopMode.FrameBufferBase,
                   Stride,
                   DestinationX,
                   DestinationY + Row
                   );
        for (Col = 0; Col < Width; Col++) {
          DstRow[Col] = SrcRow[Col] & 0x00FFFFFFU;
        }
      }
      break;

    case EfiBltVideoToBltBuffer:
      /*
       * Readback from video[SourceX, SourceY] to BltBuffer[DestX, DestY].
       */
      for (Row = 0; Row < Height; Row++) {
        SrcRow = FbPixel (
                   mGopMode.FrameBufferBase,
                   Stride,
                   SourceX,
                   SourceY + Row
                   );
        DstRow = (UINT32 *)((UINT8 *)BltBuffer +
                            (DestinationY + Row) * SrcRowDeltaBytes +
                            DestinationX * FB_BYTES_PER_PIXEL);
        for (Col = 0; Col < Width; Col++) {
          DstRow[Col] = SrcRow[Col];
        }
      }
      break;

    case EfiBltVideoToVideo:
      /*
       * Copy within video memory from [SourceX,SourceY] to
       * [DestX,DestY].  Use a temp row buffer to handle overlap.
       */
      {
        UINT32  *Tmp = AllocatePool (Width * FB_BYTES_PER_PIXEL);
        if (Tmp == NULL) {
          return EFI_OUT_OF_RESOURCES;
        }
        for (Row = 0; Row < Height; Row++) {
          SrcRow = FbPixel (
                     mGopMode.FrameBufferBase,
                     Stride,
                     SourceX,
                     SourceY + Row
                     );
          CopyMem (Tmp, SrcRow, Width * FB_BYTES_PER_PIXEL);
          DstRow = FbPixel (
                     mGopMode.FrameBufferBase,
                     Stride,
                     DestinationX,
                     DestinationY + Row
                     );
          CopyMem (DstRow, Tmp, Width * FB_BYTES_PER_PIXEL);
        }
        FreePool (Tmp);
      }
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------
 */

/**
  Try to read a valid framebuffer address and DSP_INFO word from VOP2.

  Returns TRUE if a valid configuration was found and fills in *FbAddr,
  *Width, and *Height.  Returns FALSE if the registers all read zero or
  the dimensions look degenerate.
**/
STATIC
BOOLEAN
ReadVop2State (
  OUT EFI_PHYSICAL_ADDRESS  *FbAddr,
  OUT UINT32                *Width,
  OUT UINT32                *Height
  )
{
  UINT32  YrgbMst;
  UINT32  DspInfo;
  UINT32  W;
  UINT32  H;

  /* --- Primary registers (Linux-kernel / U-Boot mainline for RK3576) --- */
  YrgbMst = MmioRead32 (RK3576_WIN0_YRGB_MST_ADDR);
  DspInfo  = MmioRead32 (RK3576_VP0_DSP_INFO_ADDR);

  DEBUG ((
    DEBUG_INFO,
    "RK3576SimpleFb: Primary registers -"
    " WIN0_YRGB_MST[0x%08x]=0x%08x  VP0_DSP_INFO[0x%08x]=0x%08x\n",
    (UINT32)RK3576_WIN0_YRGB_MST_ADDR,
    YrgbMst,
    (UINT32)RK3576_VP0_DSP_INFO_ADDR,
    DspInfo
    ));

  if ((YrgbMst != 0) && (DspInfo != 0)) {
    W = (DspInfo & 0xFFFFU) + 1;
    H = ((DspInfo >> 16) & 0xFFFFU) + 1;
    if ((W >= FB_MIN_WIDTH)  && (W <= FB_MAX_WIDTH) &&
        (H >= FB_MIN_HEIGHT) && (H <= FB_MAX_HEIGHT))
    {
      *FbAddr = (EFI_PHYSICAL_ADDRESS)YrgbMst;
      *Width  = W;
      *Height = H;
      DEBUG ((
        DEBUG_INFO,
        "RK3576SimpleFb: Using primary regs -> FB=0x%lx %ux%u\n",
        *FbAddr, *Width, *Height
        ));
      return TRUE;
    }
    DEBUG ((
      DEBUG_WARN,
      "RK3576SimpleFb: Primary regs gave degenerate dims %ux%u — trying alt\n",
      W,
      H
      ));
  }

  /* --- Alternate registers (edk2-rockchip Vop2Dxe CLUSTER0 WinOffset=0) --- */
  YrgbMst = MmioRead32 (RK3576_WIN0_YRGB_MST_ALT);
  DspInfo  = MmioRead32 (RK3576_WIN0_DSP_INFO_ALT);

  DEBUG ((
    DEBUG_INFO,
    "RK3576SimpleFb: Alternate registers -"
    " WIN0_YRGB_MST[0x%08x]=0x%08x  WIN0_DSP_INFO[0x%08x]=0x%08x\n",
    (UINT32)RK3576_WIN0_YRGB_MST_ALT,
    YrgbMst,
    (UINT32)RK3576_WIN0_DSP_INFO_ALT,
    DspInfo
    ));

  if ((YrgbMst != 0) && (DspInfo != 0)) {
    W = (DspInfo & 0xFFFFU) + 1;
    H = ((DspInfo >> 16) & 0xFFFFU) + 1;
    if ((W >= FB_MIN_WIDTH)  && (W <= FB_MAX_WIDTH) &&
        (H >= FB_MIN_HEIGHT) && (H <= FB_MAX_HEIGHT))
    {
      *FbAddr = (EFI_PHYSICAL_ADDRESS)YrgbMst;
      *Width  = W;
      *Height = H;
      DEBUG ((
        DEBUG_INFO,
        "RK3576SimpleFb: Using alternate regs -> FB=0x%lx %ux%u\n",
        *FbAddr, *Width, *Height
        ));
      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
EFIAPI
RK3576SimpleFbEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE            GopHandle;
  EFI_PHYSICAL_ADDRESS  FbBase;
  UINT32                FbWidth;
  UINT32                FbHeight;
  UINT32                FbStride;
  UINTN                 FbSize;
  EFI_PHYSICAL_ADDRESS  AllocBase;
  BOOLEAN               AllocatedFb;

  AllocatedFb = FALSE;

  DEBUG ((DEBUG_INFO, "RK3576SimpleFb: entry\n"));

  /* Check if GOP is already installed — if so, nothing to do. */
  {
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *ExistingGop = NULL;
    Status = gBS->LocateProtocol (
                    &gEfiGraphicsOutputProtocolGuid,
                    NULL,
                    (VOID **)&ExistingGop
                    );
    if (!EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_INFO,
        "RK3576SimpleFb: GOP already installed (%ux%u) — skipping\n",
        ExistingGop->Mode->Info->HorizontalResolution,
        ExistingGop->Mode->Info->VerticalResolution
        ));
      return EFI_ALREADY_STARTED;
    }
  }

  /* Try to read the framebuffer address and resolution from VOP2. */
  if (!ReadVop2State (&FbBase, &FbWidth, &FbHeight)) {
    /*
     * No valid VOP2 state found (VOP2 not initialised by U-Boot, or
     * register layout is different from expected).  Allocate a fresh
     * framebuffer and program the primary WIN0_YRGB_MST register.
     */
    FbWidth  = FB_DEFAULT_WIDTH;
    FbHeight = FB_DEFAULT_HEIGHT;
    FbStride = FbWidth * FB_BYTES_PER_PIXEL;
    FbSize   = (UINTN)FbHeight * FbStride;

    AllocBase = 0;              /* let AllocatePages choose address */
    Status = gBS->AllocatePages (
                    AllocateAnyPages,
                    EfiBootServicesData,
                    EFI_SIZE_TO_PAGES (FbSize),
                    &AllocBase
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "RK3576SimpleFb: AllocatePages failed: %r\n",
        Status
        ));
      return Status;
    }

    FbBase       = AllocBase;
    AllocatedFb  = TRUE;
    ZeroMem ((VOID *)(UINTN)FbBase, FbSize);

    /* Tell VOP2 to scan out from our new framebuffer. */
    MmioWrite32 (RK3576_WIN0_YRGB_MST_ADDR, (UINT32)FbBase);

    DEBUG ((
      DEBUG_INFO,
      "RK3576SimpleFb: Allocated FB at 0x%lx size 0x%lx, "
      "programmed WIN0_YRGB_MST=0x%08x\n",
      FbBase,
      (UINT64)FbSize,
      (UINT32)FbBase
      ));
  }

  FbStride = FbWidth * FB_BYTES_PER_PIXEL;

  /* Populate the mode information structure. */
  ZeroMem (&mModeInfo, sizeof (mModeInfo));
  mModeInfo.Version              = 0;
  mModeInfo.HorizontalResolution = FbWidth;
  mModeInfo.VerticalResolution   = FbHeight;
  mModeInfo.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mModeInfo.PixelsPerScanLine    = FbWidth;

  ZeroMem (&mGopMode, sizeof (mGopMode));
  mGopMode.MaxMode         = 1;
  mGopMode.Mode            = 0;
  mGopMode.Info            = &mModeInfo;
  mGopMode.SizeOfInfo      = sizeof (mModeInfo);
  mGopMode.FrameBufferBase = FbBase;
  mGopMode.FrameBufferSize = (UINTN)FbHeight * FbStride;

  ZeroMem (&mGop, sizeof (mGop));
  mGop.QueryMode = SimpleFbQueryMode;
  mGop.SetMode   = SimpleFbSetMode;
  mGop.Blt       = SimpleFbBlt;
  mGop.Mode      = &mGopMode;

  /* Install GOP on a new handle. */
  GopHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &GopHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "RK3576SimpleFb: InstallMultipleProtocolInterfaces failed: %r\n",
      Status
      ));
    if (AllocatedFb) {
      gBS->FreePages (FbBase, EFI_SIZE_TO_PAGES (mGopMode.FrameBufferSize));
    }
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "RK3576SimpleFb: GOP installed — %ux%u FB @ 0x%lx stride %u\n",
    FbWidth,
    FbHeight,
    FbBase,
    FbStride
    ));

  return EFI_SUCCESS;
}
