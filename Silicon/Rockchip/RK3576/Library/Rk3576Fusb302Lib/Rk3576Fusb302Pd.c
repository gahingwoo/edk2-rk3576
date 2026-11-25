/** @file
 *
 *  DisplayPort Alt Mode on a USB-C port with an FCS FUSB302, entered once by
 *  the firmware: a PD 2.0 contract as a 5 V source and DFP, then the
 *  structured VDMs that put the partner (a USB-C monitor, a dock, a USB-C to
 *  DP or HDMI adapter) into DisplayPort mode.  Polled, no interrupt, one
 *  pass; used from UsbHcd's ExitBootServices handler when the "USB-C
 *  DisplayPort" setup option is on (docs/usbc-dp.md).
 *
 *  Windows has no driver for this chip, so nothing answers the partner
 *  after this has run.  What it leaves running is the chip's automatic
 *  GoodCRC, which acknowledges every later message at the PHY layer; the
 *  partner's HPD Attentions are then acknowledged and lost.  Hot-plug and
 *  an HPD change after boot need an OS-side port driver (phase 2 in the
 *  docs).
 *
 *  The sequence is mainline Linux's, cut down to one path:
 *
 *    drivers/usb/typec/tcpm/fusb302.c   PD PHY set-up (tcpm_init,
 *                                       tcpm_set_pd_rx, set_roles,
 *                                       set_cc_polarity_and_pull), TX FIFO
 *                                       tokens, RX FIFO parsing
 *    drivers/usb/typec/tcpm/tcpm.c      SRC_SEND_CAPABILITIES ..
 *                                       SRC_READY, the VDM discovery order,
 *                                       VCONN when the other CC line shows
 *                                       Ra, MessageID handling
 *    drivers/usb/typec/altmodes/displayport.c
 *                                       Status Update, Configure, Attention
 *
 *  What it does not do: PD 3.0, extended messages, SOP' cable discovery
 *  (Linux's FUSB302 driver does not either), PR_Swap/DR_Swap/VCONN_Swap
 *  (rejected), a sink-side contract (a monitor that powers the board's
 *  port presents Rp, and Rk3576Fusb302Probe does not call it a sink), and
 *  re-entry after a hard reset beyond one VBUS cycle.
 *
 *  THIS FILE HAS NOT BEEN RUN ON HARDWARE.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rk3576Fusb302Lib.h>
#include <Library/TimerLib.h>

#include "Fusb302Internal.h"
#include "UsbPdDp.h"

//
// Registers and bits (fusb302_reg.h).
//
#define REG_SWITCHES0           0x02
#define SW0_PU_EN2              BIT7
#define SW0_PU_EN1              BIT6
#define SW0_VCONN_CC2           BIT5
#define SW0_VCONN_CC1           BIT4
#define SW0_MEAS_CC2            BIT3
#define SW0_MEAS_CC1            BIT2
#define REG_SWITCHES1           0x03
#define SW1_POWERROLE           BIT7
#define SW1_SPECREV0            BIT5      // SPECREV 01: PD 2.0, in our GoodCRCs
#define SW1_DATAROLE            BIT4
#define SW1_AUTO_GCRC           BIT2
#define SW1_TXCC2               BIT1
#define SW1_TXCC1               BIT0
#define REG_MEASURE             0x04
#define MDAC_0V2                0x05      // 42 mV steps, as Rk3576Fusb302Lib.c
#define REG_CONTROL0            0x06
#define CTL0_TX_FLUSH           BIT6
#define REG_CONTROL1            0x07
#define CTL1_RX_FLUSH           BIT2
#define REG_CONTROL3            0x09
#define CTL3_RETRIES_3_AUTO     0x07      // N_RETRIES 3, AUTO_RETRY
#define REG_MASK                0x0A
#define REG_MASKA               0x0E
#define REG_MASKB               0x0F
#define REG_INTERRUPTA          0x3E
#define IRQA_RETRYFAIL          BIT4
#define IRQA_HARDSENT           BIT3
#define IRQA_TX_SUCCESS         BIT2
#define IRQA_HARDRESET          BIT0
#define REG_INTERRUPTB          0x3F
#define REG_STATUS0             0x40
#define ST0_COMP                BIT5
#define REG_STATUS1             0x41
#define ST1_RX_EMPTY            BIT5
#define REG_INTERRUPT           0x42
#define IRQ_COLLISION           BIT1
#define REG_FIFOS               0x43

//
// Timers (pd.h, in microseconds).  The waits for an answer are longer than
// the spec's sender-response timers: a late answer costs nothing here.
//
#define T_SEND_SOURCE_CAP       150000    // tTypeCSendSourceCap
#define T_SENDER_RESPONSE       60000     // Linux PD_T_SENDER_RESPONSE
#define T_SRC_TRANSITION        30000     // tSrcTransition, 25..35 ms
#define T_VDM_RESPONSE          60000
#define T_TX_DONE               10000     // three retries of a 39-byte message
#define T_HPD_WAIT              2000000   // an adapter's HPD after Configure
#define T_VBUS_OFF              1000000   // tSafe0V + tSrcRecover, about 1.4 s
#define T_VBUS_ON               100000

#define CAPS_TRIES              10        // nCapsCount is 50; 1.5 s is plenty

typedef struct {
  UINT16    Hdr;
  UINT32    Obj[7];
  UINT32    N;
} PD_MSG;

typedef enum {
  PdRxNone = 0,       // nothing within the time given
  PdRxMsg,            // a message for the caller
  PdRxHardReset,      // the partner sent Hard Reset
  PdRxSoftReset,      // the partner sent Soft_Reset (answered)
  PdRxError           // an I2C transfer failed
} PD_RX;

typedef struct {
  UINTN                Base;
  CONST CHAR8          *Name;
  UINT32               Pdo;
  UINT8                TxId;
  INT8                 RxId;      // -1: none yet
  UINT8                IrqA;      // INTERRUPTA bits seen and not yet used
  BOOLEAN              Contract;
  RK3576_FUSB302_DP    *Dp;
} PD_CTX;

STATIC
UINT64
NowUs (
  VOID
  )
{
  return DivU64x32 (GetTimeInNanoSecond (GetPerformanceCounter ()), 1000);
}

STATIC
BOOLEAN
Rmw (
  IN UINTN  Base,
  IN UINT8  Reg,
  IN UINT8  Set
  )
{
  UINT8  Val;

  return Fusb302RegRead (Base, Reg, &Val) && Fusb302RegWrite (Base, Reg, Val | Set);
}

/* INTERRUPTA is clear-on-read: keep what was read for whoever waits on it. */
STATIC
BOOLEAN
PollIrqA (
  IN OUT PD_CTX  *Ctx
  )
{
  UINT8  Val;

  if (!Fusb302RegRead (Ctx->Base, REG_INTERRUPTA, &Val)) {
    return FALSE;
  }

  Ctx->IrqA |= Val;
  return TRUE;
}

/*
 * The PD PHY on, as tcpm_init + set_roles + set_pd_rx leave it for a
 * source on CC: Rp and the measure block on the attached line, BMC TX on
 * it, roles source/DFP, PD 2.0 in our GoodCRCs, three hardware retries,
 * FIFOs flushed, the TX/RX status bits unmasked (CONTROL0's INT_MASK stays
 * set, so the INT pin never asserts: nobody in the OS listens to it).
 */
STATIC
BOOLEAN
PdInit (
  IN OUT PD_CTX  *Ctx,
  IN     BOOLEAN Cc1,
  IN     BOOLEAN Vconn
  )
{
  UINT8    Sw0;
  UINT8    Sw1;
  UINT8    Dummy;
  BOOLEAN  Ok;

  Sw0 = Cc1 ? (SW0_PU_EN1 | SW0_MEAS_CC1) : (SW0_PU_EN2 | SW0_MEAS_CC2);
  if (Vconn) {
    Sw0 |= Cc1 ? SW0_VCONN_CC2 : SW0_VCONN_CC1;
  }

  Sw1 = SW1_POWERROLE | SW1_SPECREV0 | SW1_DATAROLE | SW1_AUTO_GCRC | (Cc1 ? SW1_TXCC1 : SW1_TXCC2);

  Ok = Fusb302RegWrite (Ctx->Base, REG_SWITCHES0, Sw0) &&
       Fusb302RegWrite (Ctx->Base, REG_CONTROL3, CTL3_RETRIES_3_AUTO) &&
       Fusb302RegWrite (Ctx->Base, REG_MASK, (UINT8)~IRQ_COLLISION) &&
       Fusb302RegWrite (Ctx->Base, REG_MASKA, (UINT8)~(IRQA_RETRYFAIL | IRQA_HARDSENT | IRQA_TX_SUCCESS | IRQA_HARDRESET)) &&
       Fusb302RegWrite (Ctx->Base, REG_MASKB, (UINT8)~BIT0) &&
       Rmw (Ctx->Base, REG_CONTROL0, CTL0_TX_FLUSH) &&
       Rmw (Ctx->Base, REG_CONTROL1, CTL1_RX_FLUSH) &&
       Fusb302RegWrite (Ctx->Base, REG_SWITCHES1, Sw1) &&
       Fusb302RegRead (Ctx->Base, REG_INTERRUPTA, &Dummy) &&
       Fusb302RegRead (Ctx->Base, REG_INTERRUPTB, &Dummy) &&
       Fusb302RegRead (Ctx->Base, REG_INTERRUPT, &Dummy);

  Ctx->TxId = 0;
  Ctx->RxId = -1;
  Ctx->IrqA = 0;
  return Ok;
}

/*
 * One SOP message out, waiting for the partner's GoodCRC (TX_SUCCESS) or
 * the chip giving up after its retries (RETRYFAIL).  The MessageID moves
 * on only after a GoodCRC (tcpm.c tcpm_pd_transmit).
 */
STATIC
BOOLEAN
PdTx (
  IN OUT PD_CTX        *Ctx,
  IN     UINT32        Type,
  IN     CONST UINT32  *Obj,
  IN     UINT32        N
  )
{
  UINT8   Buf[40];
  UINT32  Len;
  UINT64  End;
  UINT8   Irq;

  Len = UpdFifoMessage (UpdHeader (Type, N, Ctx->TxId), Obj, N, Buf);
  if ((Len == 0) || (Len > 30)) {
    return FALSE;
  }

  Ctx->IrqA &= (UINT8)~(IRQA_TX_SUCCESS | IRQA_RETRYFAIL);
  Fusb302RegRead (Ctx->Base, REG_INTERRUPT, &Irq);
  if (!Fusb302RegWriteBurst (Ctx->Base, REG_FIFOS, Buf, Len)) {
    return FALSE;
  }

  End = NowUs () + T_TX_DONE;
  do {
    if (!PollIrqA (Ctx)) {
      return FALSE;
    }

    if ((Ctx->IrqA & IRQA_TX_SUCCESS) != 0) {
      Ctx->IrqA &= (UINT8)~IRQA_TX_SUCCESS;
      Ctx->TxId  = (Ctx->TxId + 1) & 7;
      return TRUE;
    }

    if ((Ctx->IrqA & IRQA_RETRYFAIL) != 0) {
      Ctx->IrqA &= (UINT8)~IRQA_RETRYFAIL;
      return FALSE;
    }

    MicroSecondDelay (100);
  } while (NowUs () < End);

  return FALSE;
}

STATIC
BOOLEAN
PdTxCtrl (
  IN OUT PD_CTX  *Ctx,
  IN     UINT32  Type
  )
{
  return PdTx (Ctx, Type, NULL, 0);
}

STATIC
BOOLEAN
PdTxSourceCaps (
  IN OUT PD_CTX  *Ctx
  )
{
  Ctx->Dp->CapsSent++;
  return PdTx (Ctx, PD_DATA_SOURCE_CAP, &Ctx->Pdo, 1);
}

/*
 * The next message in the RX FIFO, or none: the SOP token, the header, the
 * objects, the CRC (fusb302_pd_read_message).  The partner's GoodCRCs to
 * our messages are in there too and are dropped, as is a repeat of the
 * last MessageID (a retransmission after our GoodCRC was lost).
 */
STATIC
PD_RX
PdRxOne (
  IN OUT PD_CTX  *Ctx,
  OUT    PD_MSG  *Msg
  )
{
  UINT8   St1;
  UINT8   Head[3];
  UINT8   Rest[32];
  UINT32  Index;
  UINT32  Id;

  for ( ; ; ) {
    if (!PollIrqA (Ctx)) {
      return PdRxError;
    }

    if ((Ctx->IrqA & IRQA_HARDRESET) != 0) {
      Ctx->IrqA &= (UINT8)~IRQA_HARDRESET;
      return PdRxHardReset;
    }

    if (!Fusb302RegRead (Ctx->Base, REG_STATUS1, &St1)) {
      return PdRxError;
    }

    if ((St1 & ST1_RX_EMPTY) != 0) {
      return PdRxNone;
    }

    if (!Fusb302RegReadBurst (Ctx->Base, REG_FIFOS, Head, 3)) {
      return PdRxError;
    }

    Msg->Hdr = (UINT16)(Head[1] | ((UINT16)Head[2] << 8));
    Msg->N   = UpdHdrObjects (Msg->Hdr);
    if (!Fusb302RegReadBurst (Ctx->Base, REG_FIFOS, Rest, 4 * Msg->N + 4)) {
      return PdRxError;
    }

    for (Index = 0; Index < Msg->N; Index++) {
      Msg->Obj[Index] = Rest[4 * Index] | ((UINT32)Rest[4 * Index + 1] << 8) |
                        ((UINT32)Rest[4 * Index + 2] << 16) | ((UINT32)Rest[4 * Index + 3] << 24);
    }

    if (!UpdRxTokenIsSop (Head[0]) || UpdIsCtrl (Msg->Hdr, PD_CTRL_GOODCRC)) {
      continue;
    }

    Id = UpdHdrMsgId (Msg->Hdr);
    if (!UpdIsCtrl (Msg->Hdr, PD_CTRL_SOFT_RESET) && (Ctx->RxId == (INT8)Id)) {
      continue;
    }

    Ctx->RxId = (INT8)Id;
    return PdRxMsg;
  }
}

/* The partner's DP Status, from a Status Update ACK or an Attention. */
STATIC
VOID
TakeStatus (
  IN OUT PD_CTX  *Ctx,
  IN     UINT32  Status
  )
{
  Ctx->Dp->StatusVdo = Status;
  Ctx->Dp->Hpd       = (Status & UPD_DP_STATUS_HPD) != 0;
}

/*
 * A message the caller was not waiting for, answered as tcpm.c answers it
 * in SRC_READY: Soft_Reset with Accept (the caller then starts over), a
 * Request with Accept or Reject and PS_RDY, Get_Source_Cap with our caps,
 * the role swaps and Get_Sink_Cap with Reject, a partner's VDM request
 * with NAK, and an Attention by taking its DP Status.
 */
STATIC
PD_RX
PdUnsolicited (
  IN OUT PD_CTX        *Ctx,
  IN     CONST PD_MSG  *Msg
  )
{
  UINT32  Nak;

  if (UpdIsCtrl (Msg->Hdr, PD_CTRL_SOFT_RESET)) {
    PdTxCtrl (Ctx, PD_CTRL_ACCEPT);
    Ctx->TxId     = 0;
    Ctx->RxId     = -1;
    Ctx->Contract = FALSE;
    return PdRxSoftReset;
  }

  if (UpdIsData (Msg->Hdr, PD_DATA_REQUEST)) {
    if (UpdRequestOk (Msg->Obj[0], Ctx->Pdo) && PdTxCtrl (Ctx, PD_CTRL_ACCEPT)) {
      MicroSecondDelay (T_SRC_TRANSITION);
      Ctx->Contract = PdTxCtrl (Ctx, PD_CTRL_PS_RDY);
    } else {
      PdTxCtrl (Ctx, PD_CTRL_REJECT);
    }

    return PdRxNone;
  }

  if (UpdIsCtrl (Msg->Hdr, PD_CTRL_GET_SOURCE_CAP)) {
    PdTxSourceCaps (Ctx);
    return PdRxNone;
  }

  if (UpdIsCtrl (Msg->Hdr, PD_CTRL_GET_SINK_CAP) || UpdIsCtrl (Msg->Hdr, PD_CTRL_DR_SWAP) ||
      UpdIsCtrl (Msg->Hdr, PD_CTRL_PR_SWAP) || UpdIsCtrl (Msg->Hdr, PD_CTRL_VCONN_SWAP))
  {
    PdTxCtrl (Ctx, PD_CTRL_REJECT);
    return PdRxNone;
  }

  if (UpdIsData (Msg->Hdr, PD_DATA_VENDOR_DEF) && UpdVdmStructured (Msg->Obj[0])) {
    if ((UpdVdmCmd (Msg->Obj[0]) == UPD_VDM_ATTENTION) && (UpdVdmSvid (Msg->Obj[0]) == UPD_SVID_DP)) {
      if (Msg->N >= 2) {
        TakeStatus (Ctx, Msg->Obj[1]);
      }
    } else if (UpdVdmCmdType (Msg->Obj[0]) == UPD_VDM_REQ) {
      Nak = UpdVdmHeader (
              UpdVdmSvid (Msg->Obj[0]),
              UpdVdmCmd (Msg->Obj[0]),
              UpdVdmObjPos (Msg->Obj[0]),
              UPD_VDM_NAK
              );
      PdTx (Ctx, PD_DATA_VENDOR_DEF, &Nak, 1);
    }
  }

  return PdRxNone;
}

/*
 * Wait up to Us for a message the caller wants (Want returns TRUE for it),
 * answering everything else on the way.
 */
typedef BOOLEAN (*PD_WANT)(
  IN CONST PD_MSG  *Msg,
  IN UINT32        Arg
  );

STATIC
PD_RX
PdWait (
  IN OUT PD_CTX   *Ctx,
  IN     PD_WANT  Want,
  IN     UINT32   Arg,
  IN     UINT32   Us,
  OUT    PD_MSG   *Msg
  )
{
  UINT64  End;
  PD_RX   Rx;

  End = NowUs () + Us;
  do {
    Rx = PdRxOne (Ctx, Msg);
    if (Rx == PdRxMsg) {
      if ((Want != NULL) && Want (Msg, Arg)) {
        return PdRxMsg;
      }

      Rx = PdUnsolicited (Ctx, Msg);
      if (Rx == PdRxSoftReset) {
        return Rx;
      }

      continue;
    }

    if ((Rx == PdRxHardReset) || (Rx == PdRxError)) {
      return Rx;
    }

    MicroSecondDelay (200);
  } while (NowUs () < End);

  return PdRxNone;
}

STATIC
BOOLEAN
WantRequest (
  IN CONST PD_MSG  *Msg,
  IN UINT32        Arg
  )
{
  return UpdIsData (Msg->Hdr, PD_DATA_REQUEST);
}

STATIC
BOOLEAN
WantVdmAnswer (
  IN CONST PD_MSG  *Msg,
  IN UINT32        SvidCmd
  )
{
  return UpdIsData (Msg->Hdr, PD_DATA_VENDOR_DEF) &&
         UpdVdmIsAnswer (Msg->Obj[0], SvidCmd >> 16, SvidCmd & 0xFF);
}

STATIC
BOOLEAN
WantDpHpd (
  IN CONST PD_MSG  *Msg,
  IN UINT32        Arg
  )
{
  return UpdIsData (Msg->Hdr, PD_DATA_VENDOR_DEF) && (Msg->N >= 2) &&
         (UpdVdmCmd (Msg->Obj[0]) == UPD_VDM_ATTENTION) &&
         (UpdVdmSvid (Msg->Obj[0]) == UPD_SVID_DP) &&
         ((Msg->Obj[1] & UPD_DP_STATUS_HPD) != 0);
}

/*
 * Source_Capabilities until a Request comes, then Accept, tSrcTransition
 * (VBUS does not change: 5 V is what it already is) and PS_RDY.  A Request
 * for more than we offer gets Reject and the caps again.
 */
STATIC
PD_RX
PdContract (
  IN OUT PD_CTX  *Ctx
  )
{
  PD_MSG  Msg;
  PD_RX   Rx;
  UINT32  Try;

  for (Try = 0; Try < CAPS_TRIES && !Ctx->Contract; Try++) {
    if (!PdTxSourceCaps (Ctx)) {
      MicroSecondDelay (T_SEND_SOURCE_CAP);
      continue;
    }

    Rx = PdWait (Ctx, WantRequest, 0, T_SENDER_RESPONSE, &Msg);
    if ((Rx == PdRxHardReset) || (Rx == PdRxError)) {
      return Rx;
    }

    if (Rx != PdRxMsg) {
      continue;
    }

    if (UpdRequestOk (Msg.Obj[0], Ctx->Pdo) && PdTxCtrl (Ctx, PD_CTRL_ACCEPT)) {
      MicroSecondDelay (T_SRC_TRANSITION);
      Ctx->Contract = PdTxCtrl (Ctx, PD_CTRL_PS_RDY);
    } else {
      PdTxCtrl (Ctx, PD_CTRL_REJECT);
    }
  }

  return Ctx->Contract ? PdRxMsg : PdRxNone;
}

/*
 * One structured VDM request and its answer.  BUSY is asked again (three
 * times, as tcpm.c's VDM retries); a timeout too.  TRUE only for an ACK.
 */
STATIC
BOOLEAN
PdVdm (
  IN OUT PD_CTX        *Ctx,
  IN     UINT32        Svid,
  IN     UINT32        Cmd,
  IN     UINT32        ObjPos,
  IN     CONST UINT32  *Vdo     OPTIONAL,
  OUT    PD_MSG        *Ans
  )
{
  UINT32  Obj[2];
  UINT32  N;
  UINT32  Try;
  PD_RX   Rx;

  Ans->N   = 0;
  Ans->Hdr = 0;
  Obj[0]   = UpdVdmHeader (Svid, Cmd, ObjPos, UPD_VDM_REQ);
  N        = 1;
  if (Vdo != NULL) {
    Obj[1] = *Vdo;
    N      = 2;
  }

  for (Try = 0; Try < 3; Try++) {
    if (!PdTx (Ctx, PD_DATA_VENDOR_DEF, Obj, N)) {
      continue;
    }

    Rx = PdWait (Ctx, WantVdmAnswer, (Svid << 16) | Cmd, T_VDM_RESPONSE, Ans);
    if (Rx != PdRxMsg) {
      if (Rx == PdRxNone) {
        continue;
      }

      return FALSE;
    }

    switch (UpdVdmCmdType (Ans->Obj[0])) {
      case UPD_VDM_ACK:
        return TRUE;
      case UPD_VDM_BUSY:
        MicroSecondDelay (50000);
        continue;
      default:
        return FALSE;
    }
  }

  return FALSE;
}

/*
 * Discovery and entry, in tcpm.c's order: Discover Identity, SVIDs, Modes
 * for 0xFF01; then Enter Mode, DP Status Update and DP Configure
 * (displayport.c).
 */
STATIC
VOID
PdEnterDp (
  IN OUT PD_CTX  *Ctx
  )
{
  RK3576_FUSB302_DP  *Dp;
  PD_MSG             Ans;
  UINT32             Index;
  UINT32             ObjPos;
  UINT32             Vdo;
  UINT32             Pin;
  PD_RX              Rx;

  Dp = Ctx->Dp;
  if (!PdVdm (Ctx, UPD_SVID_PD, UPD_VDM_DISC_IDENT, 0, NULL, &Ans) || (Ans.N < 2) ||
      !UpdIdModal (Ans.Obj[1]))
  {
    if (Ans.N >= 2) {
      Dp->IdHeader = Ans.Obj[1];
    }

    return;
  }

  Dp->IdHeader = Ans.Obj[1];
  Dp->Step     = Rk3576DpStepIdentity;

  if (!PdVdm (Ctx, UPD_SVID_PD, UPD_VDM_DISC_SVID, 0, NULL, &Ans) || (Ans.N < 2) ||
      !UpdSvidsHave (&Ans.Obj[1], Ans.N - 1, UPD_SVID_DP))
  {
    return;
  }

  Dp->Step = Rk3576DpStepSvid;

  if (!PdVdm (Ctx, UPD_SVID_DP, UPD_VDM_DISC_MODES, 0, NULL, &Ans)) {
    return;
  }

  ObjPos = 0;
  for (Index = 1; Index < Ans.N; Index++) {
    if (UpdDpModeUsable (Ans.Obj[Index])) {
      ObjPos      = Index;
      Dp->ModeVdo = Ans.Obj[Index];
      break;
    }
  }

  if (ObjPos == 0) {
    return;
  }

  Dp->Step = Rk3576DpStepModes;

  if (!PdVdm (Ctx, UPD_SVID_DP, UPD_VDM_ENTER_MODE, ObjPos, NULL, &Ans)) {
    return;
  }

  Dp->Step = Rk3576DpStepEntered;

  Vdo = UPD_DP_OUR_STATUS;
  if (!PdVdm (Ctx, UPD_SVID_DP, UPD_VDM_DP_STATUS, ObjPos, &Vdo, &Ans)) {
    return;
  }

  if (Ans.N >= 2) {
    TakeStatus (Ctx, Ans.Obj[1]);
  }

  Dp->Step = Rk3576DpStepStatus;

  Pin = UpdDpPickPin (Dp->ModeVdo);
  if (Pin == 0) {
    return;
  }

  Dp->ConfigVdo = UpdDpConfigVdo (Dp->ModeVdo, Pin);
  if (!PdVdm (Ctx, UPD_SVID_DP, UPD_VDM_DP_CONFIG, ObjPos, &Dp->ConfigVdo, &Ans)) {
    return;
  }

  Dp->Step       = Rk3576DpStepConfigured;
  Dp->Configured = TRUE;
  Dp->PinName    = UpdDpPinName (Pin);

  //
  // An adapter raises HPD once its own DP or HDMI side has a monitor; the
  // Status ACK may have come first.  Wait a little for the Attention.
  //
  if (!Dp->Hpd) {
    Rx = PdWait (Ctx, WantDpHpd, 0, T_HPD_WAIT, &Ans);
    if (Rx == PdRxMsg) {
      TakeStatus (Ctx, Ans.Obj[1]);
    }
  }
}

/*
 * Ra (an e-marked cable, a VCONN-powered adapter) on the line that is not
 * the attached one: below 0.2 V with our Rp on it, as tcpm_src_attach
 * decides VCONN.  Rp is put back on the attached line only afterwards.
 */
STATIC
BOOLEAN
OtherLineIsRa (
  IN UINTN    Base,
  IN BOOLEAN  Cc1
  )
{
  UINT8    St0;
  BOOLEAN  Ok;

  St0 = 0;
  Ok  = Fusb302RegWrite (
          Base,
          REG_SWITCHES0,
          SW0_PU_EN1 | SW0_PU_EN2 | (Cc1 ? SW0_MEAS_CC2 : SW0_MEAS_CC1)
          ) &&
        Fusb302RegWrite (Base, REG_MEASURE, MDAC_0V2);
  MicroSecondDelay (1000);
  Ok = Ok && Fusb302RegRead (Base, REG_STATUS0, &St0);
  Fusb302RegWrite (Base, REG_SWITCHES0, Cc1 ? (SW0_PU_EN1 | SW0_MEAS_CC1) : (SW0_PU_EN2 | SW0_MEAS_CC2));
  return Ok && ((St0 & ST0_COMP) == 0);
}

STATIC
CONST CHAR8 *
StepName (
  IN RK3576_DP_STEP  Step
  )
{
  STATIC CONST CHAR8  *CONST  Names[] = {
    "none", "PD on, no contract", "contract", "identity (modal)", "SVID 0xFF01",
    "DP mode", "mode entered", "DP status", "configured"
  };

  return ((UINTN)Step < ARRAY_SIZE (Names)) ? Names[Step] : "?";
}

VOID
EFIAPI
Rk3576Fusb302DpAltMode (
  IN  CONST RK3576_FUSB302_BUS    *Bus,
  IN  CONST RK3576_FUSB302_STATE  *State,
  IN  RK3576_FUSB302_VBUS_SET     VbusSet  OPTIONAL,
  OUT RK3576_FUSB302_DP           *Dp
  )
{
  FUSB302_BUS_SAVED  Saved;
  PD_CTX             Ctx;
  BOOLEAN            Cc1;
  UINT32             Attempt;
  PD_RX              Rx;

  ZeroMem (Dp, sizeof (*Dp));
  Dp->PinName = '-';
  if ((State->Partner != Rk3576Fusb302Sink) || !State->RpPresented ||
      (State->Orientation == UsbTypeCOrientationUnknown))
  {
    DEBUG ((DEBUG_INFO, "%a DP alt mode: no sink with Rp on one line, not tried\n", Bus->Name));
    return;
  }

  Cc1 = State->Orientation == UsbTypeCOrientationNormal;
  ZeroMem (&Ctx, sizeof (Ctx));
  Ctx.Base = Bus->I2cBase;
  Ctx.Name = Bus->Name;
  Ctx.Pdo  = UpdSourcePdo ((Bus->VbusMilliAmps != 0) ? Bus->VbusMilliAmps : 500);
  Ctx.Dp   = Dp;

  Fusb302BusOpen (Bus, &Saved);

  if (Bus->VconnSupply && OtherLineIsRa (Ctx.Base, Cc1)) {
    Dp->Vconn = TRUE;
  }

  for (Attempt = 0; Attempt < 3 && Dp->Step < Rk3576DpStepConfigured; Attempt++) {
    if (!PdInit (&Ctx, Cc1, Dp->Vconn)) {
      DEBUG ((DEBUG_ERROR, "%a DP alt mode: FUSB302 PD set-up failed on I2C\n", Bus->Name));
      break;
    }

    if (Dp->Vconn && (Attempt == 0)) {
      MicroSecondDelay (10000);   // the e-marker or the adapter's controller powers up
    }

    Dp->Step = Rk3576DpStepPdInit;
    Rx       = PdContract (&Ctx);
    if ((Rx == PdRxHardReset) || ((Rx == PdRxNone) && !Ctx.Contract)) {
      //
      // A hard reset from the partner, or no Request at all: a sink that
      // gave up on PD (its SinkWaitCap ran out while VBUS was on through
      // the boot menu) starts over only after VBUS goes away.  A source
      // answers a hard reset the same way (tcpm.c SRC_HARD_RESET_VBUS_OFF).
      //
      if ((VbusSet == NULL) || Dp->VbusCycled) {
        break;
      }

      DEBUG ((DEBUG_INFO, "%a DP alt mode: no contract (%u caps sent), switching VBUS off and on\n",
              Bus->Name, Dp->CapsSent));
      VbusSet (FALSE);
      MicroSecondDelay (T_VBUS_OFF);
      VbusSet (TRUE);
      MicroSecondDelay (T_VBUS_ON);
      Dp->VbusCycled = TRUE;
      continue;
    }

    if (Rx == PdRxError) {
      break;
    }

    Dp->Step = Rk3576DpStepContract;
    PdEnterDp (&Ctx);
    if (!Ctx.Contract) {
      // A Soft_Reset during discovery: once more from the caps.
      continue;
    }

    break;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a DP alt mode: %a; caps sent %u, VCONN %a, VBUS cycled %u; id 0x%08x mode 0x%08x "
    "status 0x%08x config 0x%08x; pin %c, HPD %a\n",
    Bus->Name,
    StepName (Dp->Step),
    Dp->CapsSent,
    Dp->Vconn ? "on" : "off",
    Dp->VbusCycled,
    Dp->IdHeader,
    Dp->ModeVdo,
    Dp->StatusVdo,
    Dp->ConfigVdo,
    Dp->PinName,
    Dp->Hpd ? "high" : "low"
    ));

  Fusb302BusClose (Bus, &Saved);
}
