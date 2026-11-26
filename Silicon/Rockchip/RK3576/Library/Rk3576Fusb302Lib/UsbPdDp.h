/** @file
 *
 *  USB Power Delivery and DisplayPort Alt Mode, the arithmetic: message
 *  headers, the one source PDO, the checks on a sink's Request, the
 *  structured VDMs a DFP sends to enter DisplayPort mode, and the choice of
 *  pin assignment.  No register is touched here and nothing is waited for;
 *  Rk3576Fusb302Pd.c does that.  Plain C with no EDK2 header, so that
 *  scripts/tests/usbpd-host can build it on the host.
 *
 *  Every field is from mainline Linux:
 *
 *    include/linux/usb/pd.h        message header, PDO, RDO
 *    include/linux/usb/pd_vdo.h    structured VDM header
 *    include/linux/usb/typec_dp.h  DisplayPort capabilities, status and
 *                                  configuration VDOs
 *    drivers/usb/typec/altmodes/displayport.c
 *                                  dp_altmode_configure (pin assignment)
 *
 *  The port is a PD 2.0 source and DFP (the board powers the port and is
 *  the USB host); SVDM version 1.0.  PD 2.0 avoids PD 3.0's collision
 *  avoidance (SinkTxOk/SinkTxNG Rp switching) and Linux falls back to it
 *  for sinks that misbehave on 3.0 (tcpm.c).
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef USB_PD_DP_H_
#define USB_PD_DP_H_

#ifndef UPD_INLINE
#ifdef _MSC_VER
#define UPD_INLINE  static __inline
#else
#define UPD_INLINE  static inline
#endif
#endif

typedef unsigned char   upd_u8;
typedef unsigned short  upd_u16;
typedef unsigned int    upd_u32;

/* ---- message header (pd.h PD_HEADER) ---- */

#define PD_CTRL_GOODCRC         1u
#define PD_CTRL_ACCEPT          3u
#define PD_CTRL_REJECT          4u
#define PD_CTRL_PING            5u
#define PD_CTRL_PS_RDY          6u
#define PD_CTRL_GET_SOURCE_CAP  7u
#define PD_CTRL_GET_SINK_CAP    8u
#define PD_CTRL_DR_SWAP         9u
#define PD_CTRL_PR_SWAP         10u
#define PD_CTRL_VCONN_SWAP      11u
#define PD_CTRL_WAIT            12u
#define PD_CTRL_SOFT_RESET      13u

#define PD_DATA_SOURCE_CAP      1u
#define PD_DATA_REQUEST         2u
#define PD_DATA_SINK_CAP        4u
#define PD_DATA_VENDOR_DEF      15u

/* Our header: DataRole DFP (bit 5), SpecRev 01 = 2.0 (bits 7:6), PowerRole
 * source (bit 8), MessageID (bits 11:9), NumObj (bits 14:12). */
UPD_INLINE upd_u16 UpdHeader (upd_u32 Type, upd_u32 Objects, upd_u32 MsgId)
{
  return (upd_u16)((Type & 0x1F) | (1u << 5) | (1u << 6) | (1u << 8) |
                   ((MsgId & 7u) << 9) | ((Objects & 7u) << 12));
}

UPD_INLINE upd_u32 UpdHdrType (upd_u16 H)    { return H & 0x1Fu; }
UPD_INLINE upd_u32 UpdHdrObjects (upd_u16 H) { return (H >> 12) & 7u; }
UPD_INLINE upd_u32 UpdHdrMsgId (upd_u16 H)   { return (H >> 9) & 7u; }
UPD_INLINE upd_u32 UpdHdrExt (upd_u16 H)     { return (H >> 15) & 1u; }
UPD_INLINE upd_u32 UpdHdrRev (upd_u16 H)     { return (H >> 6) & 3u; }

/* A control message is one with no data object. */
UPD_INLINE int UpdIsCtrl (upd_u16 H, upd_u32 Type)
{
  return UpdHdrObjects (H) == 0 && UpdHdrType (H) == Type && !UpdHdrExt (H);
}

UPD_INLINE int UpdIsData (upd_u16 H, upd_u32 Type)
{
  return UpdHdrObjects (H) != 0 && UpdHdrType (H) == Type && !UpdHdrExt (H);
}

/* ---- the source capability (pd.h PDO_FIXED) ----
 *
 * One fixed 5 V PDO: 50 mV units at bits 19:10, 10 mA units at bits 9:0,
 * USB communications capable (bit 26).  Not dual role power or data (bits
 * 29 and 25 clear), so a partner should send neither PR_Swap nor DR_Swap;
 * both are rejected if it does. */
#define UPD_PDO_USB_COMM        (1u << 26)

UPD_INLINE upd_u32 UpdSourcePdo (upd_u32 MilliAmps)
{
  return UPD_PDO_USB_COMM | ((5000u / 50u) << 10) | ((MilliAmps / 10u) & 0x3FFu);
}

/* A Request for that PDO (pd.h RDO_*): object position 1 (bits 30:28),
 * operating current (bits 19:10) and maximum current (bits 9:0, which may
 * exceed ours only with Capability Mismatch, bit 26), both in 10 mA.
 * tcpm.c tcpm_pd_check_request makes the same checks. */
UPD_INLINE int UpdRequestOk (upd_u32 Rdo, upd_u32 Pdo)
{
  upd_u32 Max = Pdo & 0x3FFu;
  upd_u32 Op  = (Rdo >> 10) & 0x3FFu;
  upd_u32 Lim = Rdo & 0x3FFu;

  if (((Rdo >> 28) & 7u) != 1u) {
    return 0;
  }

  if (Op > Max) {
    return 0;
  }

  if ((Lim > Max) && ((Rdo & (1u << 26)) == 0)) {
    return 0;
  }

  return 1;
}

/* ---- structured VDMs (pd_vdo.h VDO, SVDM version 1.0) ---- */

#define UPD_SVID_PD             0xFF00u
#define UPD_SVID_DP             0xFF01u

#define UPD_VDM_DISC_IDENT      1u
#define UPD_VDM_DISC_SVID       2u
#define UPD_VDM_DISC_MODES      3u
#define UPD_VDM_ENTER_MODE      4u
#define UPD_VDM_EXIT_MODE       5u
#define UPD_VDM_ATTENTION       6u
#define UPD_VDM_DP_STATUS       0x10u
#define UPD_VDM_DP_CONFIG       0x11u

#define UPD_VDM_REQ             0u
#define UPD_VDM_ACK             1u
#define UPD_VDM_NAK             2u
#define UPD_VDM_BUSY            3u

UPD_INLINE upd_u32 UpdVdmHeader (upd_u32 Svid, upd_u32 Cmd, upd_u32 ObjPos, upd_u32 CmdType)
{
  return (Svid << 16) | (1u << 15) | ((ObjPos & 7u) << 8) | ((CmdType & 3u) << 6) | (Cmd & 0x1Fu);
}

UPD_INLINE upd_u32 UpdVdmSvid (upd_u32 V)        { return V >> 16; }
UPD_INLINE upd_u32 UpdVdmStructured (upd_u32 V)  { return (V >> 15) & 1u; }
UPD_INLINE upd_u32 UpdVdmObjPos (upd_u32 V)      { return (V >> 8) & 7u; }
UPD_INLINE upd_u32 UpdVdmCmdType (upd_u32 V)     { return (V >> 6) & 3u; }
UPD_INLINE upd_u32 UpdVdmCmd (upd_u32 V)         { return V & 0x1Fu; }

/* Is V the partner's answer (ACK, NAK or BUSY) to our request Cmd on Svid? */
UPD_INLINE int UpdVdmIsAnswer (upd_u32 V, upd_u32 Svid, upd_u32 Cmd)
{
  return UpdVdmStructured (V) && UpdVdmSvid (V) == Svid && UpdVdmCmd (V) == Cmd &&
         UpdVdmCmdType (V) != UPD_VDM_REQ;
}

/* Discover Identity: the ID header VDO says whether the partner supports
 * modal operation (bit 26, pd_vdo.h PD_IDH_MODAL_SUPP). */
UPD_INLINE int UpdIdModal (upd_u32 IdHeader)
{
  return (IdHeader >> 26) & 1u;
}

/* Discover SVIDs: each VDO carries two SVIDs, high half first; 0 ends the
 * list.  TRUE if SVID is among the N VDOs. */
UPD_INLINE int UpdSvidsHave (const upd_u32 *Vdo, upd_u32 N, upd_u32 Svid)
{
  for (upd_u32 i = 0; i < N; i++) {
    if ((Vdo[i] >> 16) == Svid || (Vdo[i] & 0xFFFFu) == Svid) {
      return 1;
    }
  }

  return 0;
}

/* ---- DisplayPort (typec_dp.h) ---- */

#define UPD_DP_PIN_A            (1u << 0)
#define UPD_DP_PIN_B            (1u << 1)
#define UPD_DP_PIN_C            (1u << 2)
#define UPD_DP_PIN_D            (1u << 3)
#define UPD_DP_PIN_E            (1u << 4)
#define UPD_DP_PIN_F            (1u << 5)

#define UPD_DP_STATUS_CON(s)    ((s) & 3u)
#define UPD_DP_CON_DISABLED     0u
#define UPD_DP_CON_DFP_D        1u
#define UPD_DP_CON_UFP_D        2u
#define UPD_DP_CON_BOTH         3u
#define UPD_DP_STATUS_MULTI     (1u << 4)
#define UPD_DP_STATUS_TO_USB    (1u << 5)
#define UPD_DP_STATUS_EXIT      (1u << 6)
#define UPD_DP_STATUS_HPD       (1u << 7)
#define UPD_DP_STATUS_IRQ_HPD   (1u << 8)

#define UPD_DP_CAP_UFP_D        1u
#define UPD_DP_CAP_RECEPTACLE   (1u << 6)

/* The pin assignments the partner offers as a UFP_D, taking plug against
 * receptacle into account (DP_CAP_PIN_ASSIGN_UFP_D): a receptacle lists
 * them in bits 23:16, a captive plug (an adapter cable) in bits 15:8. */
UPD_INLINE upd_u32 UpdDpUfpPins (upd_u32 ModeVdo)
{
  return (ModeVdo & UPD_DP_CAP_RECEPTACLE) ? ((ModeVdo >> 16) & 0xFFu) : ((ModeVdo >> 8) & 0xFFu);
}

/* Can this mode VDO be a DisplayPort sink for us: UFP_D capable with at
 * least one pin assignment we drive (C, D or E: the USB-C pin assignments;
 * A, B and F are the withdrawn DP 1.0 ones). */
UPD_INLINE int UpdDpModeUsable (upd_u32 ModeVdo)
{
  return (ModeVdo & UPD_DP_CAP_UFP_D) != 0 &&
         (UpdDpUfpPins (ModeVdo) & (UPD_DP_PIN_C | UPD_DP_PIN_D | UPD_DP_PIN_E)) != 0;
}

/* The pin assignment to configure.  This port only ever drives two DP
 * lanes (the USBDP PHY's two DP-muxed lanes next to USB3, see the docs), so
 * D, which keeps USB3 on the other pair, is taken whenever the partner has
 * it; otherwise C, then E, whose two first lanes sit on the same pins.
 * Linux prefers C unless the partner asks for multi-function, because it
 * can drive four lanes.  0 if none fits. */
UPD_INLINE upd_u32 UpdDpPickPin (upd_u32 ModeVdo)
{
  upd_u32 Pins = UpdDpUfpPins (ModeVdo);

  if (Pins & UPD_DP_PIN_D) {
    return UPD_DP_PIN_D;
  }

  if (Pins & UPD_DP_PIN_C) {
    return UPD_DP_PIN_C;
  }

  if (Pins & UPD_DP_PIN_E) {
    return UPD_DP_PIN_E;
  }

  return 0;
}

UPD_INLINE char UpdDpPinName (upd_u32 Pin)
{
  return Pin == UPD_DP_PIN_C ? 'C' : Pin == UPD_DP_PIN_D ? 'D' : Pin == UPD_DP_PIN_E ? 'E' : '-';
}

/* The Configure VDO (dp_altmode_configure for a UFP_D partner): signalling
 * DP 1.3 if the partner has it (bits 5:2, our port does 1 only), UFP_U as
 * UFP_D (bit 1), the pin assignment at bits 15:8. */
UPD_INLINE upd_u32 UpdDpConfigVdo (upd_u32 ModeVdo, upd_u32 Pin)
{
  upd_u32 Signal = ((ModeVdo >> 2) & 0xFu) & 1u;

  return (Signal << 2) | (1u << 1) | ((Pin & 0xFFu) << 8);
}

/* Our own DP Status, sent with Status Update: DFP_D connected. */
#define UPD_DP_OUR_STATUS       UPD_DP_CON_DFP_D

/* ---- the FUSB302's FIFO tokens (fusb302_reg.h) ---- */

#define UPD_TOK_SYNC1           0x12u
#define UPD_TOK_SYNC2           0x13u
#define UPD_TOK_PACKSYM         0x80u
#define UPD_TOK_JAM_CRC         0xFFu
#define UPD_TOK_EOP             0x14u
#define UPD_TOK_TXOFF           0xFEu
#define UPD_TOK_TXON            0xA1u

/* The TX FIFO bytes of one SOP message: SOP (three SYNC-1 and a SYNC-2),
 * PACKSYM with the byte count, the header and objects little endian,
 * JAM_CRC (the chip computes the CRC), EOP, TXOFF, TXON
 * (fusb302_pd_send_message).  Returns the length, at most 11 + 4 * 7 = 39;
 * 0 for more than 7 objects. */
UPD_INLINE upd_u32 UpdFifoMessage (upd_u16 Hdr, const upd_u32 *Obj, upd_u32 N, upd_u8 *Out)
{
  upd_u32 Len = 0;
  upd_u32 Bytes = 2u + 4u * N;

  if (N > 7) {
    return 0;
  }

  Out[Len++] = UPD_TOK_SYNC1;
  Out[Len++] = UPD_TOK_SYNC1;
  Out[Len++] = UPD_TOK_SYNC1;
  Out[Len++] = UPD_TOK_SYNC2;
  Out[Len++] = (upd_u8)(UPD_TOK_PACKSYM | Bytes);
  Out[Len++] = (upd_u8)(Hdr & 0xFF);
  Out[Len++] = (upd_u8)(Hdr >> 8);
  for (upd_u32 i = 0; i < N; i++) {
    Out[Len++] = (upd_u8)(Obj[i] & 0xFF);
    Out[Len++] = (upd_u8)((Obj[i] >> 8) & 0xFF);
    Out[Len++] = (upd_u8)((Obj[i] >> 16) & 0xFF);
    Out[Len++] = (upd_u8)(Obj[i] >> 24);
  }

  Out[Len++] = UPD_TOK_JAM_CRC;
  Out[Len++] = UPD_TOK_EOP;
  Out[Len++] = UPD_TOK_TXOFF;
  Out[Len++] = UPD_TOK_TXON;
  return Len;
}

/* The RX FIFO's first byte for a message: bits 7:5 say which SOP it came
 * on (111 SOP, 110 SOP', 101 SOP''; fusb302_reg.h RX_TOKEN). */
UPD_INLINE int UpdRxTokenIsSop (upd_u8 Tok)
{
  return (Tok & 0xE0u) == 0xE0u;
}

#endif // USB_PD_DP_H_
