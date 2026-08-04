/** @file
 *
 *  Native RK3576 VOP2 video-port register computation.
 *
 *  This header and its .c deliberately contain no MMIO and no driver state:
 *  they turn a mode plus a connector configuration into the exact values the
 *  RK3576 video-port registers should hold. The driver then writes each of
 *  those registers once.
 *
 *  That split is the point of the file. The RK3588-derived path this replaces
 *  reached the same values by read-modify-writing one bit-field at a time,
 *  which meant VP_DSP_CTRL alone was written thirteen times per bring-up, each
 *  write landing on a live register and briefly presenting the video port with
 *  a configuration nobody intended. Mainline builds the value in a local and
 *  writes it once (rockchip_drm_vop2.c, vop2_crtc_atomic_enable); so does this.
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef VOP2_RK3576_H__
#define VOP2_RK3576_H__

#include <Uefi.h>
#include <Library/RockchipDisplayLib.h>

///
/// Every RK3576 video-port register this driver programs, as a complete value.
/// One field per register, so a caller cannot accidentally write a partially
/// computed one.
///
typedef struct {
  ///
  /// VP_DSP_CTRL with STANDBY (bit 31) set: the value to install while the
  /// port is still quiet.
  ///
  UINT32     DspCtrlStandby;
  ///
  /// The same value with STANDBY clear: what releases the port. Writing this
  /// is the only VP_DSP_CTRL write that should follow configuration.
  ///
  UINT32     DspCtrlActive;

  UINT32     HTotalHsEnd;      ///< VP_DSP_HTOTAL_HS_END
  UINT32     HActStEnd;        ///< VP_DSP_HACT_ST_END
  UINT32     VTotalVsEnd;      ///< VP_DSP_VTOTAL_VS_END
  UINT32     VActStEnd;        ///< VP_DSP_VACT_ST_END
  UINT32     MipiCtrl;         ///< VP_MIPI_CTRL
  UINT32     DspBg;            ///< VP_DSP_BG
  UINT32     LineFlag;         ///< VP_LINE_FLAG (indexed by CRTC, not VP offset)

  ///
  /// TRUE when the mode is interlaced, in which case the caller must also
  /// program VACT_ST_END_F1 and VS_ST_END_F1 from the two fields below.
  ///
  BOOLEAN    Interlaced;
  UINT32     VActStEndF1;      ///< VP_DSP_VACT_ST_END_F1, valid if Interlaced
  UINT32     VsStEndF1;        ///< VP_DSP_VS_ST_END_F1,   valid if Interlaced
} RK3576_VP_REGS;

/**
  Compute every RK3576 video-port register value for the given display state.

  Performs no hardware access. The mode is read from
  DisplayState->ConnectorState.DisplayMode and must already have been fixed up
  (Crtc* fields populated).

  @param[in]  DisplayState  Display state carrying the mode and connector.
  @param[out] Regs          Receives the complete register values.

  @retval EFI_SUCCESS            Values computed.
  @retval EFI_INVALID_PARAMETER  DisplayState or Regs was NULL.
  @retval EFI_UNSUPPORTED        The mode's timings do not fit the register
                                 fields, so no partial result is returned.
**/
EFI_STATUS
Rk3576VpComputeRegs (
  IN  DISPLAY_STATE   *DisplayState,
  OUT RK3576_VP_REGS  *Regs
  );

#endif /* VOP2_RK3576_H__ */
