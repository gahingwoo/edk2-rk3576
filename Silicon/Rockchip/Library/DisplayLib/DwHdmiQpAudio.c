/** @file
 *
 *  HDMI audio preset for RK3576: a fixed 48 kHz, 2-channel L-PCM
 *  configuration of the DW-HDMI-QP audio path, and the SAI6 master clock that
 *  feeds it, programmed once at ExitBootServices.
 *
 *  Why it is here.  Windows has no driver for the HDMI controller: the
 *  display driver only flips the VOP2 plane, and nothing else touches the
 *  controller after the firmware hands over.  So whoever wants HDMI audio
 *  under Windows has to find the controller already carrying it.  With the
 *  controller set up here, the audio driver only has to run SAI6 as an I2S
 *  bit-clock master at 48 kHz and feed it by DMA.  The endpoint is
 *  fixed-format, which is what csaudiork3x's "hdmi" topology already is.
 *
 *  The hardware path, from mainline Linux (rk3576.dtsi):
 *
 *    hdmi_sound: simple-audio-card, format "i2s", mclk-fs = <256>,
 *      cpu = <&sai6>, codec = <&hdmi>.
 *    sai6: sai@27d50000, GIC SPI 193, clocks MCLK_SAI6_8CH, HCLK_SAI6_8CH,
 *      dmas = <&dmac2 4>, <&dmac2 5> (tx, rx), power domain PD_VO0.
 *    hdmi: hdmi@27da0000, whose "aud" clock is MCLK_SAI6_8CH.
 *
 *  The SAI6 output reaches the HDMI controller inside the SoC once
 *  VO0_GRF_SOC_CON14 I2S_SEL is set, which DwHdmiQpSetIomux already does.
 *
 *  What is written, in this order, and nothing else:
 *
 *  CRU (base 0x27200000), all from mainline clk-rk3576.c and checked against
 *  the RK3576 TRM V1.2 Part 1:
 *
 *    0x0334 CLKSEL_CON13   clk_audio_frac_0 source: 0 = GPLL, 1 = CPLL
 *    0x0330 CLKSEL_CON12   clk_audio_frac_0 = (num << 16) | den, 12.288 MHz
 *                          (GPLL 1188 MHz: 128 / 12375, exact)
 *    0x0804 GATE_CON01     ungate clk_audio_frac_0 (bit 10)
 *    0x056C CLKSEL_CON155  mclk_sai6_src = clk_audio_frac_0 / 1,
 *                          mclk_sai6 = mclk_sai6_src
 *    0x0904 GATE_CON65     ungate mclk_sai6_src, mclk_sai6, hclk_sai6
 *                          (bits 7, 8, 9)
 *
 *  No PLL is reprogrammed, no reset is touched, and the audio PLL is not
 *  used: its rate at this point is whatever the SPL left, which nothing here
 *  can vouch for.  The parent rate is read back from the PLL registers and
 *  the fraction is used only if it is exact.
 *
 *  DW-HDMI-QP (base 0x27DA0000), mainline dw-hdmi-qp.c audio_startup,
 *  audio_prepare and write_audio_infoframe, for 2 channels at 48 kHz with
 *  24-bit samples in 32-bit I2S slots:
 *
 *    0x0044 GLOBAL_SWDISABLE        clear AVP_DATAPATH_PACKET_AUDIO_SWDISABLE
 *                                   (bit 10), read-modify-write
 *    0x0040 GLOBAL_SWRESET_REQUEST  AVP_DATAPATH_PACKET_AUDIO_SWINIT_P (bit 10,
 *                                   self-clearing)
 *    0x0AA8 PKTSCHED_PKT_EN         clear ACR, AUDS, AMD, AUDI, read-modify-write
 *    0x082C AUDIO_INTERFACE_CONTROL0  AUDIO_FIFO_CLR_P
 *    0x0820 AUDIO_INTERFACE_CONFIG0   0x00000015: I2S, FIFO init on overflow,
 *                                     I2S line 0, BPCUV internal, ASP
 *    0x0E40 AUDPKT_ACR_CONTROL0     N = 6144
 *    0x0E44 AUDPKT_ACR_CONTROL1     CTS override = TMDS / 1000, CTS_OVR_EN
 *    0x0E60 AUDPKT_CHSTATUS_OVR0    0x00000004 (CS0 not copyright, CS1 general)
 *    0x0E64 AUDPKT_CHSTATUS_OVR1    0x00000B02 (CS3 48 kHz, CS4 24-bit)
 *    0x0E20 AUDPKT_CONTROL0         0x00001001 (PBIT_FORCE_EN, CHSTATUS_OVR_EN)
 *    0x0C20 PKT_AUDI_CONTENTS0      0x000A0100 (HB1 version 1, HB2 length 10)
 *    0x0C24 PKT_AUDI_CONTENTS1      0x00000170 (PB0 checksum, PB1 CC = 2 ch)
 *    0x0C28 PKT_AUDI_CONTENTS2      0 (CA 0: FL FR)
 *    0x0C2C PKT_AUDI_CONTENTS3      0
 *    0x0AA8 PKTSCHED_PKT_EN         set ACR, AUDI, then AUDS, read-modify-write
 *
 *  Two deliberate differences from mainline.  AMD (audio metadata) is not
 *  enabled: mainline sets it next to ACR and AUDI, the vendor kernel does not,
 *  and the packet only carries 3D and multi-stream audio metadata, which a
 *  2-channel stream has none of.  And the values are written whole instead of
 *  read-modify-written, because every field other than the ones mainline sets
 *  is zero at reset (TRM V1.2 Part 2) and nothing else in this firmware writes
 *  these registers.  The two RMWs that remain are on registers the display
 *  setup already read-modify-writes itself.
 *
 *  Why CTS is overridden rather than measured: with the override the ACR
 *  packets depend only on the TMDS rate, so they stay valid while the SAI is
 *  stopped, between streams, and before Windows loads a driver at all.  For
 *  48 kHz N is 6144 at every TMDS rate mainline's table lists, and then
 *  CTS = f_TMDS * N / (128 * 48000) = f_TMDS / 1000 exactly: 148500 at
 *  148.5 MHz (1080p60), 241500 at 241.5 MHz (2560x1440@60).
 *
 *  Nothing here reads or writes a VOP2 register, and nothing runs inside the
 *  display bring-up: DwHdmiQpConnectorEnable only records the link it ended
 *  with, and the writes happen at ExitBootServices, after the last mode the
 *  boot loader may have set.
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/RockchipDisplayLib.h>
#include <Library/DwHdmiQpLib.h>
#include <Guid/EventGroup.h>

#include <VarStoreData.h>

//
// CRU, RK3576 TRM V1.2 Part 1.
//
#define AUD_CRU_BASE             0x27200000UL
#define AUD_CRU_GPLL_CON0        (AUD_CRU_BASE + 0x01C0)   // PLL_CON(112)
#define AUD_CRU_CPLL_CON0        (AUD_CRU_BASE + 0x01A0)   // PLL_CON(104)
#define AUD_CRU_MODE_CON00       (AUD_CRU_BASE + 0x0280)
#define AUD_CRU_GPLL_MODE_SHIFT  2
#define AUD_CRU_CPLL_MODE_SHIFT  8
#define AUD_CRU_CLKSEL_CON12     (AUD_CRU_BASE + 0x0330)   // audio_frac_0 num/den
#define AUD_CRU_CLKSEL_CON13     (AUD_CRU_BASE + 0x0334)   // audio_frac_0 source
#define AUD_CRU_GATE_CON01       (AUD_CRU_BASE + 0x0804)
#define AUD_CRU_CLKSEL_CON155    (AUD_CRU_BASE + 0x056C)   // mclk_sai6
#define AUD_CRU_GATE_CON65       (AUD_CRU_BASE + 0x0904)

#define AUD_OSC_HZ               24000000U

#define AUD_FRAC0_SEL_GPLL       0
#define AUD_FRAC0_SEL_CPLL       1
#define AUD_FRAC0_GATE           BIT10                     // GATE_CON01
#define AUD_SAI6_SRC_SEL_FRAC0   1                         // CLKSEL_CON155[10:8]
#define AUD_SAI6_GATES           (BIT7 | BIT8 | BIT9)      // GATE_CON65

#define AUD_HIWORD(Mask, Val)    (((UINT32)(Mask) << 16) | (UINT32)(Val))

//
// The stream this sets up.  mclk-fs = 256 is what hdmi_sound asks for.
//
#define AUD_RATE_HZ              48000U
#define AUD_MCLK_HZ              (256U * AUD_RATE_HZ)      // 12.288 MHz
#define AUD_ACR_N                6144U

//
// DW-HDMI-QP fields the shared header does not define.
//
#ifndef AUDPKT_PBIT_FORCE_EN
#define AUDPKT_PBIT_FORCE_EN     BIT(12)
#endif

#define AUD_IF_CONFIG0_VALUE     (AUD_IF_I2S | AUD_FIFO_INIT_ON_OVF_EN | \
                                  I2S_LINES_EN (0) | I2S_BPCUV_RCV_DIS | AUD_ASP)

//
// IEC 60958 consumer channel status, as snd_pcm_create_iec958_consumer()
// fills it for 48 kHz and a 24-bit sample (Linux sound/core/pcm_iec958.c):
//   CS0 = IEC958_AES0_CON_NOT_COPYRIGHT (0x04)
//   CS1 = IEC958_AES1_CON_GENERAL (0x00)
//   CS3 = IEC958_AES3_CON_FS_48000 (0x02), clock accuracy level II
//   CS4 = IEC958_AES4_CON_WORDLEN_24_20 | IEC958_AES4_CON_MAX_WORDLEN_24 (0x0B)
// OVR0 holds { RSV, RSV, CS1, CS0 } and OVR1 { CS6, CS5, CS4, CS3 }.
//
#define AUD_CHSTATUS_OVR0        0x00000004U
#define AUD_CHSTATUS_OVR1        0x00000B02U

//
// Audio InfoFrame, as hdmi_audio_infoframe_pack() builds it for 2 channels
// with everything else "refer to stream header": HB0 0x84, HB1 0x01,
// HB2 0x0A, PB1 = channels - 1 = 0x01, PB2..PB10 = 0.  The checksum makes
// the 3 header bytes and 11 payload bytes sum to 0 mod 256:
// 0x100 - (0x84 + 0x01 + 0x0A + 0x01) = 0x70.
//
// Laid out the way dw_hdmi_qp_write_infoframe() writes it:
//   AUDI_CONTENTS0 { RSV, HB2, HB1, RSV }, AUDI_CONTENTS1 { PB3..PB0 }, ...
//
#define AUD_AUDI_CONTENTS0       0x000A0100U
#define AUD_AUDI_CONTENTS1       0x00000170U
#define AUD_AUDI_CONTENTS2       0x00000000U
#define AUD_AUDI_CONTENTS3       0x00000000U

#define AUD_PKT_ALL_AUDIO        (PKTSCHED_ACR_TX_EN | PKTSCHED_AUDS_TX_EN | \
                                  PKTSCHED_AMD_TX_EN | PKTSCHED_AUDI_TX_EN)

//
// What DwHdmiQpConnectorEnable ended with.  Copied, not pointed at: the
// display state belongs to the GOP driver.
//
typedef struct {
  BOOLEAN    Valid;
  UINTN      Base;
  UINT32     ClockKhz;
  UINT32     HDisplay;
  UINT32     VDisplay;
  UINT32     VRefresh;
} DW_HDMI_QP_AUDIO_LINK;

STATIC DW_HDMI_QP_AUDIO_LINK  mAudioLink;
STATIC EFI_EVENT              mAudioExitBootServicesEvent = NULL;

/**
  Whether the sink's EDID declares basic audio: a CEA-861 extension (tag
  0x02, revision 2 or later) with byte 3 bit 6 set.  That bit means the sink
  accepts 2-channel L-PCM at 32, 44.1 and 48 kHz, which is all this sends.
**/
STATIC
BOOLEAN
DwHdmiQpAudioSinkHasBasicAudio (
  IN CONST UINT8  *Edid
  )
{
  UINTN  Extensions;
  UINTN  Index;

  Extensions = Edid[126];
  if (Extensions > EDID_MAX_EXTENSION_BLOCKS) {
    Extensions = EDID_MAX_EXTENSION_BLOCKS;
  }

  for (Index = 1; Index <= Extensions; Index++) {
    CONST UINT8  *Block = Edid + Index * EDID_BLOCK_SIZE;

    if ((Block[0] == 0x02) && (Block[1] >= 2) && ((Block[3] & BIT6) != 0)) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
DwHdmiQpAudioNoteLink (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN DISPLAY_STATE          *DisplayState,
  IN EFI_STATUS             Status
  )
{
  CONNECTOR_STATE  *ConnectorState;

  if (!FixedPcdGetBool (PcdHdmiAudioPreset)) {
    return;
  }

  ConnectorState   = &DisplayState->ConnectorState;
  mAudioLink.Valid = FALSE;

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "HdmiAudio: link not up (%r), no audio\n", Status));
    return;
  }

  //
  // Data islands are only allowed on an HDMI link.  DwHdmiQpSetup runs the
  // link as DVI only on an explicit user choice; a sink with no HDMI VSDB is
  // a DVI sink whatever the link mode is.
  //
  if ((Hdmi->SignalingMode == HDMI_SIGNALING_MODE_DVI) || !ConnectorState->SinkInfo.IsHdmi) {
    DEBUG ((DEBUG_INFO, "HdmiAudio: DVI link or DVI sink, no audio\n"));
    return;
  }

  if (!DwHdmiQpAudioSinkHasBasicAudio (ConnectorState->Edid)) {
    DEBUG ((DEBUG_INFO, "HdmiAudio: the sink's EDID declares no basic audio, no audio\n"));
    return;
  }

  mAudioLink.Base     = Hdmi->Base;
  mAudioLink.ClockKhz = ConnectorState->DisplayMode.Clock;
  mAudioLink.HDisplay = ConnectorState->DisplayMode.HDisplay;
  mAudioLink.VDisplay = ConnectorState->DisplayMode.VDisplay;
  mAudioLink.VRefresh = ConnectorState->DisplayMode.VRefresh;
  mAudioLink.Valid    = TRUE;
}

/**
  Rate of an RK3576 integer/fractional PLL from its registers, as
  DwcSdhciPlatformLib reads it: Fout = 24 MHz * (M + K / 65536) / (P * 2^S).
  Returns 0 when the PLL is bypassed or the registers make no sense.
**/
STATIC
UINT32
DwHdmiQpAudioPllRate (
  IN UINTN  Con0,
  IN UINTN  ModeShift
  )
{
  UINT32  Con0Val;
  UINT32  Con1Val;
  UINT32  M;
  UINT32  P;
  UINT32  S;
  INT16   K;
  UINT64  Rate;

  if (((MmioRead32 (AUD_CRU_MODE_CON00) >> ModeShift) & 0x3) != 1) {
    return 0;
  }

  Con0Val = MmioRead32 (Con0);
  Con1Val = MmioRead32 (Con0 + 4);
  K       = (INT16)(MmioRead32 (Con0 + 8) & 0xFFFF);

  if ((Con0Val & BIT15) != 0) {
    return 0;
  }

  M = Con0Val & 0x3FF;
  P = Con1Val & 0x3F;
  S = (Con1Val >> 6) & 0x7;
  if ((M == 0) || (P == 0)) {
    return 0;
  }

  Rate = MultU64x32 ((UINT64)AUD_OSC_HZ, M);
  if (K != 0) {
    Rate += DivS64x64Remainder (MultS64x64 ((INT64)AUD_OSC_HZ, K), 65536, NULL);
  }

  return (UINT32)DivU64x32 (Rate, P << S);
}

STATIC
UINT32
DwHdmiQpAudioGcd (
  IN UINT32  A,
  IN UINT32  B
  )
{
  while (B != 0) {
    UINT32  T = A % B;

    A = B;
    B = T;
  }

  return A;
}

/**
  Find an exact fraction Num/Den with Parent * Num / Den = Target and both
  within the 16-bit fields of the divider.  Rockchip's fractional divider
  also wants the parent at least 20 times the output (Linux
  rockchip_fractional_approximation), which 1188 MHz / 12.288 MHz is.
**/
STATIC
BOOLEAN
DwHdmiQpAudioExactFraction (
  IN  UINT32  Parent,
  IN  UINT32  Target,
  OUT UINT32  *Num,
  OUT UINT32  *Den
  )
{
  UINT32  G;

  if ((Parent == 0) || (Parent < 20U * Target)) {
    return FALSE;
  }

  G    = DwHdmiQpAudioGcd (Parent, Target);
  *Num = Target / G;
  *Den = Parent / G;

  return (*Num <= 0xFFFF) && (*Den <= 0xFFFF);
}

/**
  mclk_sai6 = 12.288 MHz from clk_audio_frac_0, and the SAI6 clocks ungated.
**/
STATIC
BOOLEAN
DwHdmiQpAudioSetMclk (
  VOID
  )
{
  UINT32  Gpll;
  UINT32  Cpll;
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Num;
  UINT32  Den;

  Gpll = DwHdmiQpAudioPllRate (AUD_CRU_GPLL_CON0, AUD_CRU_GPLL_MODE_SHIFT);
  Cpll = DwHdmiQpAudioPllRate (AUD_CRU_CPLL_CON0, AUD_CRU_CPLL_MODE_SHIFT);

  if (DwHdmiQpAudioExactFraction (Gpll, AUD_MCLK_HZ, &Num, &Den)) {
    Sel    = AUD_FRAC0_SEL_GPLL;
    Parent = Gpll;
  } else if (DwHdmiQpAudioExactFraction (Cpll, AUD_MCLK_HZ, &Num, &Den)) {
    Sel    = AUD_FRAC0_SEL_CPLL;
    Parent = Cpll;
  } else {
    DEBUG ((DEBUG_WARN, "HdmiAudio: no exact 12.288 MHz from GPLL %u or CPLL %u Hz, no audio\n",
            Gpll, Cpll));
    return FALSE;
  }

  MmioWrite32 (AUD_CRU_CLKSEL_CON13, AUD_HIWORD (0x3, Sel));
  MmioWrite32 (AUD_CRU_CLKSEL_CON12, (Num << 16) | Den);
  MmioWrite32 (AUD_CRU_GATE_CON01, AUD_HIWORD (AUD_FRAC0_GATE, 0));

  //
  // [11] mclk_sai6 = mclk_sai6_src, [10:8] src = audio_frac_0, [7:0] div 1.
  //
  MmioWrite32 (AUD_CRU_CLKSEL_CON155, AUD_HIWORD (0xFFF, AUD_SAI6_SRC_SEL_FRAC0 << 8));
  MmioWrite32 (AUD_CRU_GATE_CON65, AUD_HIWORD (AUD_SAI6_GATES, 0));

  DEBUG ((DEBUG_INFO, "HdmiAudio: mclk_sai6 = %u Hz * %u / %u (%a)\n",
          Parent, Num, Den, (Sel == AUD_FRAC0_SEL_GPLL) ? "GPLL" : "CPLL"));
  return TRUE;
}

STATIC
VOID
DwHdmiQpAudioProgramController (
  IN CONST DW_HDMI_QP_AUDIO_LINK  *Link
  )
{
  UINTN   Base;
  UINT32  Cts;
  UINT32  Val;

  Base = Link->Base;

  //
  // DwHdmiQpSetup programs the PHY for Clock * 10 kbps, that is 8 bits per
  // component, so the TMDS character rate is the pixel clock.
  //
  Cts = (UINT32)DivU64x32 (
                  MultU64x32 ((UINT64)Link->ClockKhz * 1000U, AUD_ACR_N) + (128U * AUD_RATE_HZ) / 2,
                  128U * AUD_RATE_HZ
                  );

  // dw_hdmi_qp_audio_enable: audio data path out of software disable.
  Val = MmioRead32 (Base + GLOBAL_SWDISABLE);
  MmioWrite32 (Base + GLOBAL_SWDISABLE, Val & ~(UINT32)AVP_DATAPATH_PACKET_AUDIO_SWDISABLE);

  // dw_hdmi_qp_set_audio_interface
  MmioWrite32 (Base + GLOBAL_SWRESET_REQUEST, AVP_DATAPATH_PACKET_AUDIO_SWINIT_P);
  Val = MmioRead32 (Base + PKTSCHED_PKT_EN);
  MmioWrite32 (Base + PKTSCHED_PKT_EN, Val & ~(UINT32)AUD_PKT_ALL_AUDIO);
  MmioWrite32 (Base + AUDIO_INTERFACE_CONTROL0, AUDIO_FIFO_CLR_P);
  MmioWrite32 (Base + AUDIO_INTERFACE_CONFIG0, AUD_IF_CONFIG0_VALUE);

  // dw_hdmi_qp_set_sample_rate
  MmioWrite32 (Base + AUDPKT_ACR_CONTROL0, AUD_ACR_N);
  MmioWrite32 (Base + AUDPKT_ACR_CONTROL1, AUDPKT_ACR_CTS_OVR_VAL (Cts) | AUDPKT_ACR_CTS_OVR_EN);

  // dw_hdmi_qp_set_channel_status: the override registers before the enable.
  MmioWrite32 (Base + AUDPKT_CHSTATUS_OVR0, AUD_CHSTATUS_OVR0);
  MmioWrite32 (Base + AUDPKT_CHSTATUS_OVR1, AUD_CHSTATUS_OVR1);
  MmioWrite32 (Base + AUDPKT_CONTROL0, AUDPKT_PBIT_FORCE_EN | AUDPKT_CHSTATUS_OVR_EN);

  // dw_hdmi_qp_bridge_write_audio_infoframe, without AMD.
  MmioWrite32 (Base + PKT_AUDI_CONTENTS0, AUD_AUDI_CONTENTS0);
  MmioWrite32 (Base + PKT_AUDI_CONTENTS1, AUD_AUDI_CONTENTS1);
  MmioWrite32 (Base + PKT_AUDI_CONTENTS2, AUD_AUDI_CONTENTS2);
  MmioWrite32 (Base + PKT_AUDI_CONTENTS3, AUD_AUDI_CONTENTS3);

  Val = MmioRead32 (Base + PKTSCHED_PKT_EN);
  MmioWrite32 (Base + PKTSCHED_PKT_EN, Val | PKTSCHED_ACR_TX_EN | PKTSCHED_AUDI_TX_EN);
  Val = MmioRead32 (Base + PKTSCHED_PKT_EN);
  MmioWrite32 (Base + PKTSCHED_PKT_EN, Val | PKTSCHED_AUDS_TX_EN);

  DEBUG ((DEBUG_INFO, "HdmiAudio: 48 kHz 2 ch on %ux%u@%u, TMDS %u kHz, N %u CTS %u\n",
          Link->HDisplay, Link->VDisplay, Link->VRefresh, Link->ClockKhz, AUD_ACR_N, Cts));
}

STATIC
VOID
EFIAPI
DwHdmiQpAudioOnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  //
  // The clock first, whether or not a sink is there: it is the SAI's as much
  // as the HDMI controller's, and the SAI6 ACPI device reports itself present
  // by checking that CLKSEL_CON155 holds what was written here.
  //
  if (!DwHdmiQpAudioSetMclk ()) {
    return;
  }

  if (!mAudioLink.Valid) {
    return;
  }

  DwHdmiQpAudioProgramController (&mAudioLink);
}

VOID
DwHdmiQpAudioInit (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!FixedPcdGetBool (PcdHdmiAudioPreset) || (mAudioExitBootServicesEvent != NULL)) {
    return;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  DwHdmiQpAudioOnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mAudioExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HdmiAudio: cannot register for ExitBootServices: %r\n", Status));
  }
}
