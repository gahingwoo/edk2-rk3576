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

  // HPD filter / polarity setup (matches upstream's literal 0xffff0102 write
  // to register 0xA404 — IOC_MISC_CON1, bits 1 and 8 enabled).
  HDMI_TRACE ("SetIomux: write IOC_MISC_CON1    <- 0xffff0102 (HPD filter/pol)\n");
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA404, 0xffff0102);

  HDMI_TRACE ("SetIomux: post-state\n");
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON14   ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14);
  HDMI_DUMP_REG ("IOC_MISC_CON0       ", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
  HDMI_DUMP_REG ("IOC_MISC_CON1       ", RK3588_SYS_GRF_BASE + 0xA404);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
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
  // RK3576 reports HPD level via IOC_HDMI_HPD_STATUS bit 3.
  Val = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
  HDMI_TRACE (
    "ReadHpd: IOC_HDMI_HPD_STATUS=0x%08x level=%u (bit3) -> connected=%a\n",
    Val,
    (Val & RK3576_HDMI_LEVEL_INT) ? 1 : 0,
    (Val & RK3576_HDMI_LEVEL_INT) ? "YES" : "NO"
    );
  return (Val & RK3576_HDMI_LEVEL_INT) != 0;
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
  /* Software reset */
  DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);

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
        return Status;
      }

      Status = CheckEdidBlock (Buffer, BlockIndex);
      if (EFI_ERROR (Status)) {
        /* Might be corrupted due to a bus condition, try again. */
        continue;
      }

      break;
    }

    if (Retry == 0) {
      return Status;
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

  return EFI_SUCCESS;
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
  // RK3576 carries colour depth/format in VO0_GRF_SOC_CON8 (different
  // bit layout from RK3588 — RK3576_*_MASK macros embed the shift).
  // Re-build Val for the RK3576 layout instead of using RK3588 bits.
  //
  Val = 0;
  switch (BusFormat) {
    case MEDIA_BUS_FMT_RGB888_1X24:
    case MEDIA_BUS_FMT_RGB101010_1X30:
      Val = HIWORD_UPDATE (RK3576_RGB,    RK3576_COLOR_FORMAT_MASK);
      break;
    case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
    case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
      Val = HIWORD_UPDATE (RK3576_YUV420, RK3576_COLOR_FORMAT_MASK);
      break;
    case MEDIA_BUS_FMT_YUV8_1X24:
    case MEDIA_BUS_FMT_YUV10_1X30:
      Val = HIWORD_UPDATE (RK3576_YUV444, RK3576_COLOR_FORMAT_MASK);
      break;
  }
  if (Depth == 8) {
    Val |= HIWORD_UPDATE (RK3576_8BPC,  RK3576_COLOR_DEPTH_MASK);
  } else {
    Val |= HIWORD_UPDATE (RK3576_10BPC, RK3576_COLOR_DEPTH_MASK);
  }
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
  UINT8                        Val8;
  UINT32                       BitRate;
  BOOLEAN                      HdmiMode;

  Hdptx          = &Hdmi->HdptxPhy;
  ConnectorState = &DisplayState->ConnectorState;
  SinkInfo       = &ConnectorState->SinkInfo;

  BitRate = ConnectorState->DisplayMode.Clock * 10;

  HDMI_TRACE (
    "Setup: id=%u clk_kHz=%u BitRate=%u (10x) sink_isHdmi=%u scdc=%u\n",
    Hdmi->Id,
    ConnectorState->DisplayMode.Clock,
    BitRate,
    SinkInfo->IsHdmi,
    SinkInfo->HdmiInfo.ScdcSupported
    );

  /* Must enable PHY PLL before accessing any HDMI config registers. */
  HDMI_TRACE ("Setup: -> HdptxRopllCmnConfig(BitRate=%u)\n", BitRate);
  Status = HdptxRopllCmnConfig (Hdptx, BitRate);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to enable PHY PLL. Status=%r\n", __func__, Status));
    HDMI_TRACE ("Setup: PHY PLL FAIL Status=%r\n", Status);
    HDMI_DUMP_REG ("GRF_HDPTX_STATUS    ", 0x26032000 /* RK3576 HDPTXPHY GRF base */ + 0x80);
    return Status;
  }
  HDMI_TRACE ("Setup: PHY PLL OK\n");

  Rk3588SetColorFormat (Hdmi, MEDIA_BUS_FMT_RGB888_1X24, 8);

  DwHdmiQpRegMod (Hdmi, HDCP2_BYPASS, HDCP2_BYPASS, HDCP2LOGIC_CONFIG0);

  if (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_AUTO) {
    HdmiMode = SinkInfo->IsHdmi;
  } else {
    HdmiMode = (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_HDMI);
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: %a %a mode\n",
    __func__,
    HdmiMode == SinkInfo->IsHdmi ? "Using" : "Forcing",
    HdmiMode ? "HDMI" : "DVI"
    ));

  DwHdmiQpRegMod (Hdmi, HdmiMode ? 0 : OPMODE_DVI, OPMODE_DVI, LINK_CONFIG0);

  if (ConnectorState->DisplayMode.Clock > 340000) {
    /*
     * Enable high TMDS clock ratio and scrambling for HDMI 2.0 mode.
     * Do not check for SCDC support here, as we might receive a custom
     * mode and should attempt to set it regardless.
     * Under normal circumstances (following EDID) we do not expect to see
     * an unsupported mode.
     */
    DwHdmiScdcRead (Hdmi, SCDC_SINK_VERSION, &Val8);
    DwHdmiScdcWrite (Hdmi, SCDC_SOURCE_VERSION, MIN (Val8, SCDC_MIN_SOURCE_VERSION));

    DwHdmiScdcRead (Hdmi, SCDC_TMDS_CONFIG, &Val8);
    Val8 |= SCDC_TMDS_BIT_CLOCK_RATIO_BY_40 | SCDC_SCRAMBLING_ENABLE;
    DwHdmiScdcWrite (Hdmi, SCDC_TMDS_CONFIG, Val8);

    DwHdmiQpRegWrite (Hdmi, 1, SCRAMB_CONFIG0);
    MicroSecondDelay (100 * 1000);
  } else {
    if (SinkInfo->HdmiInfo.ScdcSupported) {
      /* Disable high TMDS clock ratio and scrambling for HDMI 1.x mode. */
      DwHdmiScdcRead (Hdmi, SCDC_TMDS_CONFIG, &Val8);
      Val8 &= ~(SCDC_TMDS_BIT_CLOCK_RATIO_BY_40 | SCDC_SCRAMBLING_ENABLE);
      DwHdmiScdcWrite (Hdmi, SCDC_TMDS_CONFIG, Val8);

      DwHdmiQpRegWrite (Hdmi, 0, SCRAMB_CONFIG0);
    }
  }

  if (HdmiMode) {
    DwHdmiQpRegMod (Hdmi, KEEPOUT_REKEY_ALWAYS, KEEPOUT_REKEY_CFG, FRAME_COMPOSER_CONFIG9);

    HdmiConfigInfoframes (Hdmi, DisplayState);
  }

  HdptxRopllTmdsModeConfig (Hdptx, BitRate);

  if (HdmiMode) {
    /* clear avmute */
    MicroSecondDelay (50);
    DwHdmiQpRegWrite (Hdmi, 2, PKTSCHED_PKT_CONTROL0);
    DwHdmiQpRegMod (Hdmi, PKTSCHED_GCP_TX_EN, PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);
    HDMI_TRACE ("Setup: cleared avmute, GCP_TX enabled\n");
  }

  HDMI_TRACE ("Setup: SUCCESS — final post-state\n");
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON1    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON1);
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON8    ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON8);
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS ", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
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
    "ConnectorEnable: ENTRY id=%u clk=%u kHz %ux%u@%u flags=0x%x bus_fmt=0x%x out_mode=0x%x\n",
    Hdmi->Id,
    DisplayState->ConnectorState.DisplayMode.Clock,
    DisplayState->ConnectorState.DisplayMode.HDisplay,
    DisplayState->ConnectorState.DisplayMode.VDisplay,
    DisplayState->ConnectorState.DisplayMode.VRefresh,
    DisplayState->ConnectorState.DisplayMode.Flags,
    DisplayState->ConnectorState.BusFormat,
    DisplayState->ConnectorState.OutputMode
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
  HDMI_DUMP_REG ("VO0_GRF_SOC_CON14   ", RK3588_VO1_GRF_BASE + RK3576_VO0_GRF_SOC_CON14);

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
