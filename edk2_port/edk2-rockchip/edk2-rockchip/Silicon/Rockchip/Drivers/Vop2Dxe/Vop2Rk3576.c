/** @file
 *
 *  Native RK3576 VOP2 video-port register computation.
 *
 *  Reference: linux/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c,
 *  vop2_crtc_atomic_enable() and vop2_dither_setup(), plus the RK3576 data in
 *  rockchip_vop2_reg.c. Mainline is the only reference used here; the vendor
 *  U-Boot VOP2 code disagrees with it in places and is not consulted.
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/Vop2Regs.h>
#include <Library/MediaBusFormat.h>
#include <Library/DrmModes.h>

#include "Vop2Rk3576.h"

//
// Mirrors VOP_FEATURE_OUTPUT_10BIT from Vop2Dxe.h. Duplicated on purpose: this
// module is a pure computation over display state and deliberately does not
// include the driver's own header, which drags in the VOP2 context struct and
// the register-shadow cache. One constant is a cheaper coupling than that.
//
#define RK3576_VP_FEATURE_OUTPUT_10BIT  BIT0

//
// VP_DSP_CTRL bit-fields, named as mainline names them
// (RK3568_VP_DSP_CTRL__*). The RK3576 video port uses the RK3568 layout for
// this register; only its base offset moved.
//
#define RK3576_DSP_CTRL_OUT_MODE_SHIFT           0
#define RK3576_DSP_CTRL_OUT_MODE_MASK            0xFU
#define RK3576_DSP_CTRL_CORE_DCLK_DIV            BIT4
#define RK3576_DSP_CTRL_P2I_EN                   BIT5
#define RK3576_DSP_CTRL_DSP_FILED_POL            BIT6
#define RK3576_DSP_CTRL_DSP_INTERLACE            BIT7
#define RK3576_DSP_CTRL_DATA_SWAP_SHIFT          8
#define RK3576_DSP_CTRL_DATA_SWAP_MASK           0x1FU
#define RK3576_DSP_CTRL_POST_DSP_OUT_R2Y         BIT15
#define RK3576_DSP_CTRL_PRE_DITHER_DOWN_EN       BIT16
#define RK3576_DSP_CTRL_DITHER_DOWN_EN           BIT17
#define RK3576_DSP_CTRL_DITHER_DOWN_SEL_SHIFT    18
#define RK3576_DSP_CTRL_DITHER_DOWN_SEL_MASK     0x3U
#define RK3576_DSP_CTRL_DITHER_DOWN_MODE         BIT20
#define RK3576_DSP_CTRL_STANDBY                  BIT31

//
// DITHER_DOWN_ALLEGRO is 0 in mainline (rockchip_drm_vop.h), so selecting it
// contributes nothing to the register value. Spelled out anyway, because a
// zero that is a deliberate choice reads differently from a zero that is an
// oversight.
//
#define RK3576_DITHER_DOWN_ALLEGRO               0U

//
// VP_MIPI_CTRL, DCLK_DIV2 field.
//
#define RK3576_MIPI_CTRL_DCLK_DIV2_SHIFT         4
#define RK3576_MIPI_CTRL_DCLK_DIV2_MASK          0x3U

//
// Field values written into DATA_SWAP.
//
#define RK3576_DATA_SWAP_RB                      0x2U
#define RK3576_DATA_SWAP_RG                      0x4U

/**
  Does this bus format carry YUV?
**/
STATIC
BOOLEAN
Rk3576BusFormatIsYuv (
  IN UINT32  BusFormat
  )
{
  switch (BusFormat) {
    case MEDIA_BUS_FMT_YUV8_1X24:
    case MEDIA_BUS_FMT_YUV10_1X30:
    case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
    case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
    case MEDIA_BUS_FMT_YUYV8_2X8:
    case MEDIA_BUS_FMT_YVYU8_2X8:
    case MEDIA_BUS_FMT_UYVY8_2X8:
    case MEDIA_BUS_FMT_VYUY8_2X8:
    case MEDIA_BUS_FMT_YUYV8_1X16:
    case MEDIA_BUS_FMT_YVYU8_1X16:
    case MEDIA_BUS_FMT_UYVY8_1X16:
    case MEDIA_BUS_FMT_VYUY8_1X16:
      return TRUE;
    default:
      return FALSE;
  }
}

/**
  Does the output need R and B swapped?

  There is no media bus format for YUV444, so an out_mode of AAAA or P888 on a
  YUV bus format is taken to mean YUV444, which hardware testing shows needs
  the swap. Same reasoning as mainline's vop2_output_uv_swap().
**/
STATIC
BOOLEAN
Rk3576NeedsRbSwap (
  IN UINT32  BusFormat,
  IN UINT32  OutputMode
  )
{
  if (((BusFormat == MEDIA_BUS_FMT_YUV8_1X24) ||
       (BusFormat == MEDIA_BUS_FMT_YUV10_1X30)) &&
      ((OutputMode == ROCKCHIP_OUT_MODE_AAAA) ||
       (OutputMode == ROCKCHIP_OUT_MODE_P888)))
  {
    return TRUE;
  }

  return FALSE;
}

/**
  Accumulate the dither bits.

  Mirrors mainline vop2_dither_setup(), with one deliberate difference noted
  inline. Takes DspCtrl by pointer for the same reason mainline does: dither is
  one contributor among several to a single register value.
**/
STATIC
VOID
Rk3576DitherSetup (
  IN     CONNECTOR_STATE  *ConnectorState,
  IN OUT UINT32           *DspCtrl
  )
{
  switch (ConnectorState->BusFormat) {
    case MEDIA_BUS_FMT_RGB565_1X16:
      *DspCtrl |= RK3576_DSP_CTRL_DITHER_DOWN_EN;
      break;

    case MEDIA_BUS_FMT_RGB666_1X18:
    case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
    case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
    case MEDIA_BUS_FMT_RGB666_1X7X3_JEIDA:
      *DspCtrl |= RK3576_DSP_CTRL_DITHER_DOWN_EN;
      break;

    case MEDIA_BUS_FMT_YUV8_1X24:
    case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
      //
      // 10-bit internal pipeline feeding an 8-bit YUV bus: pre-dither is what
      // that truncation is for.
      //
      *DspCtrl |= RK3576_DSP_CTRL_PRE_DITHER_DOWN_EN;
      break;

    default:
      break;
  }

  //
  // Mainline additionally sets PRE_DITHER_DOWN_EN for every output_mode that
  // is not AAAA. This driver does not, and the difference is deliberate.
  //
  // Our HDMI path runs RGB888_1X24 out through P888, where source and output
  // are both 8 bits per component. Enabling pre-dither there dithers a signal
  // that needs no truncation, which shows up as a visible pattern on flat
  // colour. Mainline gets away with it because it drives AAAA (10-bit) on
  // this path and only falls back to P888 when the port cannot do 10-bit.
  //
  // The switch above already turns pre-dither on for the case that actually
  // needs it -- a wider internal pipeline than the output bus.
  //
  // If this is ever revisited, revisit it with a measurement: set the bit,
  // photograph a flat mid-grey field, and compare. It has been asserted in
  // both directions in this tree without one.
  //
  *DspCtrl |= (RK3576_DITHER_DOWN_ALLEGRO & RK3576_DSP_CTRL_DITHER_DOWN_SEL_MASK)
              << RK3576_DSP_CTRL_DITHER_DOWN_SEL_SHIFT;
}

EFI_STATUS
Rk3576VpComputeRegs (
  IN  DISPLAY_STATE   *DisplayState,
  OUT RK3576_VP_REGS  *Regs
  )
{
  CONNECTOR_STATE   *ConnectorState;
  CRTC_STATE        *CrtcState;
  DRM_DISPLAY_MODE  *Mode;
  UINT32            DspCtrl;
  UINT32            DataSwap;
  UINT32            OutputMode;
  UINT32            HSyncLen, HDisplay, HTotal, HActStart, HActEnd;
  UINT32            VSyncLen, VDisplay, VTotal, VActStart, VActEnd;
  UINT32            ActEnd;

  if ((DisplayState == NULL) || (Regs == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ConnectorState = &DisplayState->ConnectorState;
  CrtcState      = &DisplayState->CrtcState;
  Mode           = &ConnectorState->DisplayMode;

  ZeroMem (Regs, sizeof (*Regs));

  //
  // Timing. Same derivation as mainline: the active window is expressed
  // relative to the end of the sync pulse, so the start is total minus
  // sync-start rather than sync-end plus back porch.
  //
  HSyncLen  = (UINT32)(Mode->CrtcHSyncEnd - Mode->CrtcHSyncStart);
  HDisplay  = (UINT32)Mode->CrtcHDisplay;
  HTotal    = (UINT32)Mode->CrtcHTotal;
  HActStart = (UINT32)(Mode->CrtcHTotal - Mode->CrtcHSyncStart);
  HActEnd   = HActStart + HDisplay;

  VSyncLen  = (UINT32)(Mode->CrtcVSyncEnd - Mode->CrtcVSyncStart);
  VDisplay  = (UINT32)Mode->CrtcVDisplay;
  VTotal    = (UINT32)Mode->CrtcVTotal;
  VActStart = (UINT32)(Mode->CrtcVTotal - Mode->CrtcVSyncStart);
  VActEnd   = VActStart + VDisplay;

  //
  // Every one of these packs two values into a 32-bit register as 16-bit
  // halves. Refuse the mode rather than silently truncating it -- a wrapped
  // htotal produces a picture that is subtly wrong rather than absent, which
  // is the expensive kind of failure to debug.
  //
  if ((HTotal > 0xFFFF) || (HActEnd > 0xFFFF) || (HSyncLen > 0xFFFF) ||
      (VTotal > 0xFFFF) || (VActEnd > 0xFFFF) || (VSyncLen > 0xFFFF))
  {
    DEBUG ((
      DEBUG_ERROR,
      "%a: mode %ux%u does not fit the VP timing registers "
      "(htotal=%u hact_end=%u vtotal=%u vact_end=%u)\n",
      __func__,
      HDisplay,
      VDisplay,
      HTotal,
      HActEnd,
      VTotal,
      VActEnd
      ));
    return EFI_UNSUPPORTED;
  }

  Regs->HTotalHsEnd = (HTotal << 16) | HSyncLen;
  Regs->HActStEnd   = (HActStart << 16) | HActEnd;
  Regs->VActStEnd   = (VActStart << 16) | VActEnd;

  //
  // Output mode. A port without the 10-bit feature cannot do AAAA, so fold it
  // down to P888 here rather than leaving the caller to notice.
  //
  OutputMode = ConnectorState->OutputMode;
  if ((OutputMode == ROCKCHIP_OUT_MODE_AAAA) &&
      !(CrtcState->Feature & RK3576_VP_FEATURE_OUTPUT_10BIT))
  {
    OutputMode = ROCKCHIP_OUT_MODE_P888;
  }

  DspCtrl = (OutputMode & RK3576_DSP_CTRL_OUT_MODE_MASK)
            << RK3576_DSP_CTRL_OUT_MODE_SHIFT;

  //
  // Channel swaps.
  //
  DataSwap = 0;
  if (Rk3576NeedsRbSwap (ConnectorState->BusFormat, OutputMode)) {
    DataSwap |= RK3576_DATA_SWAP_RB;
  }

  //
  // RG swap is an RK3588 erratum workaround (mainline gates it on
  // vop2_output_rg_swap(), which is true only for RK3588 with a YUV bus
  // format on HDMI/DP). RK3576 does not need it, so it is not applied here.
  //

  DspCtrl |= (DataSwap & RK3576_DSP_CTRL_DATA_SWAP_MASK)
             << RK3576_DSP_CTRL_DATA_SWAP_SHIFT;

  //
  // RGB-to-YUV conversion on the way out, when the overlay is RGB but the bus
  // carries YUV.
  //
  if (Rk3576BusFormatIsYuv (ConnectorState->BusFormat)) {
    DspCtrl |= RK3576_DSP_CTRL_POST_DSP_OUT_R2Y;
  }

  Rk3576DitherSetup (ConnectorState, &DspCtrl);

  //
  // Interlace.
  //
  if ((Mode->Flags & DRM_MODE_FLAG_INTERLACE) != 0) {
    UINT32  VActStartF1 = VTotal + VActStart + 1;
    UINT32  VActEndF1   = VActStartF1 + VDisplay;

    if ((VActEndF1 > 0xFFFF) || ((VTotal + VSyncLen) > 0xFFFF)) {
      DEBUG ((DEBUG_ERROR, "%a: interlaced field-1 timing overflows\n", __func__));
      return EFI_UNSUPPORTED;
    }

    Regs->Interlaced  = TRUE;
    Regs->VActStEndF1 = (VActStartF1 << 16) | VActEndF1;
    Regs->VsStEndF1   = (VTotal << 16) | (VTotal + VSyncLen);

    DspCtrl |= RK3576_DSP_CTRL_DSP_INTERLACE;
    DspCtrl |= RK3576_DSP_CTRL_DSP_FILED_POL;
    DspCtrl |= RK3576_DSP_CTRL_P2I_EN;

    //
    // Mainline doubles vtotal for the interlaced case after deriving the line
    // flag from field 1's end.
    //
    ActEnd  = VActEndF1;
    VTotal += VTotal + 1;
  } else {
    ActEnd = VActEnd;
  }

  if ((VTotal > 0xFFFF)) {
    DEBUG ((DEBUG_ERROR, "%a: vtotal %u overflows after interlace adjust\n", __func__, VTotal));
    return EFI_UNSUPPORTED;
  }

  Regs->VTotalVsEnd = (VTotal << 16) | VSyncLen;

  //
  // Double-clocked modes halve the core clock and are flagged in DSP_CTRL.
  //
  if ((Mode->Flags & DRM_MODE_FLAG_DBLCLK) != 0) {
    DspCtrl |= RK3576_DSP_CTRL_CORE_DCLK_DIV;
  }

  //
  // VP_MIPI_CTRL carries a DCLK divider used only for YUV420 output. Mainline
  // writes the whole register as zero here; this driver programs the field
  // explicitly so the intent survives a future addition to the register.
  //
  Regs->MipiCtrl = 0;
  if (OutputMode == ROCKCHIP_OUT_MODE_YUV420) {
    Regs->MipiCtrl |= (0x3U & RK3576_MIPI_CTRL_DCLK_DIV2_MASK)
                      << RK3576_MIPI_CTRL_DCLK_DIV2_SHIFT;
  }

  //
  // Background colour. Black; the framebuffer covers the active area and any
  // visible background would be a bug elsewhere.
  //
  Regs->DspBg = 0;

  //
  // Line flag. Mainline subtracts a vertical-blanking margin expressed in
  // microseconds; with no timer plumbed through here the margin is zero, which
  // makes both halves the end of the active area. That only affects when the
  // line interrupt fires, and nothing in this firmware consumes it.
  //
  Regs->LineFlag = (ActEnd << 16) | ActEnd;

  //
  // Two values, differing only in STANDBY, so the caller never has to
  // construct one from the other and never has to read the register back.
  //
  Regs->DspCtrlActive  = DspCtrl;
  Regs->DspCtrlStandby = DspCtrl | RK3576_DSP_CTRL_STANDBY;

  DEBUG ((
    DEBUG_INFO,
    "%a: VP%u %ux%u%a out_mode=%u dsp_ctrl=0x%08x "
    "htotal_hs=0x%08x hact=0x%08x vtotal_vs=0x%08x vact=0x%08x\n",
    __func__,
    CrtcState->CrtcID,
    HDisplay,
    VDisplay,
    Regs->Interlaced ? "i" : "p",
    OutputMode,
    Regs->DspCtrlActive,
    Regs->HTotalHsEnd,
    Regs->HActStEnd,
    Regs->VTotalVsEnd,
    Regs->VActStEnd
    ));

  return EFI_SUCCESS;
}
