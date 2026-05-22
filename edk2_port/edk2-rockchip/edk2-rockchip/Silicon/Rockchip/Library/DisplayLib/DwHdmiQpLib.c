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
  //   IOC_MISC_CON0:     HPD_INT_CLR=1, HPD_INT_MSK=0  (one write, CLR stays=1)
  //   IOC_MISC_CON1:     0xffff0102 (SET_DLY_EN=1, LNUM_MS=2ms debounce)
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
  //   PC0 fn9 = hdmi_tx_cec_m0  (CEC — required by mainline hdmi_txm0_pins group)
  //   PC1 fn9 = hdmi_tx_hpdin_m0 (HPD input — required for HPD detect path)
  //   PC2 fn9 = hdmi_tx_scl      (DDC SCL — required for EDID/SCDC)
  //   PC3 fn9 = hdmi_tx_sda      (DDC SDA — required for EDID/SCDC)
  //
  // Mainline Linux (rk3576.dtsi) configures pinctrl-0 = <&hdmi_txm0_pins &hdmi_tx_scl &hdmi_tx_sda>
  // which sets PC0-PC3 ALL to fn9.  In particular, hdmi_txm0_pins covers BOTH PC0 (CEC)
  // and PC1 (HPD) together.  The RK3576 IOC HPD routing appears to require the entire
  // HDMI IO group (PC0-PC3) to be in fn9 for IOC_HDMI_HPD_STATUS bit3 (LEVEL_INT) to
  // reflect the actual pin state.  Leaving PC0 at fn0 (GPIO) keeps LEVEL_INT stuck at 0.
  // Set all four pins to fn9: mask=0xFFFF, value=0x9999.
  HDMI_TRACE ("SetIomux: set GPIO4 PC0-PC3 -> function 9 (CEC/HPD/SCL/SDA, matches mainline hdmi_txm0_pins)\n");
  HDMI_DUMP_REG ("GPIO4_PC_IOMUX_before", RK3588_SYS_GRF_BASE + 0xA390);
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA390, 0xFFFF9999U);
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

  // Configure HPD detection: CLR=1, MSK=0 — matches Linux mainline
  // dw_hdmi_qp_rk3576_setup_hpd() exactly:
  //   HIWORD_UPDATE(CLR, CLR | MSK)  →  mask=0x06, value=0x02  →  0x00060002
  //
  // RK3576 empirical behaviour (confirmed via Linux devmem):
  //   CLR (bit1) is NOT write-1-to-clear (W1C).  Writing 1 latches it HIGH
  //   and it remains HIGH until explicitly cleared.  CLR=1 is required for
  //   the HPD detection circuit to actively sample the pad level and update
  //   LEVEL_INT in IOC_HDMI_HPD_STATUS.  CLR=0 freezes the detector at the
  //   last sampled value (which is 0 at cold-boot time, before the monitor
  //   has had a chance to assert HPD).
  //
  // Linux writes CLR=1 once and leaves it; IOC_MISC_CON0 reads back as 0x03
  // (bit1=CLR=1, bit0=power-on default) and HPD works.  A second write to
  // clear CLR (our former "Step B") leaves IOC_MISC_CON0=0x01, disabling the
  // detector and causing LEVEL_INT to stay 0 permanently.
  Val = HIWORD_UPDATE (RK3576_HDMI_HPD_INT_CLR,
                       RK3576_HDMI_HPD_INT_CLR | RK3576_HDMI_HPD_INT_MSK);
  HDMI_TRACE ("SetIomux: write IOC_MISC_CON0 <- 0x%08x (CLR=1, MSK=0, Linux mainline)\n", Val);
  MmioWrite32 (RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0, Val);
  HDMI_DUMP_REG ("IOC_MISC_CON0 after write", RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);

  // HPD filter: mainline Linux dw_hdmi_qp_rk3576_setup_hpd() writes:
  //   HIWORD_UPDATE(SET_DLY_EN, SET_DLY_EN_MASK) | HIWORD_UPDATE(2, SET_LNUM_MS_MASK)
  //   = 0x01ff0102  (mask=0x01ff, value=0x0102: LNUM_MS=2, SET_DLY_EN=1)
  //
  // IOC_MISC_CON1 layout (RK3576):
  //   bits 7:0  = LNUM_MS (debounce count in ms; default=1, we set=2)
  //   bit8      = SET_DLY_EN (enable debounce filter; 0=raw, 1=filtered)
  //   bit0 in default state = LNUM_MS[0]=1 (LNUM_MS was 1 before our init)
  //
  // We write 0xffff0102 (wider mask = 0xffff, same value = 0x0102).
  // Effect: LNUM_MS=2 (2ms debounce), SET_DLY_EN=1, all other bits=0.
  HDMI_TRACE ("SetIomux: write IOC_MISC_CON1 <- 0xffff0102 (LNUM_MS=2ms, SET_DLY_EN=1)\n");
  MmioWrite32 (RK3588_SYS_GRF_BASE + 0xA404, 0xffff0102U);
  // Brief settle after debounce reconfiguration
  MicroSecondDelay (5 * 1000);  /* 5 ms — allow HPD debounce to arm after CLR=1 write */
  HDMI_DUMP_REG ("IOC_HDMI_HPD_STATUS (5ms post-write)", RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);

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

    /* Throttle log spam: only emit on first call, state changes, or every 64th call */
    {
      STATIC UINT32  sHpdCallCount = 0;
      STATIC BOOLEAN sPrevHwHpd    = 0xFF; /* impossible value → forces first-call log */

      if (sHpdCallCount == 0 || (sHpdCallCount & 63) == 0 || (UINT8)HwHpd != (UINT8)sPrevHwHpd) {
        HDMI_TRACE (
          "ReadHpd: HPD_STATUS=0x%08x bit3=%u bit0=%u GPIO4_PC1=%u MiscCon1=0x%08x hw=%a (call#%u)\n",
          Val,
          (Val >> 3) & 1,
          (Val >> 0) & 1,
          PinLevel,
          MiscCon1,
          HwHpd ? "HIGH" : "LOW",
          sHpdCallCount
          );
      }
      sPrevHwHpd = (UINT8)HwHpd;
      sHpdCallCount++;
    }

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
  // HPD wait: Poll IOC_HDMI_HPD_STATUS (BIT3 = LEVEL_INT) for up to
  // HPD_WAIT_TIMEOUT_MS milliseconds.  UEFI starts much earlier than the
  // Linux HDMI driver: the TV may still be initialising its HDMI port logic
  // and has not yet asserted HPD.  Mainline Linux works because the HDMI
  // driver probes ~30 s after power-on, by which time the TV is ready.
  //
  // Strategy:
  //   • If HPD goes HIGH within the timeout: clear HpdTimeoutFlag and use
  //     the real HPD state for all subsequent ReadHpd() calls.  The EDID
  //     read (in ConnectorGetEdid) will succeed through the normal path.
  //   • If HPD stays LOW (timeout): set HpdTimeoutFlag so ReadHpd() always
  //     returns TRUE.  EDID will be attempted anyway via passive DDC (which
  //     works even when HPD is LOW because many TVs connect DDC to all ports
  //     regardless of which input is selected).  If EDID fails, the driver
  //     falls back to 1080p60.
  {
    CONST UINTN  HPD_WAIT_TIMEOUT_MS = 5000;  /* 5 seconds */
    CONST UINTN  HPD_POLL_MS         = 100;   /* poll every 100 ms */
    UINTN        Elapsed             = 0;
    BOOLEAN      HpdHigh             = FALSE;
    UINT32       HpdStatus;

    DEBUG ((DEBUG_INFO,
      "[RK3576-HDMI] Waiting up to %u s for HPD (TV may need time to initialise) ...\n",
      HPD_WAIT_TIMEOUT_MS / 1000));

    while (Elapsed < HPD_WAIT_TIMEOUT_MS) {
      HpdStatus = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_HDMI_HPD_STATUS);
      if ((HpdStatus & RK3576_HDMI_LEVEL_INT) != 0) {
        HpdHigh = TRUE;
        DEBUG ((DEBUG_INFO,
          "[RK3576-HDMI] HPD HIGH after %u ms (HPD_STATUS=0x%08x)\n",
          Elapsed, HpdStatus));
        break;
      }
      /* Log full diagnostic every 1 second */
      if ((Elapsed % 1000) == 0) {
        UINT32  Gpio4Ext  = MmioRead32 (0x2AE40070U);
        UINT32  Gpio2Ext  = MmioRead32 (0x2AE20070U);
        UINT32  MiscCon0  = MmioRead32 (RK3588_SYS_GRF_BASE + RK3576_IOC_MISC_CON0);
        UINT32  MiscCon1  = MmioRead32 (RK3588_SYS_GRF_BASE + 0xA404U);
        UINT32  Gpio2Iomux = MmioRead32 (0x26044048U);  /* GPIO2_PB0 IOMUX */
        UINT32  Gpio4Iomux = MmioRead32 (RK3588_SYS_GRF_BASE + 0xA390U);  /* GPIO4_PC IOMUX */
        DEBUG ((DEBUG_INFO,
          "[RK3576-HDMI] HPD still LOW at %u ms:\n"
          "  HPD_STATUS=0x%08x  GPIO4_PC1=%u  GPIO2_PB0_pad=%u\n"
          "  IOC_MISC_CON0=0x%08x(CLR=%u,MSK=%u)  IOC_MISC_CON1=0x%08x\n"
          "  GPIO2_PB0_IOMUX=0x%08x[3:0]=0x%x(0=GPIO)  GPIO4_PC_IOMUX=0x%08x(want9999)\n",
          Elapsed,
          HpdStatus, (Gpio4Ext >> 17) & 1U, (Gpio2Ext >> 8) & 1U,
          MiscCon0, (MiscCon0 >> 1) & 1U, (MiscCon0 >> 2) & 1U, MiscCon1,
          Gpio2Iomux, Gpio2Iomux & 0xFU, Gpio4Iomux));
      }
      MicroSecondDelay (HPD_POLL_MS * 1000);
      Elapsed += HPD_POLL_MS;
    }

    if (!HpdHigh) {
      UINT32  Gpio4Ext = MmioRead32 (0x2AE40070U);
      DEBUG ((DEBUG_INFO,
        "[RK3576-HDMI] HPD timeout after %u ms — GPIO4_PC1=%u — enabling bypass (HpdTimeoutFlag=TRUE)\n"
        "[RK3576-HDMI] Hint: ensure the TV is powered on and its HDMI input is selected.\n",
        Elapsed, (Gpio4Ext >> 17) & 1U));
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

  /*
   * Use P888 (24bpp, 8bpc per channel) for standard HDMI output.
   * AAAA (30bpp) causes a VOP2↔DW-HDMI-QP bit-width mismatch: VOP2 outputs
   * 10 bits/channel on the parallel bus but VO0_GRF_SOC_CON8=0x0600 tells the
   * HDMI TX to expect 8 bits/channel (24-bit bus). The resulting misalignment
   * produces horizontal stripe artifacts and a horizontal image shift.
   * P888 matches the VO0_GRF 8bpc setting and is what Linux DRM uses for
   * standard (non-deep-colour) HDMI on RK3576.
   */
  ConnectorState->OutputMode = ROCKCHIP_OUT_MODE_P888;
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
  // RK3576 VO0_GRF_SOC_CON8: Linux ground-truth dump (working 8bpc HDMI) shows
  // 0x00000600 — bits[11:8]=6 (depth), bits[7:4]=0 (RGB format).
  //
  // U-Boot defines RK3576_8BPC=(0x0<<8)=0, but using 0 leaves the register at
  // reset value 0x0000 and the DW HDMI QP VIF block does not activate video
  // (VID_IF_STATUS lower bits stay zero even with correct VOP2 timing visible
  // in VID_MON registers).  The Linux kernel uses bits[11:8]=6 for 8bpc output
  // regardless of U-Boot's constant naming convention.
  //
  // Write mask=0x0FF0 to update both depth[11:8] and format[7:4] fields;
  // value=0x0600 → depth=6, format=0(RGB) → matches Linux ground truth.
  //
  Val = HIWORD_UPDATE (0x0600U, 0x0FF0U);
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

  InfBuf[4] = 0x0;        /* Scan Information = no info (matches vendor) */
  InfBuf[5] = 0x20;       /* Picture Aspect Ratio = 16:9 (matches vendor) */
  InfBuf[7] = Vic & 0xff; /* VIC */

  if (SinkInfo->SelectableRgbRange) {
    InfBuf[6] = 0x2 << 2; /* RGB Quantization Range = Full */
  }

  HdmiInfoframeSetChecksum (InfBuf, sizeof (InfBuf));

  /*
   * DWC HDMI QP packet RAM layout (mainline kernel dw_hdmi_qp_write_pkt):
   *   CONTENTS0[7:0]   = InfBuf[0] (packet type, e.g. 0x82 for AVI)
   *   CONTENTS0[15:8]  = InfBuf[1] (version)
   *   CONTENTS0[23:16] = InfBuf[2] (length)
   *   CONTENTS0[31:24] = 0 (unused)
   *
   *   CONTENTS1..4 pack InfBuf[3..16] in groups of 4, with the checksum
   *   at CONTENTS1[7:0] and VIC (InfBuf[7]) at CONTENTS2[7:0].
   *
   * Note: bits[7:0] of CONTENTS0 are NOT "managed by HW" — the hardware
   * does NOT auto-fill the type byte.  Omitting InfBuf[0] leaves type=0x00
   * in the transmitted InfoFrame, causing strict sinks to reject it.
   * Linux writes buf[0] explicitly; we must do the same.
   *
   * Reference: drivers/gpu/drm/bridge/synopsys/dw-hdmi-qp.c
   *   regmap_write(hdmi->regm, reg, buf[0] | (buf[1]<<8) | (buf[2]<<16));
   */
  val = ((UINT32)InfBuf[0]) | ((UINT32)InfBuf[1] << 8) | ((UINT32)InfBuf[2] << 16);
  DwHdmiQpRegWrite (Hdmi, val, PKT_AVI_CONTENTS0);

  for (i = 0; i < 4; i++) {
    val = 0;
    for (j = 0; j < 4; j++) {
      /* Source range is InfBuf[3..16] = checksum + 13 payload bytes. */
      if (i * 4 + j >= 14) {
        break;
      }

      val |= (UINT32)InfBuf[i * 4 + j + 3] << (8 * j);
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
  DEBUG ((DEBUG_INFO,
    "[RK3576-HDMI] Setup ENTRY: Clock=%u kHz BitRate=%u kbps %ux%u "
    "(1080p60=148500/1485000, QHD=241700/2417000)\n",
    ConnectorState->DisplayMode.Clock, BitRate,
    ConnectorState->DisplayMode.HDisplay,
    ConnectorState->DisplayMode.VDisplay));

  /* ── STEP 0: Vendor-match: INT_MASK disable + TIMER_BASE_CONFIG0 ─────── */
  /*
   * Vendor EDK2 binary (VA=0x4234..0x4258) writes these three registers at
   * the very start of HDMI TX initialisation — before any PHY or clock work.
   *
   *   MAINUNIT_0_INT_MASK_N = 0  (0x27DA3014) — mask all mainunit interrupts
   *   MAINUNIT_1_INT_MASK_N = 0  (0x27DA3024) — mask all mainunit interrupts
   *   TIMER_BASE_CONFIG0    (0x27DA0080) — HDMITX reference clock rate in Hz
   *
   * The register holds the HDMITX reference clock frequency in Hz.  This
   * value is used for all internal DWC HDMI QP timing, including CEC.
   * Using the wrong value breaks CEC and may affect other timing logic.
   *
   *   RK3588 ref clock = 428,571,429 Hz → 0x1982C300 (vendor default)
   *   RK3576 ref clock = 396,000,000 Hz → 0x179A7B00
   *     (kernel 6.19 commit f7a1de0d8622 "drm/bridge: dw-hdmi-qp: Fixup
   *      timer base setup" fixes this for RK3576)
   *
   * Without TIMER_BASE_CONFIG0 the DWC HDMI QP internal timing logic may
   * not be calibrated correctly, which can prevent the controller from
   * outputting a valid TMDS link even when VOP2 and the PHY are ready.
   */
  DwHdmiQpRegWrite (Hdmi, 0, MAINUNIT_0_INT_MASK_N);
  DwHdmiQpRegWrite (Hdmi, 0, MAINUNIT_1_INT_MASK_N);
  MmioWrite32 (Hdmi->Base + TIMER_BASE_CONFIG0, 0x179A7B00U);  /* 396 MHz, RK3576 */
  HDMI_TRACE ("Setup: [0] MAINUNIT_0/1_INT_MASK_N=0, TIMER_BASE_CONFIG0=0x179A7B00 (396MHz RK3576)\n");
  HDMI_DUMP_REG ("  TIMER_BASE_CONFIG0", Hdmi->Base + TIMER_BASE_CONFIG0);

  /*
   * Re-initialize I2C master timing to match the 396 MHz TIMER_BASE_CONFIG0.
   *
   * DwHdmiQpInitHw() set TIMER_BASE = 24 MHz with I2CM_SM/FM_SCL_CONFIG0 =
   * 0x00600071 (96/113 cycles → ~115 kHz at 24 MHz).  After we change
   * TIMER_BASE to 396 MHz those same cycle counts give:
   *   96 cycles @ 396 MHz = 242 ns  (DDC minimum SCL-high = 4 µs !!!)
   *   → I2C runs at ~1.9 MHz → DDC slave never responds → SCDC timeout.
   *
   * Fix: reset I2CM and reprogram SCL timing for 396 MHz.
   * Linux uses 0x085c085c at ~428 MHz ref → 5.0 µs/half-period → 100 kHz.
   * At 396 MHz: 0x085c = 2140 cycles = 5.4 µs → 92 kHz DDC — valid.
   *
   * Matches Linux kernel dw_hdmi_qp_init_hw() sequence.
   */
  DwHdmiQpRegWrite (Hdmi, 0x01, I2CM_CONTROL0);        /* SW reset I2CM  */
  DwHdmiQpRegWrite (Hdmi, 0x085c085c, I2CM_SM_SCL_CONFIG0);  /* 396 MHz → 92 kHz DDC */
  DwHdmiQpRegWrite (Hdmi, 0x085c085c, I2CM_FM_SCL_CONFIG0);  /* same for FM  */
  DwHdmiQpRegMod   (Hdmi, 0, I2CM_FM_EN, I2CM_INTERFACE_CONTROL0);
  DwHdmiQpRegWrite (
    Hdmi,
    I2CM_OP_DONE_CLEAR | I2CM_NACK_RCVD_CLEAR,
    MAINUNIT_1_INT_CLEAR
    );
  HDMI_TRACE ("Setup: [0b] I2C re-init for 396MHz: SM/FM_SCL_CONFIG0=0x085c085c (92kHz DDC)\n");

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

  /* ── STEP 1b: clear stale controller state from previous boot ──────────
   *
   * If Linux (or a previous EDK2 boot) was running before us, the HDMI TX
   * packet scheduler may still have AVI/GCP/Vendor InfoFrames queued in its
   * packet RAM and PKT_EN bits set.  When we re-enable the video path at
   * step [10e] before our own packets are programmed, the controller
   * momentarily transmits whatever stale packets are sitting in its RAM,
   * confusing the sink (wrong VIC, wrong color-depth GCP, etc.) and producing
   * the "sometimes no signal / sometimes moiré" symptom on the very first
   * boot after a Linux session.
   *
   * Reset to a known-empty state: disable all scheduled packets and clear
   * PKT_CONTROL0 (GCP body) before we run any of the configuration steps.
   * Step [9] (AVI) and step [11] (AVMUTE/GCP_TX) will re-enable only the
   * packets they explicitly populate.
   */
  HDMI_TRACE ("Setup: [1b] Clear stale packet scheduler state\n");
  DwHdmiQpRegWrite (Hdmi, 0, PKTSCHED_PKT_EN);
  DwHdmiQpRegWrite (Hdmi, 0, PKTSCHED_PKT_CONTROL0);
  HDMI_DUMP_REG ("  PKTSCHED_PKT_EN cleared", Hdmi->Base + PKTSCHED_PKT_EN);

  /* ── STEP 2: Enable HDMI TX QP clocks ───────────────────────────────── */
  HDMI_TRACE ("Setup: [2] Enable QP clocks (CMU_CONFIG0) — keep SWDISABLE set until after PHY\n");
  /*
   * Clear VIDQPCLK_OFF (bit3) and LINKQPCLK_OFF (bit5) in CMU_CONFIG0 to
   * un-gate the video/link QP clocks inside the HDMI TX controller.
   * Without this the serializer has no clock even if the PHY PLL is running.
   *
   * AVP_DATAPATH_VIDEO_SWDISABLE is intentionally left SET here and cleared
   * only after the PHY lanes are ready (step [10]).  This matches Linux
   * behaviour where VOP2 is in standby (no video) during HDMI TX init;
   * enabling the video path before the PHY is ready can latch garbage video
   * data from the wrong clock domain and leave the HDMI TX in a bad state.
   */
  DwHdmiQpRegMod (Hdmi, 0, VIDQPCLK_OFF | LINKQPCLK_OFF, CMU_CONFIG0);
  HDMI_DUMP_REG ("  CMU_CONFIG0 post  ", Hdmi->Base + CMU_CONFIG0);
  HDMI_DUMP_REG ("  CMU_STATUS  post  ", Hdmi->Base + CMU_STATUS);
  HDMI_DUMP_REG ("  SWDISABLE   post  ", Hdmi->Base + GLOBAL_SWDISABLE);

  /* ── STEP 3: PHY PLL configure for TMDS ─────────────────────────────── */
#ifdef SOC_RK3576
  /*
   * NOTE: The SRST_LINKSYM_HDMITXPHY0 pulse was removed.
   * Mainline Linux dw_hdmi_qp_rk3576_phy_init() (via phy_power_on) does NOT
   * perform this reset pulse.  The log confirmed CRU_SOFTRST_CON25 was already
   * 0x00000000 (deasserted) before our pulse, so the assert+deassert had no
   * positive effect and may have caused a transient glitch.  Removed.
   */
#endif /* SOC_RK3576 */

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
  /*
   * VP0 must be in STANDBY when switching CLKSEL_CON147.
   *
   * In Linux, clk_set_parent(DCLK_VP0, hdptxphy_pll) is called from
   * vop2_crtc_atomic_enable() while VP0 is still in standby — before the
   * STANDBY bit is cleared.  Switching a live DCLK mux corrupts VOP2 output
   * and the HDMITX never sees a valid video stream.
   *
   * Our framework calls Vop2Enable() (which clears STANDBY) before calling
   * ConnectorEnable() (which runs this step), so VP0 is already active here.
   * Fix: briefly put VP0 back into standby, switch the mux, then re-enable.
   *
   * VP0_DSP_CTRL @ 0x27D00C00 (VOP2_BASE + 0xC00):  bit31 = STANDBY
   * REG_CFG_DONE  @ 0x27D00000 (VOP2_BASE + 0x000):  commit VP0 shadows with
   *   CFG_DONE_EN(bit15) | VP0_CFG_DONE(bit0) | VP0_CFG_DONE_MASK(bit16)
   *   = 0x00018001
   */
  {
    UINT32  DspCtrl = MmioRead32 (0x27D00000UL + 0xC00U);
    HDMI_TRACE ("Setup: [4]  VP0_DSP_CTRL pre-mux  = 0x%08x  STANDBY=%u\n",
                DspCtrl, (DspCtrl >> 31) & 1U);

    /* [4a] Assert VP0 STANDBY */
    HDMI_TRACE ("Setup: [4a] VP0 -> STANDBY (before DCLK mux switch)\n");
    MmioWrite32 (0x27D00000UL + 0xC00U, DspCtrl | BIT (31));
    MmioWrite32 (0x27D00000UL + 0x000U, 0x00018001U);   /* REG_CFG_DONE: commit VP0 */
    MicroSecondDelay (20 * 1000);   /* 20 ms — one full frame at 60 Hz */
    HDMI_TRACE ("Setup: [4a] VP0_DSP_CTRL after STANDBY commit = 0x%08x  STANDBY=%u\n",
                MmioRead32 (0x27D00000UL + 0xC00U),
                (MmioRead32 (0x27D00000UL + 0xC00U) >> 31) & 1U);

    /* [4b] Switch DCLK_VP0 mux to clk_hdmiphy_pixel0
     * DCLK_VP0 gate: CLKGATE_CON(61) bit13 @ CRU+0x800+61*4 = CRU+0x8F4
     * HIWORD: mask bit13 => 0x20002000 = gate; 0x20000000 = ungate
     */
    HDMI_DUMP_REG ("  CLKGATE_CON61 pre ", 0x27200000UL + 0x8F4U);
    MmioWrite32 (0x27200000UL + 0x8F4U, 0x20002000U);  /* gate DCLK_VP0 */
    MmioWrite32 (0x27200000UL + 0x054C, 0x08000800U);
    HDMI_DUMP_REG ("  CLKSEL_CON147 post", 0x27200000UL + 0x054C);
    MmioWrite32 (0x27200000UL + 0x8F4U, 0x20000000U);  /* ungate DCLK_VP0 */
    HDMI_DUMP_REG ("  CLKGATE_CON61 post", 0x27200000UL + 0x8F4U);
    HDMI_DUMP_REG ("  CLKSEL_CON145 src ", 0x27200000UL + 0x0544);  /* dclk_vp0_src mux/div */
    HDMI_DUMP_REG ("  CMU_STATUS postMux", Hdmi->Base + CMU_STATUS);
    MicroSecondDelay (2 * 1000);    /* 2 ms — let new clock stabilise */

    /* [4c] Deassert VP0 STANDBY */
    HDMI_TRACE ("Setup: [4c] VP0 -> active (after DCLK mux switch)\n");
    MmioWrite32 (0x27D00000UL + 0xC00U, DspCtrl & ~(UINT32)BIT (31));
    MmioWrite32 (0x27D00000UL + 0x000U, 0x00018001U);   /* REG_CFG_DONE: commit VP0 */
    MicroSecondDelay (20 * 1000);   /* 20 ms — allow VP0 to re-sync output */
    HDMI_TRACE ("Setup: [4]  VP0_DSP_CTRL post-mux = 0x%08x  STANDBY=%u\n",
                MmioRead32 (0x27D00000UL + 0xC00U),
                (MmioRead32 (0x27D00000UL + 0xC00U) >> 31) & 1U);
  }

  /* ── STEP 4c: VO0 GRF CON1 = TMDS link mode ─────────────────────────── */
  /*
   * BSP rk3576_set_link_mode() explicitly writes bit0=0 to VO0_GRF_SOC_CON1
   * to ensure TMDS link mode (not FRL).  Bit0=1 = FRL mode, which requires
   * a completely different link setup and causes garbage on the TMDS output.
   * GRF registers survive soft reset, so a previous boot or ATF code could
   * have left this set.  Write it explicitly to guarantee TMDS mode.
   */
  HDMI_TRACE ("Setup: [4d] VO0_GRF_SOC_CON1 <- TMDS link mode (bit0=0)\n");
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

  /* ── STEP 7: HDMI / DVI mode ──────────────────────────────────────────
   * Force HDMI mode regardless of sink EDID parsing — user demand: always
   * drive the link in HDMI TMDS mode (not DVI). This guarantees AVI
   * infoframes, SCDC, GCP, and HDMI-specific signalling are emitted.
   * Only honour an explicit DVI selection from the user via SignalingMode. */
  if (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_DVI) {
    HdmiMode = FALSE;
  } else {
    HdmiMode = TRUE;
  }
  HDMI_TRACE (
    "Setup: [7] mode=%a (SignalingMode=%u IsHdmi=%u, FORCED=%a) -> LINK_CONFIG0\n",
    HdmiMode ? "HDMI" : "DVI",
    Hdmi->SignalingMode,
    SinkInfo->IsHdmi,
    (Hdmi->SignalingMode == HDMI_SIGNALING_MODE_DVI) ? "DVI(user)" : "HDMI"
    );
  /* Match Linux mainline dw_hdmi_qp_set_op_mode():
   *   for HDMI TMDS:  OPMODE_FRL=0, OPMODE_FRL_4LANES=0, OPMODE_DVI=0
   *   for DVI:        OPMODE_DVI=1 (others 0)
   * The PHY-PLL configure step leaves LINK_CONFIG0=0x10 (bit4) which puts
   * the link state machine in DVI mode by default. We must clear it for
   * HDMI mode here, BEFORE the lane bring-up, so that lanes are configured
   * for HDMI signalling (TMDS clock + control periods + data islands)
   * rather than DVI (TMDS clock + sync + pixel data only).
   */
  {
    UINT32  OpmodeMask = OPMODE_FRL | OPMODE_FRL_4LANES | OPMODE_DVI;
    UINT32  OpmodeVal  = HdmiMode ? 0 : OPMODE_DVI;
    DwHdmiQpRegMod (Hdmi, OpmodeVal, OpmodeMask, LINK_CONFIG0);
  }
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

  /* ── STEP 7c: Clear SCDC TMDS_CONFIG at the sink ──────────────────────
   * Mirrors mainline dw_hdmi_qp_bridge_enable():
   *   dw_hdmi_scdc_write(hdmi, SCDC_TMDS_CONFIG, 0)
   *
   * For HDMI 2.0 sinks (scdc=1), write SCDC register 0x20 = 0x00 to:
   *   bit0 (TMDS_BIT_CLOCK_RATIO) = 0  → 1:10 ratio (for ≤ 340 Mbps)
   *   bit1 (SCRAMBLING_ENABLE)    = 0  → no scrambling at sink
   *
   * If a previous Linux session left the sink in a scrambled state
   * (e.g. from 4K mode or HDMI 2.0 testing), our unscrambled 1080p60
   * TMDS would look like noise to the TV.  Explicitly clear the SCDC
   * so the TV decodes our unscrambled TMDS correctly.
   *
   * SCDC_TMDS_CONFIG = 0x20 in the HDMI SCDC address space.
   * This is an I2C write to slave 0x54 — same path as EDID DDC.
   * Non-fatal: if the write fails, log and continue (EDID read already
   * proved DDC is working, so this is extremely unlikely to fail).
   */
  if (SinkInfo->HdmiInfo.ScdcSupported) {
    EFI_STATUS  ScdcSt = EFI_DEVICE_ERROR;
    UINT8       ScdcReadback = 0xFF;
    UINT32      ScdcRetry;
    BOOLEAN     ScdcWriteOk = FALSE;

    HDMI_TRACE ("Setup: [7c] SCDC TMDS_CONFIG=0 (disable scrambling at sink)\n");

    /*
     * Retry the SCDC write up to 3 times.  If a previous Linux session left
     * the sink in HDMI-2.0 scrambling mode (e.g. after running at 4K), a single
     * failed I2C transaction here leaves the monitor decoding our unscrambled
     * TMDS as noise → "sometimes no signal".  Retries give the I2C bus a
     * chance to recover from glitches caused by HPD bouncing or DDC bus
     * contention with the EDID read that just completed.
     */
    for (ScdcRetry = 0; ScdcRetry < 3; ScdcRetry++) {
      ScdcSt = DwHdmiScdcWrite (Hdmi, 0x20 /* SCDC_TMDS_CONFIG */, 0x00);
      if (!EFI_ERROR (ScdcSt)) {
        ScdcWriteOk = TRUE;
        HDMI_TRACE ("Setup: [7c] SCDC TMDS_CONFIG=0 OK (attempt %u)\n", ScdcRetry + 1);
        break;
      }
      HDMI_TRACE ("Setup: [7c] SCDC write attempt %u FAILED Status=%r — retrying\n",
                  ScdcRetry + 1, ScdcSt);
      MicroSecondDelay (2000);   /* 2 ms — let I2C bus settle between attempts */
    }

    if (!ScdcWriteOk) {
      HDMI_TRACE ("Setup: [7c] SCDC write all retries FAILED — sink may stay scrambled\n");
    } else {
      /* Readback for diagnostic.  If the read times out the write still
       * succeeded (ACK confirmed by DwHdmiScdcWrite); the sink has applied it. */
      ScdcSt = DwHdmiScdcRead (Hdmi, 0x20, &ScdcReadback);
      HDMI_TRACE ("Setup: [7c] SCDC TMDS_CONFIG readback=0x%02x (%a)\n",
                  ScdcReadback,
                  EFI_ERROR (ScdcSt) ? "read-fail (write ACKed)" :
                  (ScdcReadback == 0 ? "OK=0" : "MISMATCH"));
    }
  }

  /* ── STEP 9: AVI infoframe ───────────────────────────────────────────── */
  if (HdmiMode) {
    HDMI_TRACE ("Setup: [9] HdmiConfigInfoframes (AVI)\n");
    DwHdmiQpRegMod (Hdmi, KEEPOUT_REKEY_ALWAYS, KEEPOUT_REKEY_CFG, FRAME_COMPOSER_CONFIG9);
    /* Vendor U-Boot dw_hdmi_setup() writes FLT_CONFIG0=0 in the HDMI-mode
     * branch right before the infoframes, regardless of FRL state.  Make sure
     * we don't inherit any stale FRL link-training config from a previous
     * firmware that left FLT_CONFIG0 non-zero. */
    DwHdmiQpRegWrite (Hdmi, 0, FLT_CONFIG0);
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

  /* ── STEP 10e: Enable HDMI TX video path now that PHY is stable ──────── */
  /*
   * Now that the PHY PLL is locked and data lanes are ready, clear
   * AVP_DATAPATH_VIDEO_SWDISABLE to connect VOP2 pixel output to the HDMI TX.
   * Delaying this until after PHY init mirrors Linux (where VOP2 is in
   * standby during HDMI TX setup): the HDMI TX sees a clean, stable pixel
   * stream for the first time rather than potentially corrupted data from the
   * clock-mux transition at step [4].
   */
  HDMI_TRACE ("Setup: [10e] Enable AVP video path (clear SWDISABLE) — PHY stable\n");
  DwHdmiQpRegMod (Hdmi, 0, AVP_DATAPATH_VIDEO_SWDISABLE, GLOBAL_SWDISABLE);
  MicroSecondDelay (5000);  /* 5 ms: allow HDMI TX to sync to incoming video */
  HDMI_DUMP_REG ("  SWDISABLE   10e   ", Hdmi->Base + GLOBAL_SWDISABLE);
  HDMI_DUMP_REG ("  VID_MON_ST0 10e   ", Hdmi->Base + VIDEO_MONITOR_STATUS0);

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
  /* EI (Electrical Idle) state — LANEx_REG01 bit7=OVRD bit6=EI_EN; want 0x80 (OVRD=1 EI_EN=0) */
  HDMI_DUMP_REG ("  LN0_R0301(EI)      ", HDMI0TX_PHY_BASE + 0x0C04); /* data lane 0; want 0x80   */
  HDMI_DUMP_REG ("  LN1_R0401(EI)      ", HDMI0TX_PHY_BASE + 0x1004); /* data lane 1; want 0x80   */
  HDMI_DUMP_REG ("  LN2_R0501(EI)      ", HDMI0TX_PHY_BASE + 0x1404); /* data lane 2; want 0x80   */
  HDMI_DUMP_REG ("  LN3_R0601(EI)clk   ", HDMI0TX_PHY_BASE + 0x1804); /* clock lane;  want 0x80   */
  HDMI_DUMP_REG ("  GRF_CON0(bias/bgr) ", 0x26032000UL + 0x00);       /* BIAS_EN|BGR_EN|PLL_EN    */

  /* ── STEP 10d: post-PHY status read-only dump ───────────────────────────
   *
   * REMOVED: The VOP2 VP0 standby cycle that previously lived here.  Vendor
   * U-Boot dw_hdmi_setup() and the BSP kernel dw_hdmi_qp_setup() / encoder
   * enable paths NEVER touch VP0_DSP_CTRL / OVL_CTRL / IF_CTRL after the
   * VOP2 has been brought up.  Toggling VP0 standby mid-flow created a
   * disturbance in the pixel-clock domain that the sink interpreted as link
   * loss (HPD dropped from HIGH to LOW after PreInit).  Trust Vop2Dxe's
   * configuration and proceed straight to the avmute clear. */
  HDMI_TRACE ("Setup: [10d] post-PHY status (read-only, no VOP touches)\n");
  HDMI_DUMP_REG ("  LINK_CONFIG0  post-PHY   ", Hdmi->Base + LINK_CONFIG0);
  HDMI_DUMP_REG ("  CMU_STATUS    post-PHY   ", Hdmi->Base + CMU_STATUS);
  HDMI_DUMP_REG ("  MAIN_STATUS0  post-PHY   ", Hdmi->Base + MAINUNIT_STATUS0);
  HDMI_DUMP_REG ("  VP0_DSP_CTRL  post-PHY   ", 0x27D00C00UL);
  HDMI_DUMP_REG ("  OVL_CTRL      post-PHY   ", 0x27D00600UL);
  HDMI_DUMP_REG ("  IF_CTRL[HDMI0] post-PHY  ", 0x27D00184UL);
  HDMI_DUMP_REG ("  VID_IF_CFG0   post-PHY   ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
  HDMI_DUMP_REG ("  VID_IF_STATUS post-PHY   ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
  HDMI_DUMP_REG ("  VID_MON_ST0   post-PHY   ", Hdmi->Base + VIDEO_MONITOR_STATUS0);

  /* ── STEP 11: avmute clear + GCP TX enable ─────────────────────────────
   * Vendor U-Boot dw_hdmi_setup() (rockchip-linux/u-boot next-dev,
   * drivers/video/drm/dw_hdmi_qp.c) executes after PHY init:
   *     mdelay(50);
   *     hdmi_writel(hdmi, 2, PKTSCHED_PKT_CONTROL0);
   *     hdmi_modb(hdmi, PKTSCHED_GCP_TX_EN, PKTSCHED_GCP_TX_EN,
   *               PKTSCHED_PKT_EN);
   *
   * The literal 2 in PKTSCHED_PKT_CONTROL0 is the General Control Packet
   * body byte 0 with bit1 (CLEAR_AVMUTE) set.  When GCP_TX_EN is then
   * enabled, the controller transmits a GCP that explicitly tells the sink
   * to release AVMUTE.  Many sinks default to AVMUTE asserted after PHY
   * (re)initialisation; if we send GCP_TX_EN with PKT_CONTROL0=0 the GCP
   * carries an empty payload and the sink stays muted, presenting as
   * "no signal".
   *
   * The previous code wrote 0 here and used MicroSecondDelay(50) (50 µs,
   * not 50 ms).  Both are bugs aligned with the observed "no signal"
   * symptom — corrected against vendor U-Boot. */
  if (HdmiMode) {
    HDMI_TRACE ("Setup: [11] Clear avmute (PKT_CTL0=2 vendor), enable GCP_TX\n");
    MicroSecondDelay (50000);  /* 50 ms — match vendor mdelay(50) */
    DwHdmiQpRegWrite (Hdmi, 2, PKTSCHED_PKT_CONTROL0);
    DwHdmiQpRegMod (Hdmi, PKTSCHED_GCP_TX_EN, PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);
    HDMI_DUMP_REG ("  PKTSCHED_PKT_CTL0  ", Hdmi->Base + PKTSCHED_PKT_CONTROL0);
    HDMI_DUMP_REG ("  PKTSCHED_PKT_EN    ", Hdmi->Base + PKTSCHED_PKT_EN);

    /*
     * Re-trigger the AVMUTE CLEAR GCP once more after a 50 ms delay.
     *
     * A single GCP transmission can be lost on the HDMI link if it happens
     * to coincide with HPD bouncing, the sink's PLL re-locking, or its CEC
     * controller wake-up.  When that single GCP-clear is missed the sink
     * stays in AVMUTE (default after a sink-side reset) and presents as
     * "no signal" even though our PHY/VOP2 are healthy.
     *
     * Toggling PKT_EN GCP_TX off→on forces the scheduler to re-transmit the
     * packet now sitting in CONTROL0 (still = 2 = CLEAR_AVMUTE).
     */
    MicroSecondDelay (50000);
    DwHdmiQpRegMod (Hdmi, 0, PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);
    DwHdmiQpRegMod (Hdmi, PKTSCHED_GCP_TX_EN, PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);
    HDMI_TRACE ("Setup: [11b] Re-armed GCP_TX (second AVMUTE CLEAR transmission)\n");

    /* VIDEO_INTERFACE_CONFIG0: do NOT write BIT(21).
     *
     * Linux ground-truth dump (working 8bpc HDMI): VID_IF_CONFIG0=0x00000000.
     * Linux VID_IF_STATUS=0x00060560 (lower bits set = video active).
     *
     * When UEFI previously wrote BIT(21)=0x00200000, VID_MON registers
     * confirmed correct 1080p60 timing arriving from VOP2, but
     * VID_IF_STATUS stayed 0x00060000 (lower bits zero = no active video).
     * Removing BIT(21) is required to allow the VIF block to arm and pass
     * pixel data to the TMDS serialiser.
     *
     * Vendor U-Boot writes BIT(21) last with the comment "Mark uboot hdmi
     * is enabled" — this is a U-Boot-specific bookkeeping flag that has no
     * bearing on hardware functionality and actively breaks the VIF here. */
    HDMI_TRACE ("Setup: [11b] VIDEO_INTERFACE_CONFIG0 = 0 (not written; Linux-confirmed correct)\n");
    HDMI_DUMP_REG ("  VID_IF_CFG0 pre-final   ", Hdmi->Base + VIDEO_INTERFACE_CONFIG0);
    HDMI_DUMP_REG ("  VID_IF_STATUS pre-final ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
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
   * when the video interface sub-block is ungated but VID_IF_CONFIG0=0 — skip these reads.
   * SCRAMB_CONFIG0 (0x960) specifically causes the abort; removed from this dump. */
  HDMI_DUMP_REG ("HDMITX VID_IF_STAT  ", Hdmi->Base + VIDEO_INTERFACE_STATUS0);
  HDMI_DUMP_REG ("HDMITX VID_MON_CFG0 ", Hdmi->Base + VIDEO_MONITOR_CONFIG0);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST0  ", Hdmi->Base + VIDEO_MONITOR_STATUS0);
  /* VIDEO_MONITOR_STATUS1-6: additional timing measurements (H_TOTAL, V_TOTAL, etc.) */
  HDMI_DUMP_REG ("HDMITX VID_MON_ST1  ", Hdmi->Base + VIDEO_MONITOR_STATUS1);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST2  ", Hdmi->Base + VIDEO_MONITOR_STATUS2);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST3  ", Hdmi->Base + VIDEO_MONITOR_STATUS3);
  HDMI_DUMP_REG ("HDMITX VID_MON_ST4  ", Hdmi->Base + VIDEO_MONITOR_STATUS4);
  HDMI_DUMP_REG ("HDMITX LINK_CONFIG0 ", Hdmi->Base + LINK_CONFIG0);
  /* SCRAMB_CONFIG0 (0x960) skipped: causes synchronous external abort when VID_IF_CONFIG0=0 */
  HDMI_DUMP_REG ("HDMITX PKTSCHED_EN  ", Hdmi->Base + PKTSCHED_PKT_EN);
  HDMI_DUMP_REG ("HDMITX PKT_AVI_CON0 ", Hdmi->Base + PKT_AVI_CONTENTS0);
  HDMI_DUMP_REG ("HDMITX PKT_AVI_CON1 ", Hdmi->Base + PKT_AVI_CONTENTS1);
  HDMI_DUMP_REG ("HDMITX PKTSCHED_ST0 ", Hdmi->Base + PKTSCHED_PKT_STATUS0);
  HDMI_DUMP_REG ("HDMITX PKTSCHED_ST1 ", Hdmi->Base + PKTSCHED_PKT_STATUS1);
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
    DisplayState->ConnectorState.OutputMode == 0 ? "P888=OK" :
    DisplayState->ConnectorState.OutputMode == 15 ? "AAAA(deep-color)" : "UNEXPECTED"
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
