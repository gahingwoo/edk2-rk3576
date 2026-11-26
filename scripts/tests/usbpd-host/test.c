// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * Silicon/Rockchip/RK3576/Library/Rk3576Fusb302Lib/UsbPdDp.h against the
 * values mainline Linux produces for the same messages (include/linux/usb/
 * pd.h, pd_vdo.h, typec_dp.h, drivers/usb/typec/altmodes/displayport.c and
 * fusb302.c's TX FIFO layout), worked out by hand:
 *
 *  - headers: Source_Capabilities with one object 0x1161, Accept 0x0163,
 *    PS_RDY 0x0166, a VDM with one object 0x116F, MessageID at 11:9;
 *  - PDO_FIXED(5000, 900, PDO_FIXED_USB_COMM) = 0x0401905A, and the
 *    Request checks of tcpm_pd_check_request;
 *  - the SVDM headers of Discover Identity 0xFF008001, Discover Modes
 *    0xFF018003, Enter Mode at position 1 0xFF018104, DP Status 0xFF018110,
 *    DP Configure 0xFF018111, and an ACK's command type;
 *  - pin assignment and Configure VDOs for a receptacle dock (C and D), a
 *    captive-plug adapter (C and E in bits 15:8), a C-only one;
 *  - the FIFO bytes of a two-object message (21 with address and register,
 *    within one RK I2C transfer).
 */
#include <stdio.h>
#include "../../../Silicon/Rockchip/RK3576/Library/Rk3576Fusb302Lib/UsbPdDp.h"

static int fails, checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            fails++;                                                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

int main(void)
{
    upd_u32 pdo = UpdSourcePdo(900);
    upd_u8 buf[40];
    upd_u32 obj[2];
    upd_u32 len;

    CHECK(UpdHeader(PD_DATA_SOURCE_CAP, 1, 0) == 0x1161, "Source_Caps header 0x%04x", UpdHeader(PD_DATA_SOURCE_CAP, 1, 0));
    CHECK(UpdHeader(PD_CTRL_ACCEPT, 0, 0) == 0x0163, "Accept");
    CHECK(UpdHeader(PD_CTRL_PS_RDY, 0, 3) == (0x0166 | 3 << 9), "PS_RDY, MessageID 3");
    CHECK(UpdHeader(PD_DATA_VENDOR_DEF, 1, 7) == (0x116F | 7 << 9), "VDM header");
    CHECK(UpdHdrObjects(0x2162) == 2 && UpdHdrType(0x2162) == PD_DATA_REQUEST, "a sink's Request header");
    CHECK(UpdIsCtrl(0x0041, PD_CTRL_GOODCRC) && !UpdIsCtrl(0x1041, PD_CTRL_GOODCRC), "GoodCRC is a control message");

    CHECK(pdo == 0x0401905Au, "PDO 5 V 900 mA USB comm: 0x%08x", pdo);
    CHECK(UpdRequestOk(1u << 28 | 90u << 10 | 90u, pdo), "Request 900/900 mA");
    CHECK(UpdRequestOk(1u << 28 | 50u << 10 | 300u | 1u << 26, pdo), "max above ours with Capability Mismatch");
    CHECK(!UpdRequestOk(1u << 28 | 50u << 10 | 300u, pdo), "max above ours without it");
    CHECK(!UpdRequestOk(2u << 28 | 50u << 10 | 50u, pdo), "object position 2 does not exist");
    CHECK(!UpdRequestOk(1u << 28 | 150u << 10 | 150u, pdo), "1.5 A operating");

    CHECK(UpdVdmHeader(UPD_SVID_PD, UPD_VDM_DISC_IDENT, 0, UPD_VDM_REQ) == 0xFF008001u, "Discover Identity");
    CHECK(UpdVdmHeader(UPD_SVID_PD, UPD_VDM_DISC_SVID, 0, UPD_VDM_REQ) == 0xFF008002u, "Discover SVIDs");
    CHECK(UpdVdmHeader(UPD_SVID_DP, UPD_VDM_DISC_MODES, 0, UPD_VDM_REQ) == 0xFF018003u, "Discover Modes");
    CHECK(UpdVdmHeader(UPD_SVID_DP, UPD_VDM_ENTER_MODE, 1, UPD_VDM_REQ) == 0xFF018104u, "Enter Mode");
    CHECK(UpdVdmHeader(UPD_SVID_DP, UPD_VDM_DP_STATUS, 1, UPD_VDM_REQ) == 0xFF018110u, "DP Status Update");
    CHECK(UpdVdmHeader(UPD_SVID_DP, UPD_VDM_DP_CONFIG, 1, UPD_VDM_REQ) == 0xFF018111u, "DP Configure");
    CHECK(UpdVdmIsAnswer(0xFF018144u, UPD_SVID_DP, UPD_VDM_ENTER_MODE), "Enter Mode ACK");
    CHECK(UpdVdmCmdType(0xFF018184u) == UPD_VDM_NAK, "Enter Mode NAK");
    CHECK(!UpdVdmIsAnswer(0xFF018104u, UPD_SVID_DP, UPD_VDM_ENTER_MODE), "a request is not an answer");
    CHECK(UpdIdModal(1u << 26 | 0x2109) && !UpdIdModal(0x2109), "modal operation bit");
    {
        upd_u32 svids[2] = { 0x8087FF01u, 0x00000000u };
        CHECK(UpdSvidsHave(svids, 2, UPD_SVID_DP), "0xFF01 in the low half");
        svids[0] = 0x05AC0000u;
        CHECK(!UpdSvidsHave(svids, 2, UPD_SVID_DP), "no DP SVID");
    }

    /* A dock with a receptacle: UFP_D, pins C and D in bits 23:16. */
    {
        upd_u32 vdo = UPD_DP_CAP_RECEPTACLE | UPD_DP_CAP_UFP_D | 1u << 2 | (UPD_DP_PIN_C | UPD_DP_PIN_D) << 16;
        CHECK(UpdDpModeUsable(vdo), "dock usable");
        CHECK(UpdDpPickPin(vdo) == UPD_DP_PIN_D, "dock: D (USB3 kept)");
        CHECK(UpdDpConfigVdo(vdo, UPD_DP_PIN_D) == 0x00000806u, "Configure D: 0x%08x", UpdDpConfigVdo(vdo, UPD_DP_PIN_D));
    }
    /* A captive-plug adapter cable: pins in bits 15:8. */
    {
        upd_u32 vdo = UPD_DP_CAP_UFP_D | 1u << 2 | (UPD_DP_PIN_C | UPD_DP_PIN_E) << 8;
        CHECK(UpdDpUfpPins(vdo) == (UPD_DP_PIN_C | UPD_DP_PIN_E), "captive plug pins");
        CHECK(UpdDpPickPin(vdo) == UPD_DP_PIN_C, "adapter: C");
        CHECK(UpdDpConfigVdo(vdo, UPD_DP_PIN_C) == 0x00000406u, "Configure C");
        CHECK(UpdDpPinName(UPD_DP_PIN_C) == 'C', "name");
    }
    /* DFP_D only (a DP source on the other end): not for us. */
    CHECK(!UpdDpModeUsable(2u | UPD_DP_CAP_RECEPTACLE | UPD_DP_PIN_C << 16), "DFP_D only");
    /* A and B only (withdrawn): not for us. */
    CHECK(!UpdDpModeUsable(1u | UPD_DP_CAP_RECEPTACLE | (UPD_DP_PIN_A | UPD_DP_PIN_B) << 16), "A/B only");
    /* Signalling 0 from the partner: Configure says 0 too. */
    CHECK(UpdDpConfigVdo(1u | UPD_DP_PIN_C << 8, UPD_DP_PIN_C) == 0x00000402u, "no signalling bit");

    obj[0] = 0xFF018111u;
    obj[1] = 0x00000806u;
    len = UpdFifoMessage(UpdHeader(PD_DATA_VENDOR_DEF, 2, 5), obj, 2, buf);
    CHECK(len == 19, "two objects: 19 FIFO bytes, %u", len);
    CHECK(buf[0] == 0x12 && buf[3] == 0x13 && buf[4] == (0x80 | 10), "SOP and PACKSYM | 10");
    CHECK(buf[5] == 0x6F && buf[6] == (0x21 | 5 << 1), "header little endian 0x%02x 0x%02x", buf[5], buf[6]);
    CHECK(buf[7] == 0x11 && buf[10] == 0xFF && buf[11] == 0x06, "objects little endian");
    CHECK(buf[15] == 0xFF && buf[16] == 0x14 && buf[17] == 0xFE && buf[18] == 0xA1, "JAM_CRC EOP TXOFF TXON");
    CHECK(UpdFifoMessage(0, obj, 8, buf) == 0, "more than seven objects refused");
    CHECK(UpdRxTokenIsSop(0xE0) && !UpdRxTokenIsSop(0xC0), "RX token SOP vs SOP'");

    printf("usbpd-host: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
