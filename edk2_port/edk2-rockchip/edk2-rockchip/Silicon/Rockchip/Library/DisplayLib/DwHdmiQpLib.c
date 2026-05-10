/** @file
  Rockchip HDMI Driver.

  Copyright (c) 2022 Rockchip Electronics Co. Ltd.
  Copyright (c) 2024-2025, Mario Bălănică <mariobalanica02@gmail.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi/UefiBaseType.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DwHdmiQpLib.h>

//
// Heavy debug instrumentation tags for the RK3576 HDMI bring-up.
// All traces are emitted at DEBUG_INFO so they survive RELEASE builds.
//
#define HDMI_TRACE(Fmt, ...)  \
  DEBUG ((DEBUG_INFO, "[RK3576-HDMI] " Fmt, ##__VA_ARGS__))
#define HDMI_DUMP_REG(Tag, Addr) \
  HDMI_TRACE ("  %a [0x%08x] = 0x%08x\n", (Tag), (UINT32)(UINTN)(Addr), MmioRead32 (Addr))
#include <Library/DrmModes.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/MediaBusFormat.h>
#include <Library/uboot-env.h>

#include <Protocol/RockchipConnectorProtocol.h>

#include <VarStoreData.h>

#define HIWORD_UPDATE(val, mask)  (val | (mask) << 16)

#define RK3588_GRF_SOC_CON2             0x0308
#define RK3588_HDMI1_HPD_INT_MSK        BIT(15)
#define RK3588_HDMI1_HPD_INT_CLR        BIT(14)
#define RK3588_HDMI0_HPD_INT_MSK        BIT(13)
#define RK3588_HDMI0_HPD_INT_CLR        BIT(12)
#define RK3588_GRF_SOC_CON7             0x031c
#define RK3588_SET_HPD_PATH_MASK        (0x3 << 12)
#define RK3588_GRF_SOC_STATUS1          0x0384
#define RK3588_HDMI0_LOW_MORETHAN100MS  BIT(20)
#define RK3588_HDMI0_HPD_PORT_LEVEL     BIT(19)
#define RK3588_HDMI0_IHPD_PORT          BIT(18)
#define RK3588_HDMI0_OHPD_INT           BIT(17)
#define RK3588_HDMI0_LEVEL_INT          BIT(16)
#define RK3588_HDMI0_INTR_CHANGE_CNT    (0x7 << 13)
#define RK3588_HDMI1_LOW_MORETHAN100MS  BIT(28)
#define RK3588_HDMI1_HPD_PORT_LEVEL     BIT(27)
#define RK3588_HDMI1_IHPD_PORT          BIT(26)
#define RK3588_HDMI1_OHPD_INT           BIT(25)
#define RK3588_HDMI1_LEVEL_INT          BIT(24)
#define RK3588_HDMI1_INTR_CHANGE_CNT    (0x7 << 21)

#define RK3588_GRF_VO1_CON3        0x000c
#define RK3588_COLOR_FORMAT_MASK   0xf
#define RK3588_YUV444              0x2
#define RK3588_YUV420              0x3
#define RK3588_COMPRESSED_DATA     0xb
#define RK3588_COLOR_DEPTH_MASK    (0xf << 4)
#define RK3588_8BPC                0
#define RK3588_10BPC               (0x6 << 4)
#define RK3588_CECIN_MASK          BIT(8)
#define RK3588_SCLIN_MASK          BIT(9)
#define RK3588_SDAIN_MASK          BIT(10)
#define RK3588_MODE_MASK           BIT(11)
#define RK3588_COMPRESS_MODE_MASK  BIT(12)
#define RK3588_I2S_SEL_MASK        BIT(13)
#define RK3588_SPDIF_SEL_MASK      BIT(14)
#define RK3588_GRF_VO1_CON4        0x0010
#define RK3588_HDMI21_MASK         BIT(0)
#define RK3588_GRF_VO1_CON9        0x0024
#define RK3588_HDMI0_GRANT_SEL     BIT(10)
#define RK3588_HDMI0_GRANT_SW      BIT(11)
#define RK3588_HDMI1_GRANT_SEL     BIT(12)
#define RK3588_HDMI1_GRANT_SW      BIT(13)
#define RK3588_GRF_VO1_CON6        0x0018
#define RK3588_GRF_VO1_CON7        0x001c

#define PPS_TABLE_LEN  8

#define COLOR_DEPTH_10BIT  BIT(31)
#define HDMI_FRL_MODE      BIT(30)
#define HDMI_EARC_MODE     BIT(29)
#define DATA_RATE_MASK     0xFFFFFFF

#define HDMI20_MAX_RATE  600000
#define HDMI_8K60_RATE   2376000

#define DDC_CI_ADDR       0x37
#define DDC_SEGMENT_ADDR  0x30
#define DDC_ADDR          0x50
#define SCDC_ADDR         0x54

#define HDMI_EDID_BLOCK_RETRIES  4

/* DW-HDMI Controller >= 0x200a are at least compliant with SCDC version 1 */
#define SCDC_MIN_SOURCE_VERSION  0x1

VOID
DwHdmiQpRegWrite (
  OUT struct  DwHdmiQpDevice  *Hdmi,
  IN  UINT32                  Value,
  IN  UINT32                  Offset
  )
{
  MmioWrite32 (Hdmi->Base + Offset, Value);
}

UINT32
DwHdmiQpRegRead (
  OUT struct  DwHdmiQpDevice  *Hdmi,
  IN  UINT32                  Offset
  )
{
  return MmioRead32 (Hdmi->Base + Offset);
}

VOID
DwHdmiQpRegMod (
  OUT struct  DwHdmiQpDevice  *Hdmi,
  IN  UINT32                  Value,
  IN  UINT32                  Mask,
  IN  UINT32                  Offset
  )
{
  UINT32  Val;

  Val  = MmioRead32 (Hdmi->Base + Offset);
  Val &= ~Mask;
  Val |= Value;

  MmioWrite32 (Hdmi->Base + Offset, Val);
}

VOID
DwHdmiQpSetIomux (
  OUT struct  DwHdmiQpDevice  *Hdmi
  )
{
  UINT32  Val;

#ifdef SOC_RK3576
  //
  // RK3576 has a single HDMI controller. Mirror the upstream Linux
  // dw_hdmi_qp_rk3576_io_init() + dw_hdmi_qp_rk3576_setup_hpd() sequence:
  //   VO0_GRF_SOC_CON14: SCLIN | SDAIN | HDMI_GRANT_SEL | I2S_SEL
  //   IOC_MISC_CON0:     HPD_INT_CLR=1, HPD_INT_MSK=0  (clear + unmask)
  //   IOC_MISC_CON1:     0xffff0102 (HPD filter / polarity, bits 1|8)
  //
  HDMI_TRACE ("SetIomux: ENTRY id=%u base=0x%lx\n", Hdmi->Id, (UINT64)Hdmi->Base);

  //
  // Step 0: Configure GPIO4 PC0-PC3 IOMUX to HDMI function 9.
  //
  // GPIO4 PC0 = hdmi_tx_cec_m0  (function 9)
  // GPIO4 PC1 = hdmi_tx_hpdin_m0 (function 9) <- HPD detection pin
  // GPIO4 PC2 = hdmi_tx_scl      (function 9)
  // GPIO4 PC3 = hdmi_tx_sda      (function 9)
  //
  // IOC_GRF = RK3588_SYS_GRF_BASE = 0x26040000
  // GPIO4-PC IOMUX register = IOC_GRF + 0xA390
  // Format: 4 bits per pin; [3:0]=PC0, [7:4]=PC1, [11:8]=PC2, [15:12]=PC3
  // HIWORD_MASK: top 16 bits = write-enable mask (0xFFFF), low 16 = values
  //

  //
  // Force GPIO4_PC1 (HPD) to INPUT in the GPIO DDR register BEFORE switching
  // the iomux to function 9.  GpioPinSetFunction() only writes the iomux
  // register — it does not touch the GPIO DDR (direction) register.  If a
  // prior boot stage left PC1 as a GPIO OUTPUT driving LOW, the DDR register
  // retains that setting even after the iomux switch.  In alt-function mode
  // the iomux overrides the signal routing, but explicitly clearing the DDR
  // output bit eliminates any residual state and keeps the diagnosis clean.
  //
  // GPIO4 base = 0x2AE40000.  DDR_H register (pins 16-31) = base + 0x000C.
  // PC1 = GPIO pin 17 = bit 1 in DDR_H.
  // HIWORD write: bits[31:16] = mask (bit17 → mask bit 1 = 0x0002<<16),
  //               bits[15:0]  = value 0 (input).
  // Combined: 0x00020000 = mask bit17, clear bit1 (INPUT).
  //
  HDMI_TRACE ("SetIomux: GPIO4_PC1 DDR -> INPUT before iomux switch\n");
  HDMI_DUMP_REG ("GPIO4_SWPORT_DDR_H  ", 0x2AE4000CUL);
  MmioWrite32 (0x2AE4000CUL, 0x00020000U);
  HDMI_DUMP_REG ("GPIO4_SWPORT_DDR_H  ", 0x2AE4000CUL);

  // rk3576-pinctrl.dtsi function map for GPIO4_PCx with fn9:
  //   PC0 fn9 = hdmi_tx_cec_m0  (CEC, OPTIONAL — mainline DTS does NOT claim it)
  //   PC1 fn9 = hdmi_tx_hpdin_m0 (HPD input — required for HPD detect path)
  //   PC2 fn9 = hdmi_tx_scl      (DDC SCL — required for EDID/SCDC)
  //   PC3 fn9 = hdmi_tx_sda      (DDC SDA — required for EDID/SCDC)
  // Leave PC0 at GPIO function (mask=0xFFF0 leaves bits[3:0] untouched).
  // CEC is not used by UEFI and the Linux mainline rock-4d DTS doesn't claim
  // it either; switching PC0 caused USB stack regressions in testing.
  HDMI_TRACE ("SetIomux: set GPIO4 PC1-PC3 -> function 9 (HPD/SCL/SDA), PC0 stays GPIO (CEC unused)\n");
  HDMI_DUMP_REG ("GPIO4_PC_IOMUX_before", RK3588_SYS_GRF_BASE + 0xA390);
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA390, 0xFFF09990U);
  HDMI_DUMP_REG ("GPIO4_PC_IOMUX_after ", RK3588_SYS_GRF_BASE + 0xA390);

  //
  // Set GPIO4_PC1 (HPD input) pull to NONE so the monitor can drive pin 19
  // HIGH.  If the pad retains a pull-down from an earlier boot stage the
  // monitor's open-drain HPD output may be overridden to LOW, making
  // IOC_HDMI_HPD_STATUS BIT(3) always read 0 even when the monitor is
  // asserting HPD.
  //
  // IOC_GRF (RK3588_SYS_GRF_BASE = 0x26040000) + 0xA3B0 is the GPIO4_PC
  // pull register.  Layout: 2 bits per pin, PC0=[1:0] PC1=[3:2] PC2=[5:4]
  // PC3=[7:6].  Values: 00=none, 01=pull-up, 10=pull-down.
  //
  // We clear PC1 pull (bits [3:2]) to 00=none.  Also clear PC2 (SCL) and
  // PC3 (SDA) pulls — DDC lines should be driven by the monitor side.
  // PC0 (CEC, unused) pull is also cleared for safety.
  //
  // HIWORD_UPDATE format: upper 16 = write-enable mask.
  //   Mask = 0x00FF (bits 7:0 = PC0-PC3 pull fields).
  //   Value = 0x0000 (all pulls = none).
  //
  HDMI_TRACE ("SetIomux: GPIO4 PC0-PC3 pull -> NONE (clear bootloader pull-down from HPD pin)\n");
  HDMI_DUMP_REG ("GPIO4_PC_PULL_before ", RK3588_SYS_GRF_BASE + 0xA3B0);
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA3B0, 0x00FF0000U);
  HDMI_DUMP_REG ("GPIO4_PC_PULL_after  ", RK3588_SYS_GRF_BASE + 0xA3B0);

  HDMI_TRACE ("SetIomux: pre-state\n");
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON14   ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14);
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON1    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON1);
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON8    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON8);
  HDMI_DUMP_REG ("IOC_MISC_CON0       ", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
  HDMI_DUMP_REG ("IOC_MISC_CON1       ", RK3588_SYS_GRF_BASE + 0xA404);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);

  Val = HIWORD_UPDATE (RK3576_SCLIN_MASK,     RK3576_SCLIN_MASK)     |
        HIWORD_UPDATE (RK3576_SDAIN_MASK,     RK3576_SDAIN_MASK)     |
        HIWORD_UPDATE (RK3576_HDMI_GRANT_SEL, RK3576_HDMI_GRANT_SEL) |
        HIWORD_UPDATE (RK3576_I2S_SEL_MASK,   RK3576_I2S_SEL_MASK);
  HDMI_TRACE ("SetIomux: write VO0_GRF_SOC_CON14 <- 0x%08x\n", Val);
  MmioWrite32 (RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14, Val);

  // Clear any pending HPD interrupt and unmask, in a single write
  // (RK3588_SYS_GRF_BASE has been remapped to RK3576 IOC_GRF @ 0x26040000).
  Val = HIWORD_UPDATE (RK3576_HDMI_HPD_INT_CLR, RK3576_HDMI_HPD_INT_CLR) |
        HIWORD_UPDATE (0,                       RK3576_HDMI_HPD_INT_MSK);
  HDMI_TRACE ("SetIomux: write IOC_MISC_CON0    <- 0x%08x (clear+unmask HPD)\n", Val);
  MmioWrite32 (RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0, Val);

  // HPD filter + routing: matches Linux kernel dw_hdmi_qp_rk3576_setup_hpd()
  // which writes regmap_write(hdmi->regmap, 0xa404, 0xffff0102).
  // bit1=1: HPD glitch filter enable.
  // bit8=1: required by kernel — likely enables HPD input path sampling.
  //         Previous code wrote 0xffff0002 (bit8=0) based on an incorrect
  //         assumption that bit8 inverts HPD polarity. The kernel always
  //         writes bit8=1, so restore it here.
  HDMI_TRACE ("SetIomux: write IOC_MISC_CON1    <- 0xffff0102 (HPD filter+enable, matches kernel)\n");
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA404, 0xffff0102);

  HDMI_TRACE ("SetIomux: post-state\n");
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON14   ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14);
  HDMI_DUMP_REG ("IOC_MISC_CON0       ", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
  HDMI_DUMP_REG ("IOC_MISC_CON1       ", RK3588_SYS_GRF_BASE + 0xA404);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  // GPIO2 EXT_PORT (0x2AE20070): raw pad levels for GPIO2 pins.
  // GPIO2_PB0 = bit8.  Should be HIGH (1) after HdmiTxIomux asserts HDMI_TX_ON_H.
  HDMI_DUMP_REG ("GPIO2_EXT_PORT      ", 0x2AE20070);
  // GPIO4 EXT_PORT (0x2AE40070): shows raw pad levels for all GPIO4 pins.
  // GPIO4_PC1 = bit17 of this register.  Should be HIGH (1) when display HPD asserted.
  // Non-zero bit17 with HPD_STATUS=0 would indicate the HDMI TX HDP circuit is
  // still frozen (SRST_HDMITXHDP not deasserted at the right time).
  HDMI_DUMP_REG ("GPIO4_EXT_PORT      ", 0x2AE40070);
  // GPIO4 DDR_H (0x2AE4000C): PC1 direction.  bit1=0 → INPUT (desired).
  HDMI_DUMP_REG ("GPIO4_SWPORT_DDR_H  ", 0x2AE4000CUL);
  // GPIO4 PC pull register (IOC_GRF + 0xA3B0): pull config for PC0-PC7.
  // bit[3:2]=PC1: 0=none, 1=pullup, 2=pulldown, 3=reserved.
  HDMI_DUMP_REG ("GPIO4_PC_P          ", 0x26040000UL + 0xA3B0UL);
  HDMI_TRACE ("SetIomux: EXIT\n");
  return;
#endif

  if (!Hdmi->Id) {
    Val = HIWORD_UPDATE (RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
          HIWORD_UPDATE (RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
          HIWORD_UPDATE (RK3588_MODE_MASK, RK3588_MODE_MASK) |
          HIWORD_UPDATE (RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON3, Val);

    Val = HIWORD_UPDATE (
            RK3588_SET_HPD_PATH_MASK,
            RK3588_SET_HPD_PATH_MASK
            );
    MmioWrite32 (RK3588_SYS_GRF_BASE + RK3588_GRF_SOC_CON7, Val);

    Val = HIWORD_UPDATE (
            RK3588_HDMI0_GRANT_SEL,
            RK3588_HDMI0_GRANT_SEL
            );
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON9, Val);
  } else {
    Val = HIWORD_UPDATE (RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
          HIWORD_UPDATE (RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
          HIWORD_UPDATE (RK3588_MODE_MASK, RK3588_MODE_MASK) |
          HIWORD_UPDATE (RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON6, Val);

    Val = HIWORD_UPDATE (
            RK3588_SET_HPD_PATH_MASK,
            RK3588_SET_HPD_PATH_MASK
            );
    MmioWrite32 (RK3588_SYS_GRF_BASE + RK3588_GRF_SOC_CON7, Val);

    Val = HIWORD_UPDATE (
            RK3588_HDMI1_GRANT_SEL,
            RK3588_HDMI1_GRANT_SEL
            );
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON9, Val);
  }
}

STATIC
BOOLEAN
DwHdmiReadHpd (
  IN struct DwHdmiQpDevice  *Hdmi
  )
{
  UINT32  Val;

#ifdef SOC_RK3576
  //
  // RK3576: HPD level is IOC_HDMI_HPD_STATUS BIT(3) or BIT(0).
  // Return the real hardware value.  If the HPD wait loop in ConnectorPreInit
  // timed out (HpdTimeoutFlag set), return TRUE anyway so that display
  // initialisation continues for capture-card / no-HPD sinks.
  //
  Val = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  {
    UINT32  Gpio4Ext   = MmioRead32 (0x2AE40070U);
    UINT32  MiscCon1   = MmioRead32 (RK3588_SYS_GRF_BASE + 0xA404U);
    UINT32  PinLevel   = (Gpio4Ext >> 17) & 1;
    BOOLEAN HwHpd      = (Val & RK3576_HDMI_LEVEL_INT) != 0;
    HDMI_TRACE (
      "ReadHpd: HPD_STATUS=0x%08x bit3=%u bit0=%u GPIO4_PC1=%u MiscCon1=0x%08x hw=%a\n",
      Val,
      (Val >> 3) & 1,
      (Val >> 0) & 1,
      PinLevel,
      MiscCon1,
      HwHpd ? "HIGH" : "LOW"
      );
    if (HwHpd) {
      return TRUE;
    }
    /* HPD is LOW.  HpdTimeoutFlag is set when the ConnectorPreInit wait loop
     * exhausted its budget — proceed so capture cards / KVMs (which never
     * assert HPD) still work. */
    if (Hdmi->HpdTimeoutFlag) {
      HDMI_TRACE ("ReadHpd: hw=LOW but HpdTimeoutFlag set -> FORCED=YES (no-HPD sink)\n");
      return TRUE;
    }
    return FALSE;
  }
#else
  Val = MmioRead32 (RK3588_SYS_GRF_BASE + RK3588_GRF_SOC_STATUS1);

  if (!Hdmi->Id) {
    return (Val & RK3588_HDMI0_LEVEL_INT) != 0;
  } else {
    return (Val & RK3588_HDMI1_LEVEL_INT) != 0;
  }
#endif
}

STATIC
EFI_STATUS
DwHdmiI2cRead (
  IN struct DwHdmiQpDevice  *Hdmi,
  UINT8                     *Buf,
  UINTN                     Length
  )
{
  struct DwHdmiQpI2c  *I2c = &Hdmi->I2c;
  EFI_STATUS          Status;
  UINT32              Timeout;
  INT32               Retry;
  UINT32              Intr;

  if (!I2c->IsRegAddr) {
    I2c->SlaveReg  = 0x0;
    I2c->IsRegAddr = TRUE;
  }

  /*
   * Note: I2CM_NBYTES > 0 seems broken - it triggers I2CM_OP_DONE_IRQ
   * before actually finishing the transfer, so we may read bogus data.
   * Read one byte at a time instead.
   */

  while (Length--) {
    DwHdmiQpRegMod (Hdmi, I2c->SlaveReg << 12, I2CM_ADDR, I2CM_INTERFACE_CONTROL0);

    for (Retry = 50; Retry > 0;) {
      if (!DwHdmiReadHpd (Hdmi)) {
        DEBUG ((DEBUG_ERROR, "%a: Disconnected! Abort.\n", __func__));
        return EFI_DEVICE_ERROR;
      }

      if (I2c->IsSegment) {
        DwHdmiQpRegMod (Hdmi, I2CM_EXT_READ, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
      } else {
        DwHdmiQpRegMod (Hdmi, I2CM_FM_READ, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
      }

      for (Timeout = 20; Timeout > 0; Timeout--) {
        MicroSecondDelay (1000);
        Intr  = DwHdmiQpRegRead (Hdmi, MAINUNIT_1_INT_STATUS);
        Intr &= (I2CM_OP_DONE_IRQ |
                 I2CM_READ_REQUEST_IRQ |
                 I2CM_NACK_RCVD_IRQ);
        if (Intr) {
          DwHdmiQpRegWrite (Hdmi, Intr, MAINUNIT_1_INT_CLEAR);
          break;
        }
      }

      if (Timeout == 0) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Timed out at offset 0x%x. Retry=%d\n",
          __func__,
          I2c->SlaveReg,
          Retry
          ));
        DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);
        DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
        Retry -= 10;
        Status = EFI_TIMEOUT;
        continue;
      }

      if (Intr & I2CM_NACK_RCVD_IRQ) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Error at offset 0x%x. Retry=%d\n",
          __func__,
          I2c->SlaveReg,
          Retry
          ));
        DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);
        DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
        Retry--;
        Status = EFI_DEVICE_ERROR;
        MicroSecondDelay (10 * 1000);
        continue;
      }

      break;
    }

    if (Retry <= 0) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed at offset 0x%x. Status=%r\n",
        __func__,
        I2c->SlaveReg,
        Status
        ));
      return Status;
    }

    *Buf = DwHdmiQpRegRead (Hdmi, I2CM_INTERFACE_RDDATA_0_3) & 0xff;
    DEBUG ((
      DEBUG_VERBOSE,
      "%a: [0x%02x] = 0x%02x\n",
      __func__,
      I2c->SlaveReg,
      *Buf
      ));
    Buf++;
    I2c->SlaveReg++;

    DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
  }

  I2c->IsSegment = FALSE;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DwHdmiI2cWrite (
  IN struct DwHdmiQpDevice  *Hdmi,
  UINT8                     *Buf,
  UINTN                     Length
  )
{
  struct DwHdmiQpI2c  *I2c = &Hdmi->I2c;
  EFI_STATUS          Status;
  UINT32              Timeout;
  INT32               Retry;
  UINT32              Intr;

  if (!I2c->IsRegAddr) {
    I2c->SlaveReg = Buf[0];
    Length--;
    Buf++;
    I2c->IsRegAddr = TRUE;
  }

  while (Length--) {
    for (Retry = 50; Retry > 0;) {
      if (!DwHdmiReadHpd (Hdmi)) {
        DEBUG ((DEBUG_ERROR, "%a: Disconnected! Abort.\n", __func__));
        return EFI_DEVICE_ERROR;
      }

      DwHdmiQpRegWrite (Hdmi, *Buf, I2CM_INTERFACE_WRDATA_0_3);
      DwHdmiQpRegMod (Hdmi, I2c->SlaveReg << 12, I2CM_ADDR, I2CM_INTERFACE_CONTROL0);
      DwHdmiQpRegMod (Hdmi, I2CM_FM_WRITE, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);

      for (Timeout = 20; Timeout > 0; Timeout--) {
        MicroSecondDelay (1000);
        Intr  = DwHdmiQpRegRead (Hdmi, MAINUNIT_1_INT_STATUS);
        Intr &= (I2CM_OP_DONE_IRQ |
                 I2CM_READ_REQUEST_IRQ |
                 I2CM_NACK_RCVD_IRQ);
        if (Intr) {
          DwHdmiQpRegWrite (Hdmi, Intr, MAINUNIT_1_INT_CLEAR);
          break;
        }
      }

      if (Timeout == 0) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Timed out at offset 0x%x. Retry=%d\n",
          __func__,
          I2c->SlaveReg,
          Retry
          ));
        DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);
        DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
        Retry -= 10;
        Status = EFI_TIMEOUT;
        continue;
      }

      if (Intr & I2CM_NACK_RCVD_IRQ) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Error at offset 0x%x. Retry=%d\n",
          __func__,
          I2c->SlaveReg,
          Retry
          ));
        DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);
        DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
        Retry--;
        Status = EFI_DEVICE_ERROR;
        MicroSecondDelay (10 * 1000);
        continue;
      }

      break;
    }

    DwHdmiQpRegMod (Hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);

    if (Retry <= 0) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed at offset 0x%x. Status=%r\n",
        __func__,
        I2c->SlaveReg,
        Status
        ));
      return Status;
    }

    DEBUG ((
      DEBUG_VERBOSE,
      "%a: [0x%02x] = 0x%02x\n",
      __func__,
      I2c->SlaveReg,
      *Buf
      ));

    Buf++;
    I2c->SlaveReg++;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DwHdmiQpI2cXfer (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN struct i2c_msg         *Msgs,
  IN INTN                   Num
  )
{
  struct DwHdmiQpI2c  *I2c   = &Hdmi->I2c;
  UINT8               Addr   = Msgs[0].addr;
  EFI_STATUS          Status = EFI_SUCCESS;

  if (Addr == DDC_CI_ADDR) {
    /*
     * The internal I2C controller does not support the multi-byte
     * read and write operations needed for DDC/CI.
     * TOFIX: Blacklist the DDC/CI address until we filter out
     * unsupported I2C operations.
     */
    return EFI_UNSUPPORTED;
  }

  DEBUG ((
    DEBUG_VERBOSE,
    "HDMI I2C xfer: Num: %d, Addr: %02x\n",
    Num,
    Addr
    ));

  for (int i = 0; i < Num; i++) {
    if (Msgs[i].len == 0) {
      DEBUG ((
        DEBUG_ERROR,
        "Unsupported transfer %d/%d, no data\n",
        i + 1,
        Num
        ));
      return EFI_UNSUPPORTED;
    }
  }

  /* Unmute DONE and ERROR interrupts */
  DwHdmiQpRegMod (
    Hdmi,
    I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
    I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
    MAINUNIT_1_INT_MASK_N
    );

  if ((Addr == DDC_SEGMENT_ADDR) && (Msgs[0].len == 1)) {
    Addr = DDC_ADDR;
  }

  DwHdmiQpRegMod (Hdmi, Addr << 5, I2CM_SLVADDR, I2CM_INTERFACE_CONTROL0);

  /* Set slave device register address on transfer */
  I2c->IsRegAddr = FALSE;

  /* Set segment pointer for I2C extended read mode operation */
  I2c->IsSegment = FALSE;

  for (int i = 0; i < Num; i++) {
    DEBUG ((
      DEBUG_VERBOSE,
      "xfer: num: %d/%d, len: %d, flags: %x\n",
      i + 1,
      Num,
      Msgs[i].len,
      Msgs[i].flags
      ));

    if ((Msgs[i].addr == DDC_SEGMENT_ADDR) && (Msgs[i].len == 1)) {
      I2c->IsSegment = TRUE;
      DwHdmiQpRegMod (
        Hdmi,
        DDC_SEGMENT_ADDR,
        I2CM_SEG_ADDR,
        I2CM_INTERFACE_CONTROL1
        );
      DwHdmiQpRegMod (
        Hdmi,
        *Msgs[i].buf << 7,
        I2CM_SEG_PTR,
        I2CM_INTERFACE_CONTROL1
        );
    } else {
      if (Msgs[i].flags & I2C_M_RD) {
        Status = DwHdmiI2cRead (Hdmi, Msgs[i].buf, Msgs[i].len);
      } else {
        Status = DwHdmiI2cWrite (Hdmi, Msgs[i].buf, Msgs[i].len);
      }
    }

    if (EFI_ERROR (Status)) {
      break;
    }
  }

  /* Mute DONE and ERROR interrupts */
  DwHdmiQpRegMod (
    Hdmi,
    0,
    I2CM_OP_DONE_MASK_N | I2CM_NACK_RCVD_MASK_N,
    MAINUNIT_1_INT_MASK_N
    );

  return Status;
}

STATIC
VOID
DwHdmiI2cInit (
  OUT struct DwHdmiQpDevice  *Hdmi
  )
{
  /*
   * Match Linux kernel dw_hdmi_qp_init_hw():
   *   1. Mask all main-unit interrupts (unmasked per-transfer in DwHdmiQpI2cXfer)
   *   2. Set 24 MHz reference clock for the DDC timer
   *   3. Software-reset the I2C master
   *   4. Set SCL timing for ~100 kHz DDC @ 24 MHz ref clock:
   *        [31:16] = high period, [15:0] = low period
   *        0x60 = 96 ref cycles = 4.0 µs high
   *        0x71 = 113 ref cycles = 4.7 µs low
   *        SCL = 24 000 000 / (96 + 113) ≈ 115 kHz (DDC-compatible)
   *      Set BOTH SM and FM SCL registers with this value so either mode
   *      gets correct timing.
   *   5. FM_EN = 0 (Standard Mode) — matches the Linux kernel.
   *      The FM_READ (BIT(2)) and FM_WRITE (BIT(1)) trigger bits work in BOTH
   *      SM and FM modes; FM_EN only selects which SCL timing register is used.
   *      Using FM_EN=1 breaks I2CM_FM_WRITE on this IP (writes time out with no
   *      MAINUNIT_1_INT_STATUS firing) while FM_EN=0 makes both reads and writes
   *      work correctly.
   *   6. Clear any stale DONE / NACK flags
   */
  DwHdmiQpRegWrite (Hdmi, 0, MAINUNIT_0_INT_MASK_N);
  DwHdmiQpRegWrite (Hdmi, 0, MAINUNIT_1_INT_MASK_N);
  DwHdmiQpRegWrite (Hdmi, 24000000, TIMER_BASE_CONFIG0);  /* 24 MHz ref clk */

  /* Software reset */
  DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);

  /* ~100 kHz SCL at 24 MHz ref: high=0x60 (4.0 µs), low=0x71 (4.7 µs) */
  DwHdmiQpRegWrite (Hdmi, 0x00600071, I2CM_SM_SCL_CONFIG0);
  DwHdmiQpRegWrite (Hdmi, 0x00600071, I2CM_FM_SCL_CONFIG0);

  /* Standard Mode (FM_EN=0) — required for FM_WRITE to function correctly */
  DwHdmiQpRegMod (Hdmi, 0, I2CM_FM_EN, I2CM_INTERFACE_CONTROL0);

  /* Clear DONE and ERROR interrupts */
  DwHdmiQpRegWrite (
    Hdmi,
    I2CM_OP_DONE_CLEAR | I2CM_NACK_RCVD_CLEAR,
    MAINUNIT_1_INT_CLEAR
    );
}

STATIC
EFI_STATUS
DwHdmiScdcRead (
  IN  struct DwHdmiQpDevice  *Hdmi,
  IN  UINT8                  Register,
  OUT UINT8                  *Value
  )
{
  struct i2c_msg  Msgs[] = {
    {
      .addr  = SCDC_ADDR,
      .flags = 0,
      .len   = 1,
      .buf   = &Register,
    },{
      .addr  = SCDC_ADDR,
      .flags = I2C_M_RD,
      .len   = sizeof (*Value),
      .buf   = Value,
    }
  };

  return DwHdmiQpI2cXfer (Hdmi, Msgs, ARRAY_SIZE (Msgs));
}

STATIC
EFI_STATUS
DwHdmiScdcWrite (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN UINT8                  Register,
  IN UINT8                  Value
  )
{
  UINT8  Buf[2] = { Register, Value };

  struct i2c_msg  Msgs[] = {
    {
      .addr  = SCDC_ADDR,
      .flags = 0,
      .len   = 1 + sizeof (Value),
      .buf   = Buf,
    }
  };

  return DwHdmiQpI2cXfer (Hdmi, Msgs, ARRAY_SIZE (Msgs));
}

STATIC
EFI_STATUS
DwHdmiReadEdidBlock (
  IN  struct DwHdmiQpDevice  *Hdmi,
  IN  UINT8                  BlockIndex,
  OUT UINT8                  *Buffer,
  IN  UINTN                  Length
  )
{
  UINT8  BaseAddr = BlockIndex * EDID_BLOCK_SIZE;
  UINT8  Segment  = BlockIndex >> 1;

  struct i2c_msg  Msgs[] = {
    {
      .addr  = DDC_SEGMENT_ADDR,
      .flags = 0,
      .len   = 1,
      .buf   = &Segment,
    },{
      .addr  = DDC_ADDR,
      .flags = 0,
      .len   = 1,
      .buf   = &BaseAddr,
    },{
      .addr  = DDC_ADDR,
      .flags = I2C_M_RD,
      .len   = Length,
      .buf   = Buffer,
    }
  };

  UINT8  SkipMsg = (Segment > 0) ? 0 : 1;

  return DwHdmiQpI2cXfer (Hdmi, Msgs + SkipMsg, ARRAY_SIZE (Msgs) - SkipMsg);
}

EFI_STATUS
DwHdmiQpConnectorPreInit (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  CONNECTOR_STATE        *ConnectorState = &DisplayState->ConnectorState;
  struct DwHdmiQpDevice  *Hdmi;

  Hdmi = DW_HDMI_QP_FROM_CONNECTOR_PROTOCOL (This);

  HDMI_TRACE (
    "ConnectorPreInit: ENTRY id=%u base=0x%lx out_if=0x%x signaling=%u\n",
    Hdmi->Id,
    (UINT64)Hdmi->Base,
    Hdmi->OutputInterface,
    Hdmi->SignalingMode
    );

  ConnectorState->Type            = DRM_MODE_CONNECTOR_HDMIA;
  ConnectorState->OutputInterface = Hdmi->OutputInterface;

  HDMI_TRACE ("ConnectorPreInit: -> HdmiTxIomux(%u)\n", Hdmi->Id);
  HdmiTxIomux (Hdmi->Id);

  HDMI_TRACE ("ConnectorPreInit: -> DwHdmiQpSetIomux\n");
  DwHdmiQpSetIomux (Hdmi);

#ifdef SOC_RK3576
  //
  // Wait up to 5000 ms for the monitor to assert HPD after detecting HDMI
  // signal presence.  Monitors need tens to hundreds of milliseconds after
  // the TMDS lines become active before they drive HPD HIGH.
  // Print status every 200 ms, showing both IOC register and raw GPIO4_PC1
  // pad level so we can distinguish "IOC not routing HPD" from "no HPD".
  //
  {
    UINT32   HpdWaitMs  = 0;
    UINT32   HpdStatus;
    BOOLEAN  HpdHigh    = FALSE;

    HDMI_TRACE ("ConnectorPreInit: waiting for HPD (max 5000 ms)...\n");
    Hdmi->HpdTimeoutFlag = FALSE;

    while (HpdWaitMs < 5000) {
      UINT32  Gpio4Ext;
      UINT32  PadLevel;

      HpdStatus = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
      Gpio4Ext  = MmioRead32 (0x2AE40070U);
      PadLevel  = (Gpio4Ext >> 17) & 1U;

      if ((HpdStatus & RK3576_HDMI_LEVEL_INT) != 0) {
        DEBUG ((DEBUG_INFO,
          "[RK3576-HDMI] HPD HIGH after %u ms (HPD_STATUS=0x%08x GPIO4_PC1_pad=%u)\n",
          HpdWaitMs, HpdStatus, PadLevel));
        HpdHigh = TRUE;
        break;
      }
      /* Log every 200 ms: show both the IOC software status and the raw pad
       * level.  If PadLevel=1 but HPD_STATUS bit3=0, the IOC is not routing
       * the HPD signal correctly.  If PadLevel=0, the monitor is not
       * asserting HPD (board hardware issue or timing issue). */
      if ((HpdWaitMs % 200) == 0) {
        DEBUG ((DEBUG_INFO,
          "[RK3576-HDMI] HPD wait %u ms HPD_STATUS=0x%08x bit3=%u GPIO4_PC1_pad=%u\n",
          HpdWaitMs, HpdStatus, (HpdStatus >> 3) & 1U, PadLevel));
      }
      MicroSecondDelay (10 * 1000);  /* 10 ms */
      HpdWaitMs += 10;
    }

    if (!HpdHigh) {
      UINT32  Gpio4Ext = MmioRead32 (0x2AE40070U);
      HpdStatus = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
      DEBUG ((DEBUG_WARN,
        "[RK3576-HDMI] HPD timeout after 5000 ms (HPD_STATUS=0x%08x bit3=%u GPIO4_PC1_pad=%u) "
        "— proceeding (no-HPD sink?)\n",
        HpdStatus, (HpdStatus >> 3) & 1U, (Gpio4Ext >> 17) & 1U));
      Hdmi->HpdTimeoutFlag = TRUE;
    }
  }
#endif

  HDMI_TRACE ("ConnectorPreInit: -> DwHdmiI2cInit\n");
  DwHdmiI2cInit (Hdmi);

  HDMI_TRACE ("ConnectorPreInit: EXIT (HPD now=%a)\n",
              DwHdmiReadHpd (Hdmi) ? "HIGH" : "LOW");
  return EFI_SUCCESS;
}

EFI_STATUS
DwHdmiQpConnectorInit (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  CONNECTOR_STATE  *ConnectorState = &DisplayState->ConnectorState;

  ConnectorState->OutputMode = ROCKCHIP_OUT_MODE_AAAA;
  ConnectorState->ColorSpace = V4L2_COLORSPACE_DEFAULT;

  return EFI_SUCCESS;
}

EFI_STATUS
DwHdmiQpConnectorGetEdid (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  struct DwHdmiQpDevice  *Hdmi;
  CONNECTOR_STATE        *ConnectorState;
  EFI_STATUS             Status;
  UINT32                 Retry;
  UINT32                 BlockIndex;
  UINT32                 Extensions;
  UINT8                  *Buffer;

  Hdmi           = DW_HDMI_QP_FROM_CONNECTOR_PROTOCOL (This);
  ConnectorState = &DisplayState->ConnectorState;

  //
  // RK3576 bring-up note: attempt the real DDC/EDID read with the fixed
  // I2C init (TIMER_BASE_CONFIG0 + I2CM_FM_SCL_CONFIG0).  If DDC fails
  // for any reason (HPD low, no monitor, bus error), fall back to a
  // hardcoded 1920x1080@60 preferred mode so display still initialises.
  //
  // The Rk3576FallbackMode label at the end of this function implements
  // the fallback; only the SOC_RK3576 code paths jump to it.
  //
#ifndef SOC_RK3576
  (void)0;  /* suppress empty-block warning on non-RK3576 builds */
#else
  HDMI_TRACE ("GetEdid: RK3576 attempting DDC EDID read (I2C fix applied)\n");
#endif

  for (BlockIndex = 0, Extensions = 0; BlockIndex <= Extensions; BlockIndex++) {
    Buffer = EDID_BLOCK (ConnectorState->Edid, BlockIndex);

    for (Retry = HDMI_EDID_BLOCK_RETRIES; Retry > 0; Retry--) {
      Status = DwHdmiReadEdidBlock (Hdmi, BlockIndex, Buffer, EDID_BLOCK_SIZE);
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Failed to read EDID block %u. Status=%r\n",
          __func__,
          BlockIndex,
          Status
          ));
#ifdef SOC_RK3576
        goto Rk3576FallbackMode;
#else
        return Status;
#endif
      }

      Status = CheckEdidBlock (Buffer, BlockIndex);
      if (EFI_ERROR (Status)) {
        /* Might be corrupted due to a bus condition, try again. */
        continue;
      }

      break;
    }

    if (Retry == 0) {
#ifdef SOC_RK3576
      goto Rk3576FallbackMode;
#else
      return Status;
#endif
    }

    if (BlockIndex == 0) {
      Extensions = ((EDID_BASE *)ConnectorState->Edid)->ExtensionFlag;
      if (Extensions > EDID_MAX_EXTENSION_BLOCKS) {
        DEBUG ((
          DEBUG_WARN,
          "%a: Reading only %u extensions out of %u reported.\n",
          __func__,
          EDID_MAX_EXTENSION_BLOCKS,
          Extensions
          ));
        Extensions = EDID_MAX_EXTENSION_BLOCKS;
      }
    }
  }

  HDMI_TRACE ("GetEdid: DDC EDID read succeeded\n");
  return EFI_SUCCESS;

#ifdef SOC_RK3576
Rk3576FallbackMode:
  //
  // DDC/I2C EDID read failed (monitor not connected, HPD low, or bus error).
  // Fall back to a hardcoded 1920x1080@60 preferred mode so that display
  // initialisation can proceed regardless.  Mark sink as HDMI so that AVI
  // and GCP infoframes are sent correctly.
  //
  HDMI_TRACE ("GetEdid: RK3576 DDC failed — falling back to hardcoded 1920x1080@60\n");
  ConnectorState->SinkInfo.IsHdmi                        = TRUE;
  ConnectorState->SinkInfo.PreferredMode.Vic              = 16;
  ConnectorState->SinkInfo.PreferredMode.OscFreq          = 148500;
  ConnectorState->SinkInfo.PreferredMode.HActive          = 1920;
  ConnectorState->SinkInfo.PreferredMode.HFrontPorch      = 88;
  ConnectorState->SinkInfo.PreferredMode.HSync            = 44;
  ConnectorState->SinkInfo.PreferredMode.HBackPorch       = 148;
  ConnectorState->SinkInfo.PreferredMode.HSyncActive      = 1;
  ConnectorState->SinkInfo.PreferredMode.VActive          = 1080;
  ConnectorState->SinkInfo.PreferredMode.VFrontPorch      = 4;
  ConnectorState->SinkInfo.PreferredMode.VSync            = 5;
  ConnectorState->SinkInfo.PreferredMode.VBackPorch       = 36;
  ConnectorState->SinkInfo.PreferredMode.VSyncActive      = 1;
  ConnectorState->SinkInfo.PreferredMode.DenActive        = 0;
  ConnectorState->SinkInfo.PreferredMode.ClkActive        = 0;
  return EFI_SUCCESS;
#endif
}

VOID
Rk3588SetColorFormat (
  OUT struct DwHdmiQpDevice  *Hdmi,
  IN UINT64                  BusFormat,
  IN UINT32                  Depth
  )
{
  UINT32  Val = 0;

  switch (BusFormat) {
    case MEDIA_BUS_FMT_RGB888_1X24:
    case MEDIA_BUS_FMT_RGB101010_1X30:
      Val = HIWORD_UPDATE (0, RK3588_COLOR_FORMAT_MASK);
      break;
    case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
    case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
      Val = HIWORD_UPDATE (RK3588_YUV420, RK3588_COLOR_FORMAT_MASK);
      break;
    case MEDIA_BUS_FMT_YUV8_1X24:
    case MEDIA_BUS_FMT_YUV10_1X30:
      Val = HIWORD_UPDATE (RK3588_YUV444, RK3588_COLOR_FORMAT_MASK);
      break;
    default:
      DEBUG ((DEBUG_INFO, "%a can't set correct color format\n", __func__));
      return;
  }

  if (Depth == 8) {
    Val |= HIWORD_UPDATE (RK3588_8BPC, RK3588_COLOR_DEPTH_MASK);
  } else {
    Val |= HIWORD_UPDATE (RK3588_10BPC, RK3588_COLOR_DEPTH_MASK);
  }

#ifdef SOC_RK3576
  //
  // RK3576 VO0_GRF_SOC_CON8: write BOTH color DEPTH (bits[11:8]) and
  // COLOR_FORMAT (bits[7:4]). Matches mainline dw_hdmi_qp_rk3576_enc_init():
  //   FIELD_PREP_WM16(RK3576_COLOR_DEPTH_MASK, RK3576_8BPC) |
  //   FIELD_PREP_WM16(RK3576_COLOR_FORMAT_MASK, RK3576_RGB)
  // Expected write for 8bpc+RGB: 0x0FF00090, readback: 0x00000090.
  //
  Val = HIWORD_UPDATE ((Depth == 8) ? RK3576_8BPC : RK3576_10BPC,
                       RK3576_COLOR_DEPTH_MASK)
      | HIWORD_UPDATE (RK3576_RGB, RK3576_COLOR_FORMAT_MASK);
  MmioWrite32 (RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON8, Val);
  return;
#endif

  if (!Hdmi->Id) {
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON3, Val);
  } else {
    MmioWrite32 (RK3588_VO1_GRF_BASE + RK3588_GRF_VO1_CON6, Val);
  }
}

STATIC
VOID
HdmiInfoframeSetChecksum (
  IN OUT UINT8   *Buf,
  IN     UINT32  Size
  )
{
  UINT8   Checksum = 0;
  UINT32  Index;

  Buf[3] = 0;
  for (Index = 0; Index < Size; Index++) {
    Checksum += Buf[Index];
  }

  Buf[3] = 256 - Checksum;
}

STATIC
VOID
HdmiConfigAviInfoframe (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN DISPLAY_STATE          *DisplayState,
  IN UINT32                 Vic
  )
{
  CONNECTOR_STATE    *ConnectorState;
  DISPLAY_SINK_INFO  *SinkInfo;
  UINT8              InfBuf[17];
  UINT32             val, i, j;

  ConnectorState = &DisplayState->ConnectorState;
  SinkInfo       = &ConnectorState->SinkInfo;

  ZeroMem (InfBuf, sizeof (InfBuf));

  InfBuf[0] = 0x82; /* Type = AVI */
  InfBuf[1] = 2;    /* Version */
  InfBuf[2] = 13;   /* Length */

  InfBuf[4] = 0x2;        /* Scan Information = Underscan */
  InfBuf[7] = Vic & 0xff; /* VIC */

  if (SinkInfo->SelectableRgbRange) {
    InfBuf[6] = 0x2 << 2; /* RGB Quantization Range = Full */
  }

  HdmiInfoframeSetChecksum (InfBuf, sizeof (InfBuf));

  /*
   * The Designware IP uses a different byte format from standard
   * AVI info frames, though generally the bits are in the correct
   * bytes.
   */
  val = (InfBuf[1] << 8) | (InfBuf[2] << 16);
  DwHdmiQpRegWrite (Hdmi, val, PKT_AVI_CONTENTS0);

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      if (i * 4 + j >= 14) {
        break;
      }

      if (!j) {
        val = InfBuf[i * 4 + j + 3];
      }

      val |= InfBuf[i * 4 + j + 3] << (8 * j);
    }

    DwHdmiQpRegWrite (Hdmi, val, PKT_AVI_CONTENTS1 + i * 4);
  }

  DwHdmiQpRegMod (Hdmi, 0, PKTSCHED_AVI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
  DwHdmiQpRegMod (Hdmi, PKTSCHED_AVI_TX_EN, PKTSCHED_AVI_TX_EN, PKTSCHED_PKT_EN);
}

STATIC
VOID
HdmiConfigVendorInfoframe (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN DISPLAY_STATE          *DisplayState,
  IN UINT32                 Vic
  )
{
  UINT8   InfBuf[9];
  UINT32  val, reg, i;

  DwHdmiQpRegMod (Hdmi, 0, PKTSCHED_VSI_TX_EN, PKTSCHED_PKT_EN);

  if (Vic == 0) {
    return;
  }

  ZeroMem (InfBuf, sizeof (InfBuf));

  InfBuf[0] = 0x81; /* Type = HDMI */
  InfBuf[1] = 1;    /* Version */
  InfBuf[2] = 5;    /* Length */

  /* HDMI OUI */
  InfBuf[4] = 0x03;
  InfBuf[5] = 0x0c;
  InfBuf[6] = 0x00;

  InfBuf[7] = 0x1 << 5;   /* Video Format */
  InfBuf[8] = Vic & 0xff; /* VIC */

  HdmiInfoframeSetChecksum (InfBuf, sizeof (InfBuf));

  /* vsi header */
  val = (InfBuf[2] << 16) | (InfBuf[1] << 8) | InfBuf[0];
  DwHdmiQpRegWrite (Hdmi, val, PKT_VSI_CONTENTS0);

  reg = PKT_VSI_CONTENTS1;
  for (i = 3; i < sizeof (InfBuf); i++) {
    if (i % 4 == 3) {
      val = InfBuf[i];
    }

    if (i % 4 == 0) {
      val |= InfBuf[i] << 8;
    }

    if (i % 4 == 1) {
      val |= InfBuf[i] << 16;
    }

    if (i % 4 == 2) {
      val |= InfBuf[i] << 24;
    }

    if ((i % 4 == 2) || (i == (sizeof (InfBuf) - 1))) {
      DwHdmiQpRegWrite (Hdmi, val, reg);
      reg += 4;
    }
  }

  DwHdmiQpRegWrite (Hdmi, 0, PKT_VSI_CONTENTS7);

  DwHdmiQpRegMod (Hdmi, 0, PKTSCHED_VSI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
  DwHdmiQpRegMod (Hdmi, PKTSCHED_VSI_TX_EN, PKTSCHED_VSI_TX_EN, PKTSCHED_PKT_EN);
}

STATIC
VOID
HdmiConfigInfoframes (
  IN struct DwHdmiQpDevice  *Hdmi,
  IN DISPLAY_STATE          *DisplayState
  )
{
  CONNECTOR_STATE    *ConnectorState;
  DISPLAY_SINK_INFO  *SinkInfo;
  UINT32             CeaVic;
  UINT32             HdmiVic;

  ConnectorState = &DisplayState->ConnectorState;
  SinkInfo       = &ConnectorState->SinkInfo;

  /*
   * XXX: We currently output full range RGB, while modes with
   * VIC > 1 expect limited range. Ideally we'd set the VIC to 0,
   * but there's concern that some sinks might get confused by that
   * and refuse to display anything.
   */
  CeaVic = ConnectorState->DisplayModeVic;

  /* Convert CEA-defined VIC to HDMI-defined one if applicable (4K). */
  HdmiVic = ConvertCeaToHdmiVic (CeaVic);

  /*
   * HDMI VIC must only be set in vendor infoframe.
   * VIC in AVI infoframe must be <= 64 for HDMI 1.x.
   */
  if ((HdmiVic > 0) || (!SinkInfo->HdmiInfo.Hdmi20Supported && (CeaVic > 64))) {
    CeaVic = 0;
  }

  HdmiConfigAviInfoframe (Hdmi, DisplayState, CeaVic);

  HdmiConfigVendorInfoframe (Hdmi, DisplayState, HdmiVic);
}

EFI_STATUS
DwHdmiQpSetup (
  OUT struct DwHdmiQpDevice  *Hdmi,
  OUT DISPLAY_STATE          *DisplayState
  )
{
  struct RockchipHdptxPhyHdmi  *Hdptx;
  CONNECTOR_STATE              *ConnectorState;
  DISPLAY_SINK_INFO            *SinkInfo;
  EFI_STATUS                   Status;
  UINT32                       BitRate;
  BOOLEAN                      HdmiMode;

  Hdptx          = &Hdmi->HdptxPhy;
  ConnectorState = &DisplayState->ConnectorState;
  SinkInfo       = &ConnectorState->SinkInfo;

  BitRate = ConnectorState->DisplayMode.Clock * 10;

  HDMI_TRACE (
    "Setup: ENTRY id=%u clk_kHz=%u BitRate=%u sink_isHdmi=%u scdc=%u\n",
    Hdmi->Id,
    ConnectorState->DisplayMode.Clock,
    BitRate,
    SinkInfo->IsHdmi,
    SinkInfo->HdmiInfo.ScdcSupported
    );

  /* ── STEP 1: Pre-state ────────────────────────────────────────────────── */
  HDMI_TRACE ("Setup: [1] PRE-STATE dump\n");
  HDMI_DUMP_REG ("  CMU_STATUS        ", Hdmi->Base + CMU_STATUS);
  HDMI_DUMP_REG ("  CMU_CONFIG0       ", Hdmi->Base + CMU_CONFIG0);
  HDMI_DUMP_REG ("  GLOBAL_SWDISABLE  ", Hdmi->Base + GLOBAL_SWDISABLE);
  HDMI_DUMP_REG ("  RST_MGR_STATUS0   ", Hdmi->Base + RESET_MANAGER_STATUS0);
  HDMI_DUMP_REG ("  MAIN_STATUS0      ", Hdmi->Base + MAINUNIT_STATUS0);
  HDMI_DUMP_REG ("  VID_IF_CONFIG0    ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
  HDMI_DUMP_REG ("  VID_IF_STATUS0    ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
  /*
   * LINK_CONFIG0 (0x968) and TMDS_FIFO_CONFIG0 (0x970) live in the link QP
   * clock domain.  On cold boot this clock is only available AFTER the HDPTX
   * PHY PLL is locked (step [3]).  Reading them here causes a synchronous
   * external abort (EC=0x25).  They are dumped below after step [3] instead.
   */
  HDMI_DUMP_REG ("  HDPTX_GRF_CON0    ", 0x26032000UL + 0x00);
  HDMI_DUMP_REG ("  HDPTX_GRF_STATUS  ", 0x26032000UL + 0x80);
  HDMI_DUMP_REG ("  PMU1CRU_GATE_CON0 ", 0x27220000UL + 0x800);
  HDMI_DUMP_REG ("  CRU_GATE_CON64    ", 0x27200000UL + 0x800 + 64 * 4);
  HDMI_DUMP_REG ("  CRU_SOFTRST_CON28 ", 0x27200000UL + 0xA70);

  /* ── STEP 2: Enable HDMI TX QP clocks + AVP video datapath ─────────── */
  HDMI_TRACE ("Setup: [2] Enable QP clocks (CMU_CONFIG0) + AVP video datapath (GLOBAL_SWDISABLE)\n");
  /*
   * Clear VIDQPCLK_OFF (bit3) and LINKQPCLK_OFF (bit5) in CMU_CONFIG0 to
   * un-gate the video/link QP clocks inside the HDMI TX controller.
   * Without this the serializer has no clock even if the PHY PLL is running.
   */
  DwHdmiQpRegMod (Hdmi, 0, VIDQPCLK_OFF | LINKQPCLK_OFF, CMU_CONFIG0);
  /*
   * Enable AVP video datapath by clearing AVP_DATAPATH_VIDEO_SWDISABLE
   * (bit6) in GLOBAL_SWDISABLE.  With this bit set the HDMI TX will not
   * output any TMDS signal even if video frames are arriving from VOP2.
   */
  DwHdmiQpRegMod (Hdmi, 0, AVP_DATAPATH_VIDEO_SWDISABLE, GLOBAL_SWDISABLE);
  HDMI_DUMP_REG ("  CMU_CONFIG0 post  ", Hdmi->Base + CMU_CONFIG0);
  HDMI_DUMP_REG ("  CMU_STATUS  post  ", Hdmi->Base + CMU_STATUS);
  HDMI_DUMP_REG ("  SWDISABLE   post  ", Hdmi->Base + GLOBAL_SWDISABLE);

  /* ── STEP 3: PHY PLL configure for TMDS ─────────────────────────────── */
  HDMI_TRACE ("Setup: [3] PHY PLL configure BitRate=%u kbps (1485000 = 1080p60)\n", BitRate);
  HDMI_DUMP_REG ("  HDPTX_STATUS pre  ", 0x26032000UL + 0x80);
  Status = HdptxRopllCmnConfig (Hdptx, BitRate);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to enable PHY PLL. Status=%r\n", __func__, Status));
    HDMI_TRACE ("Setup: [3] PHY PLL FAIL Status=%r\n", Status);
    HDMI_DUMP_REG ("  HDPTX_STATUS fail ", 0x26032000UL + 0x80);
    HDMI_DUMP_REG ("  HDPTX_GRF_CON0    ", 0x26032000UL + 0x00);
    return Status;
  }
  HDMI_TRACE ("Setup: [3] PHY PLL OK — PHY_CLK_RDY locked\n");
  HDMI_DUMP_REG ("  HDPTX_STATUS post ", 0x26032000UL + 0x80);
  HDMI_DUMP_REG ("  CMU_STATUS postPLL", Hdmi->Base + CMU_STATUS);
  /* Link QP clock domain now running — safe to read link registers. */
  HDMI_DUMP_REG ("  LINK_CONFIG0 postPLL", Hdmi->Base + LINK_CONFIG0);
  HDMI_DUMP_REG ("  TMDS_FIFO_CFG0 postPLL", Hdmi->Base + TMDS_FIFO_CONFIG0);

#ifdef SOC_RK3576
  /* ── STEP 4: VP0 DCLK mux → clk_hdmiphy_pixel0 ──────────────────────── */
  /*
   * HDPTX PHY PLL is now locked; clk_hdmiphy_pixel0 is running.
   * Switch VP0 DCLK mux to clk_hdmiphy_pixel0 via CLKSEL_CON(147) bit11.
   *
   * From Linux kernel clk-rk3576.c:
   *   PNAME(dclk_vp0_p) = { "dclk_vp0_src", "clk_hdmiphy_pixel0" };
   *   COMPOSITE_NODIV(DCLK_VP0, dclk_vp0_p, RK3576_CLKSEL_CON(147), 11, 1, ...)
   *     bit11 = 0 → dclk_vp0_src (PLL divider path)
   *     bit11 = 1 → clk_hdmiphy_pixel0 (HDPTX PHY pixel clock)
   *
   * CRU CLKSEL base = 0x300; CLKSEL_CON(147) = 0x300 + 147*4 = 0x54C
   * HIWORD write-mask format: bits[31:16]=bitmask, bits[15:0]=value
   *   mask = 0x0800 (bit11 only), value = 0x0800 (bit11 = 1)
   */
  HDMI_TRACE ("Setup: [4] CLKSEL_CON147 bit11 <- 1 (VP0 DCLK -> clk_hdmiphy_pixel0)\n");
  /* Snapshot VP0 standby state BEFORE we switch the DCLK mux.  The vendor
   * binary always shows VP0 in standby (bit31=1) at this point because its
   * Setup() runs before the framework's Crtc->Enable().  Our framework calls
   * Vop2Enable() first, so we expect bit31=0 here — which is exactly what
   * makes the mux switch happen mid-frame and freezes HDMITX. */
  {
    UINT32  DspCtrlPreMux = MmioRead32 (0x27D00000UL + 0xC00U);
    HDMI_TRACE ("Setup: [4]  VP0_DSP_CTRL pre-mux = 0x%08x  STANDBY=%u\n",
                DspCtrlPreMux, (DspCtrlPreMux >> 31) & 1U);
  }
  MmioWrite32 (0x27200000UL + 0x054C, 0x08000800U);
  HDMI_DUMP_REG ("  CLKSEL_CON147 post", 0x27200000UL + 0x054C);
  HDMI_DUMP_REG ("  CLKSEL_CON145 src ", 0x27200000UL + 0x0544);  /* dclk_vp0_src mux/div */
  HDMI_DUMP_REG ("  CMU_STATUS postMux", Hdmi->Base + CMU_STATUS);

  /* [4b] VP0 standby cycle removed — mainline does not do this; relies on
   * VOP2 already running with the right clock once DCLK mux is switched. */

  /* ── STEP 4c: VO0 GRF CON1 = TMDS link mode ─────────────────────────── */
  /*
   * BSP rk3576_set_link_mode() explicitly writes bit0=0 to VO0_GRF_SOC_CON1
   * to ensure TMDS link mode (not FRL).  Bit0=1 = FRL mode, which requires
   * a completely different link setup and causes garbage on the TMDS output.
   * GRF registers survive soft reset, so a previous boot or ATF code could
   * have left this set.  Write it explicitly to guarantee TMDS mode.
   */
  HDMI_TRACE ("Setup: [4c] VO0_GRF_SOC_CON1 <- TMDS link mode (bit0=0)\n");
  MmioWrite32 (RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON1, HIWORD_UPDATE (0, BIT (0)));
  HDMI_DUMP_REG ("  VO0_GRF_CON1 post ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON1);
#endif

  /* ── STEP 5: Color format ────────────────────────────────────────────── */
  HDMI_TRACE ("Setup: [5] SetColorFormat RGB888 8bpc -> VO0_GRF_SOC_CON8\n");
  Rk3588SetColorFormat (Hdmi, MEDIA_BUS_FMT_RGB888_1X24, 8);
  HDMI_DUMP_REG ("  VO0_GRF_SOC_CON8 post", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON8);

  /* ── STEP 6: HDCP2 bypass ───────────────────────────────────────────── */
  HDMI_TRACE ("Setup: [6] HDCP2 bypass\n");
  DwHdmiQpRegMod (Hdmi, HDCP2_BYPASS, HDCP2_BYPASS, HDCP2LOGIC_CONFIG0);

  /* ── STEP 7: HDMI / DVI mode ────────────────────────────────────────── */
  if (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_AUTO) {
    HdmiMode = SinkInfo->IsHdmi;
  } else {
    HdmiMode = (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_HDMI);
  }
  HDMI_TRACE (
    "Setup: [7] mode=%a (SignalingMode=%u IsHdmi=%u) -> LINK_CONFIG0\n",
    HdmiMode ? "HDMI" : "DVI",
    Hdmi->SignalingMode,
    SinkInfo->IsHdmi
    );
  /* Clear OPMODE_DVI, OPMODE_FRL, OPMODE_FRL_4LANES → TMDS + HDMI/DVI mode.
   * BSP hdmi_set_op_mode() explicitly clears FRL bits as part of TMDS setup.
   * Reset value is 0 but GRF-persistent state may differ across warm reboots. */
  DwHdmiQpRegMod (Hdmi, HdmiMode ? 0 : OPMODE_DVI,
                  OPMODE_DVI | OPMODE_FRL | OPMODE_FRL_4LANES, LINK_CONFIG0);
  HDMI_DUMP_REG ("  LINK_CONFIG0 now  ", Hdmi->Base + LINK_CONFIG0);

  /* ── STEP 7b: Disable scrambling ────────────────────────────────────── */
  /*
   * BSP dw_hdmi_qp_setup() writes 0 to SCRAMB_CONFIG0 for TMDS rates
   * at or below 340 MHz (no scrambling required).  The reset value is 0 but
   * writing it explicitly guarantees we're not inheriting stale state from a
   * previous firmware run that may have enabled scrambling.
   */
  DwHdmiQpRegWrite (Hdmi, 0, SCRAMB_CONFIG0);
  HDMI_DUMP_REG ("  SCRAMB_CONFIG0    ", Hdmi->Base + SCRAMB_CONFIG0);



  /* ── STEP 9: AVI infoframe ───────────────────────────────────────────── */
  if (HdmiMode) {
    HDMI_TRACE ("Setup: [9] HdmiConfigInfoframes (AVI)\n");
    DwHdmiQpRegMod (Hdmi, KEEPOUT_REKEY_ALWAYS, KEEPOUT_REKEY_CFG, FRAME_COMPOSER_CONFIG9);
    HdmiConfigInfoframes (Hdmi, DisplayState);
  }

  /* ── STEP 10: PHY lane configure + PostEnableLane ────────────────────── */
  HDMI_TRACE ("Setup: [10] HdptxRopllTmdsModeConfig — lane init + deassert lane reset\n");
  HDMI_DUMP_REG ("  HDPTX_STATUS preLN", 0x26032000UL + 0x80);
  Status = HdptxRopllTmdsModeConfig (Hdptx, BitRate);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: PHY lane config failed. Status=%r\n", __func__, Status));
    HDMI_TRACE ("Setup: [10] Lanes FAIL Status=%r\n", Status);
    HDMI_DUMP_REG ("  HDPTX_STATUS failLN", 0x26032000UL + 0x80);
    HDMI_DUMP_REG ("  HDPTX_GRF_CON0    ", 0x26032000UL + 0x00);
    return Status;
  }
  HDMI_TRACE ("Setup: [10] Lanes OK — PHY_RDY + PLL_LOCK_DONE\n");
  HDMI_DUMP_REG ("  HDPTX_STATUS postLN", 0x26032000UL + 0x80);
  HDMI_DUMP_REG ("  CMU_STATUS  postLN ", Hdmi->Base + CMU_STATUS);

  /* HDPTX PHY internal register readback (whitelist: only registers
   * explicitly written by HdptxRopllCmnConfig / HdptxRopllTmdsModeConfig /
   * HdptxPostEnableLane).  PHY base = HDMI0TX_PHY_BASE = 0x2B000000.
   * Expected values for 1080p60 TMDS (BitRate=1485000 kbps) shown inline. */
  HDMI_TRACE ("Setup: [10c] HDPTX PHY internal regs (whitelist)\n");
  HDMI_DUMP_REG ("  LNTOP_R0200(mode)  ", HDMI0TX_PHY_BASE + 0x0800); /* want 0x06 = HDMI+TMDS    */
  HDMI_DUMP_REG ("  LNTOP_R0201(div)   ", HDMI0TX_PHY_BASE + 0x0804); /* want 0x07 = 1/10 rate    */
  HDMI_DUMP_REG ("  LNTOP_R0206(bus)   ", HDMI0TX_PHY_BASE + 0x0818); /* want 0x07 = 40-bit bus   */
  HDMI_DUMP_REG ("  LNTOP_R0207(lnen)  ", HDMI0TX_PHY_BASE + 0x081C); /* want 0x0F = 4 lanes en   */
  HDMI_DUMP_REG ("  CMN_R0086(pcgclk)  ", HDMI0TX_PHY_BASE + 0x0218); /* PLL_PCG_CLK_EN set?      */
  HDMI_DUMP_REG ("  CMN_R0087          ", HDMI0TX_PHY_BASE + 0x021C); /* want 0x04                */
  HDMI_DUMP_REG ("  CMN_R009A(spd)     ", HDMI0TX_PHY_BASE + 0x0268); /* want 0x11 = HS_SPEED_SEL */
  HDMI_DUMP_REG ("  SB_R0114           ", HDMI0TX_PHY_BASE + 0x0450); /* want 0x00                */
  HDMI_DUMP_REG ("  LN0_R0303(drv)     ", HDMI0TX_PHY_BASE + 0x0C0C); /* want 0x2F (final write)  */
  HDMI_DUMP_REG ("  LN0_R030A(amp)     ", HDMI0TX_PHY_BASE + 0x0C28); /* want 0x17                */
  HDMI_DUMP_REG ("  LN0_R030B(swing)   ", HDMI0TX_PHY_BASE + 0x0C2C); /* want 0x77                */
  HDMI_DUMP_REG ("  LN0_R0312(rate)    ", HDMI0TX_PHY_BASE + 0x0C48); /* want 0x00 = RBR rate     */
  HDMI_DUMP_REG ("  GRF_CON0(bias/bgr) ", 0x26032000UL + 0x00);       /* BIAS_EN|BGR_EN|PLL_EN    */

  /* ── STEP 10d: HDMITX link trigger + VP0 standby→enable sequence ────────
   *
   * Reverse-engineering of the vendor "all-vendor-EDK2" binary shows:
   *
   *   PHY lanes ready          (= our Step 10 — already done above)
   *   LINK_CONFIG0 |= FRL_START  ← vendor logs "lnk=10 lnk_rb=0"
   *   VP0 standby toggle:
   *     STANDBY = 1, commit, wait ~20 ms (HDMITX FIFO drains)
   *     STANDBY = 0, commit
   *   wait for VID_MON_ST0 frame counter to advance
   *
   * SAFETY — clock-gating abort hazard:
   *   VP0_DSP_CTRL bit31=STANDBY gates the VOP2→HDMITX pixel clock path.
   *   While standby is asserted, the HDMITX video-monitor sub-block has no
   *   pixel clock, and any read of registers in the 0x27DA08xx range
   *   (VIDEO_MONITOR_STATUS0 @ 0x27DA0884, VIDEO_INTERFACE_CONFIG0/STATUS,
   *   etc.) triggers a synchronous external abort (ESR=0x96000210).
   *   Rule: between "STANDBY=1 write" and "standby=0 CFG_DONE poll done",
   *   read ONLY VOP2 registers (0x27D0xxxx) and HDMITX control registers
   *   that are not in the 0x27DA08xx range (e.g. LINK_CONFIG0).
   *
   * VP0_DSP_CTRL: regular (non-HIWORD) VOP2 register, bit31 = STANDBY_EN.
   * REG_CFG_DONE (HIWORD): bit16=mask, bit15=GLB_CFG_DONE_EN, bit0=VP0.
   * VOP2 base RK3576 = 0x27D00000.
   */
  {
    CONST UINTN  Vop2Base   = 0x27D00000UL;
    CONST UINTN  RegCfgDone = Vop2Base + 0x000U;
    CONST UINTN  Vp0DspCtrl = Vop2Base + 0xC00U;
    UINT32       LinkRb;
    UINT32       DspCtrlPre;
    UINT32       FrameCntPre;
    UINT32       FrameCntNow;
    UINTN        PollUs;
    UINT32       CfgDoneVal;

    HDMI_TRACE ("Setup: [10d] late-stage HDMITX/VOP2 enable (vendor-matched order)\n");

    /* Pre-state: VOP2 registers only — safe regardless of standby.
     * *** Do NOT read any 0x27DA08xx register before standby is cleared. *** */
    HDMI_DUMP_REG ("  VP0_DSP_CTRL  pre        ", Vp0DspCtrl);
    HDMI_DUMP_REG ("  REG_CFG_DONE  pre        ", RegCfgDone);
    HDMI_DUMP_REG ("  LINK_CONFIG0  pre        ", Hdmi->Base + LINK_CONFIG0);

    DspCtrlPre = MmioRead32 (Vp0DspCtrl);
    HDMI_TRACE ("Setup: [10d] VP0 STANDBY currently %u\n", (DspCtrlPre >> 31) & 1U);

    /* (a) Trigger HDMITX link re-arm: FRL_START is self-clearing.
     *     Vendor writes 0x10 here; readback returns 0 (lnk_rb=0). */
    HDMI_TRACE ("Setup: [10d] (a) LINK_CONFIG0 <- FRL_START (vendor 'lnk=10' trigger)\n");
    DwHdmiQpRegMod (Hdmi, FRL_START, FRL_START, LINK_CONFIG0);
    LinkRb = DwHdmiQpRegRead (Hdmi, LINK_CONFIG0);
    HDMI_TRACE ("Setup: [10d]     LINK_CONFIG0 readback = 0x%08x (vendor sees 0)\n", LinkRb);

    /* (b) Assert VP0 standby; commit; wait 20 ms for HDMITX FIFO to drain.
     *     ─────────────────────────────────────────────────────────────────
     *     NO reads of 0x27DA08xx below until standby is cleared in (c). */
    HDMI_TRACE ("Setup: [10d] (b) VP0 STANDBY=1, commit, wait 20 ms\n");
    MmioWrite32 (Vp0DspCtrl, MmioRead32 (Vp0DspCtrl) | (1U << 31));
    MmioWrite32 (RegCfgDone, 0x00018001U);
    MicroSecondDelay (20000);
    /* Only VOP2 reads here — pixel-clock-independent. */
    HDMI_DUMP_REG ("  VP0_DSP_CTRL  standby    ", Vp0DspCtrl);
    HDMI_DUMP_REG ("  REG_CFG_DONE  standby    ", RegCfgDone);

    /* (c) De-assert standby; re-commit against the now-live PHY pixel clock. */
    HDMI_TRACE ("Setup: [10d] (c) VP0 STANDBY=0, commit (re-arm with live PHY clock)\n");
    MmioWrite32 (Vp0DspCtrl, MmioRead32 (Vp0DspCtrl) & ~(1U << 31));
    MmioWrite32 (RegCfgDone, 0x00018001U);

    /* Poll REG_CFG_DONE bit0 for vsync ack (VOP2 register — always safe).
     * VP0 is active once bit0 clears; pixel clock to HDMITX is restored. */
    for (PollUs = 0; PollUs < 50000; PollUs += 200) {
      CfgDoneVal = MmioRead32 (RegCfgDone);
      if (!(CfgDoneVal & BIT (0))) {
        break;
      }
      MicroSecondDelay (200);
    }
    if (PollUs >= 50000) {
      HDMI_TRACE ("Setup: [10d]     WARN: REG_CFG_DONE bit0 still set after 50 ms\n");
    } else {
      HDMI_TRACE ("Setup: [10d]     REG_CFG_DONE bit0 cleared at ~%lu us (vsync OK)\n", PollUs);
    }

    /* ── Standby fully cleared; pixel clock restored to HDMITX. ──────────
     * 0x27DA08xx reads are now safe again.                                  */

    /* (d) Snapshot VID_MON_ST0 as baseline, then wait up to 500 ms for the
     *     frame counter to advance — proof HDMITX is sampling pixel data.
     *     At 1080p60 each frame is ~16.7 ms; 500 ms ≈ 30 frames. */
    FrameCntPre = MmioRead32 (Hdmi->Base + VIDEO_MONITOR_STATUS0);
    HDMI_TRACE ("Setup: [10d] (d) VID_MON_ST0 baseline = 0x%08x; waiting for advance\n",
                FrameCntPre);
    HDMI_DUMP_REG ("  VID_IF_CFG0   post-arm   ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
    HDMI_DUMP_REG ("  VID_IF_STATUS post-arm   ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
    HDMI_DUMP_REG ("  MAIN_STATUS0  post-arm   ", Hdmi->Base + MAINUNIT_STATUS0);
    HDMI_DUMP_REG ("  CMU_STATUS    post-arm   ", Hdmi->Base + CMU_STATUS);

    for (PollUs = 0; PollUs < 500000; PollUs += 1000) {
      FrameCntNow = MmioRead32 (Hdmi->Base + VIDEO_MONITOR_STATUS0);
      if (FrameCntNow != FrameCntPre) {
        break;
      }
      MicroSecondDelay (1000);
    }
    FrameCntNow = MmioRead32 (Hdmi->Base + VIDEO_MONITOR_STATUS0);
    if (FrameCntNow == FrameCntPre) {
      HDMI_TRACE ("Setup: [10d]     FAIL: VID_MON_ST0 frozen after 500 ms = 0x%08x\n",
                  FrameCntNow);
    } else {
      HDMI_TRACE ("Setup: [10d]     OK: VID_MON_ST0 advanced at ~%lu us (0x%08x -> 0x%08x)\n",
                  PollUs, FrameCntPre, FrameCntNow);
    }

    /* Final post-state dump. */
    HDMI_DUMP_REG ("  VP0_DSP_CTRL  post       ", Vp0DspCtrl);
    HDMI_DUMP_REG ("  REG_CFG_DONE  post       ", RegCfgDone);
    HDMI_DUMP_REG ("  LINK_CONFIG0  post       ", Hdmi->Base + LINK_CONFIG0);
    HDMI_DUMP_REG ("  VID_IF_CFG0   post       ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
    HDMI_DUMP_REG ("  VID_IF_STATUS post       ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
    HDMI_DUMP_REG ("  VID_MON_ST0   post       ", Hdmi->Base + VIDEO_MONITOR_STATUS0);
    HDMI_DUMP_REG ("  MAIN_STATUS0  post       ", Hdmi->Base + MAINUNIT_STATUS0);
    HDMI_DUMP_REG ("  CMU_STATUS    post       ", Hdmi->Base + CMU_STATUS);
  }

  /* ── STEP 11: avmute clear + GCP TX enable ───────────────────────────── */
  if (HdmiMode) {
    HDMI_TRACE ("Setup: [11] Clear avmute, enable GCP_TX\n");
    MicroSecondDelay (50);
    DwHdmiQpRegWrite (Hdmi, 2, PKTSCHED_PKT_CONTROL0);
    DwHdmiQpRegMod (Hdmi, PKTSCHED_GCP_TX_EN, PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);
    HDMI_DUMP_REG ("  PKTSCHED_PKT_EN    ", Hdmi->Base + PKTSCHED_PKT_EN);
  }

  /* [11b] 500ms TMDS wait removed — mainline does not block here. */

  /* ── STEP 12: Final status dump ─────────────────────────────────────── */
  HDMI_TRACE ("Setup: [12] SUCCESS — ConnectorEnable exit Success\n");
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON1    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON1);
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON8    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON8);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  HDMI_DUMP_REG ("HDMITX CMU_STATUS   ", Hdmi->Base + CMU_STATUS);
  HDMI_DUMP_REG ("HDMITX CMU_CONFIG0  ", Hdmi->Base + CMU_CONFIG0);
  HDMI_DUMP_REG ("HDMITX SWDISABLE    ", Hdmi->Base + GLOBAL_SWDISABLE);
  HDMI_DUMP_REG ("HDMITX RST_MGR_CFG0 ", Hdmi->Base + RESET_MANAGER_CONFIG0);
  HDMI_DUMP_REG ("HDMITX RST_MGR_ST0  ", Hdmi->Base + RESET_MANAGER_STATUS0);
  HDMI_DUMP_REG ("HDMITX RST_MGR_ST1  ", Hdmi->Base + RESET_MANAGER_STATUS1);
  HDMI_DUMP_REG ("HDMITX RST_MGR_ST2  ", Hdmi->Base + RESET_MANAGER_STATUS2);
  HDMI_DUMP_REG ("HDMITX MAIN_STATUS0 ", Hdmi->Base + MAINUNIT_STATUS0);
  HDMI_DUMP_REG ("HDMITX VID_IF_CFG0  ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
  /* VID_IF_CFG1/CFG2/CTL0/FC_CONFIG0/SCRAMB_CFG0 (0x804-0x960) trigger synchronous external abort
   * when the video interface sub-block is ungated but VID_IF_CONFIG0=0 — skip these reads. */
  HDMI_DUMP_REG ("HDMITX VID_IF_STAT  ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
  HDMI_DUMP_REG ("HDMITX VID_MON_CFG0 ", Hdmi->Base + VIDEO_MONITOR_CONFIG0);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST0  ", Hdmi->Base + VIDEO_MONITOR_STATUS0);
  /* VIDEO_MONITOR_STATUS1-6: additional timing measurements (H_TOTAL, V_TOTAL, etc.) */
  HDMI_DUMP_REG ("HDMITX VID_MON_ST1  ", Hdmi->Base + VIDEO_MONITOR_STATUS1);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST2  ", Hdmi->Base + VIDEO_MONITOR_STATUS2);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST3  ", Hdmi->Base + VIDEO_MONITOR_STATUS3);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST4  ", Hdmi->Base + VIDEO_MONITOR_STATUS4);
  HDMI_DUMP_REG ("HDMITX LINK_CONFIG0 ", Hdmi->Base + LINK_CONFIG0);
  HDMI_DUMP_REG ("HDMITX SCRAMB_CFG0  ", Hdmi->Base + SCRAMB_CONFIG0);
  HDMI_DUMP_REG ("HDMITX PKTSCHED_EN  ", Hdmi->Base + PKTSCHED_PKT_EN);
  HDMI_DUMP_REG ("HDMITX TMDS_FIFO_C0 ", Hdmi->Base + TMDS_FIFO_CONFIG0);
  /* TMDS_FIFO_CONTROL0 @ 0x974 is a hole — triggers synchronous external abort, skip */
  HDMI_DUMP_REG ("IOC_MISC_CON0       ", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
  HDMI_DUMP_REG ("IOC_MISC_CON1       ", RK3588_SYS_GRF_BASE + 0xA404);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_ST_end ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  /* Raw GPIO4_PC1 pad level: bit17 = 1 means HPD physically HIGH at the pin */
  {
    UINT32  Gpio4ExtFinal = MmioRead32 (0x2AE40070U);
    DEBUG ((DEBUG_INFO,
      "[RK3576-HDMI] Step12: GPIO4_EXT_PORT=0x%08x GPIO4_PC1_HPD_pad=%u\n",
      Gpio4ExtFinal, (Gpio4ExtFinal >> 17) & 1U));
  }
  HDMI_DUMP_REG ("HDPTX_GRF_CON0      ", 0x26032000UL + 0x00);
  HDMI_DUMP_REG ("HDPTX_GRF_STATUS    ", 0x26032000UL + 0x80);
  HDMI_DUMP_REG ("PMU1CRU_GATE_CON0   ", 0x27220000UL + 0x800);
  HDMI_DUMP_REG ("CRU_GATE_CON64      ", 0x27200000UL + 0x800 + 64 * 4);
  HDMI_DUMP_REG ("CRU_SOFTRST_CON28   ", 0x27200000UL + 0xA70);
  return EFI_SUCCESS;
}

EFI_STATUS
DwHdmiQpConnectorEnable (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  struct DwHdmiQpDevice  *Hdmi;
  EFI_STATUS             Status;

  Hdmi = DW_HDMI_QP_FROM_CONNECTOR_PROTOCOL (This);

  HDMI_TRACE (
    "ConnectorEnable: ENTRY id=%u clk=%u kHz %ux%u@%u flags=0x%x "
    "bus_fmt=0x%x(RGB888) out_mode=0x%x(%a)\n",
    Hdmi->Id,
    DisplayState->ConnectorState.DisplayMode.Clock,
    DisplayState->ConnectorState.DisplayMode.HDisplay,
    DisplayState->ConnectorState.DisplayMode.VDisplay,
    DisplayState->ConnectorState.DisplayMode.VRefresh,
    DisplayState->ConnectorState.DisplayMode.Flags,
    DisplayState->ConnectorState.BusFormat,
    DisplayState->ConnectorState.OutputMode,
    DisplayState->ConnectorState.OutputMode == 15 ? "AAAA=OK" : "UNEXPECTED"
    );

  Status = DwHdmiQpSetup (Hdmi, DisplayState);

  HDMI_TRACE ("ConnectorEnable: EXIT Status=%r\n", Status);
  return Status;
}

EFI_STATUS
DwHdmiQpConnectorDisable (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  // Todo
  return EFI_SUCCESS;
}

EFI_STATUS
DwHdmiQpConnectorDetect (
  OUT ROCKCHIP_CONNECTOR_PROTOCOL  *This,
  OUT DISPLAY_STATE                *DisplayState
  )
{
  struct DwHdmiQpDevice  *Hdmi;
  BOOLEAN                Hpd;

  Hdmi = DW_HDMI_QP_FROM_CONNECTOR_PROTOCOL (This);

  Hpd = DwHdmiReadHpd (Hdmi);
  HDMI_TRACE ("ConnectorDetect: id=%u -> %a\n", Hdmi->Id,
              Hpd ? "EFI_SUCCESS (sink present)" : "EFI_NOT_FOUND (no sink)");
  return Hpd ? EFI_SUCCESS : EFI_NOT_FOUND;
}

ROCKCHIP_CONNECTOR_PROTOCOL  mHdmiConnectorOps = {
  NULL,
  DwHdmiQpConnectorPreInit,
  DwHdmiQpConnectorInit,
  NULL,
  DwHdmiQpConnectorDetect,
  NULL,
  DwHdmiQpConnectorGetEdid,
  NULL,
  DwHdmiQpConnectorEnable,
  DwHdmiQpConnectorDisable,
  NULL
};

STATIC struct DwHdmiQpDevice  mRk3588DwHdmiQpDevices[] = {
  {
    .Id              = 0,
    .Base            = 0x27DA0000, /* RK3576 HDMI0 */
    .OutputInterface = VOP_OUTPUT_IF_HDMI0,
  },
  {
    .Id              = 1,
    .Base            = 0x27DA0000, /* Dummy for HDMI1 as RK3576 only has one */
    .OutputInterface = VOP_OUTPUT_IF_HDMI1,
  },
};

EFI_STATUS
EFIAPI
DwHdmiQpInitHdmi (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Index;
  UINT32      Mask;
  EFI_HANDLE  Handle;

  Mask = PcdGet32 (PcdDisplayConnectorsMask);
  HDMI_TRACE ("InitHdmi: ENTRY ConnectorsMask=0x%08x SignalingMode=%u\n",
              Mask, PcdGet8 (PcdHdmiSignalingMode));
  HDMI_TRACE ("InitHdmi: SYS_GRF_BASE=0x%08x VO1_GRF_BASE=0x%08x\n",
              RK3588_SYS_GRF_BASE, RK3588_VO1_GRF_BASE);
  HDMI_TRACE ("InitHdmi: pre-state HPD/IOC dump\n");
  HDMI_DUMP_REG ("IOC_MISC_CON0       ", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
  HDMI_DUMP_REG ("IOC_MISC_CON1       ", RK3588_SYS_GRF_BASE + 0xA404);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  HDMI_DUMP_REG ("GPIO4_EXT_PORT      ", 0x2AE40070);
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON14   ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14);
  // CRU SOFTRST state at boot (before HdmiTxIomux runs).
  // SRST_HDMITX0_REF = 358 -> CON22 bit6 (addr 0x27200A58)
  // SRST_HDMITXHDP   = 453 -> CON28 bit5 (addr 0x27200A70)  <- HPD circuit reset
  // If bit5 of CON28 reads 1 here, HPD was frozen in reset until HdmiTxIomux deasserted it.
  HDMI_DUMP_REG ("CRU_SOFTRST_CON22   ", 0x27200A58);
  HDMI_DUMP_REG ("CRU_SOFTRST_CON28   ", 0x27200A70);

  for (Index = 0; Index < ARRAY_SIZE (mRk3588DwHdmiQpDevices); Index++) {
    struct DwHdmiQpDevice  *Hdmi = &mRk3588DwHdmiQpDevices[Index];

    if (!(Mask & Hdmi->OutputInterface)) {
      HDMI_TRACE ("InitHdmi: skip device[%u] id=%u out_if=0x%x (not in mask)\n",
                  Index, Hdmi->Id, Hdmi->OutputInterface);
      continue;
    }

    HDMI_TRACE (
      "InitHdmi: install device[%u] id=%u base=0x%lx out_if=0x%x\n",
      Index, Hdmi->Id, (UINT64)Hdmi->Base, Hdmi->OutputInterface
      );

    Hdmi->Signature     = DW_HDMI_QP_SIGNATURE;
    Hdmi->HdptxPhy.Id   = Hdmi->Id;
    Hdmi->SignalingMode = PcdGet8 (PcdHdmiSignalingMode);
    CopyMem (&Hdmi->Connector, &mHdmiConnectorOps, sizeof (ROCKCHIP_CONNECTOR_PROTOCOL));

    Handle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gRockchipConnectorProtocolGuid,
                    &Hdmi->Connector,
                    NULL
                    );
    HDMI_TRACE ("InitHdmi: InstallProtocol -> %r handle=%p\n", Status, Handle);
    ASSERT_EFI_ERROR (Status);
  }

  HDMI_TRACE ("InitHdmi: EXIT\n");
  return EFI_SUCCESS;
}
