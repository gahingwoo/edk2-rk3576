/** @file
 *
 *  Motorcomm YT8531 / YT8531S GbE PHY init.
 *
 *  The ArmSoM CM5-IO carrier populates a YT8531C on GMAC0, not the RTL8211F
 *  the RK3576 devicetree names.  Before this file the only PHY driver here was
 *  the Realtek one, so on that board PhyInit() fell through to
 *  "Unknown PHY ID" and the PHY was never configured at all.
 *
 *  ---------------------------------------------------------------------------
 *  NOT VERIFIED ON HARDWARE.  This is a faithful port of the register writes
 *  mainline performs, but nobody has watched a link come up with it.  It also
 *  is very likely NOT SUFFICIENT ON ITS OWN for CM5-IO: the part fitted there
 *  is the crystal-less variant, which needs the SoC to drive 25 MHz into it on
 *  clk_mac_refout.  That is a CRU/GRF change this driver does not make.  Treat
 *  a still-dead link as expected until that piece exists.
 *  ---------------------------------------------------------------------------
 *
 *  Reference: linux/drivers/net/phy/motorcomm.c
 *    yt8531_config_init() -> ytphy_rgmii_clk_delay_config() + yt8531_set_ds()
 *
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>

#include "EthernetPhy.h"

/*
 * Extended ("ext") register access: write the ext register number to the page
 * select register, then read/write the data register.  Both live in the normal
 * MDIO register space.
 */
#define YTPHY_PAGE_SELECT  0x1E
#define YTPHY_PAGE_DATA    0x1F

/* Ext registers */
#define YT8521_CHIP_CONFIG_REG         0xA001
#define   YT8521_CCR_RXC_DLY_EN        BIT8

#define YT8521_RGMII_CONFIG1_REG       0xA003
#define   YT8521_RC1R_RX_DELAY_MASK    (0xFU << 10)   /* bits [13:10] */
#define   YT8521_RC1R_RX_DELAY_SHIFT   10
#define   YT8521_RC1R_GE_TX_DELAY_MASK (0xFU << 0)    /* bits  [3:0]  */
#define   YT8521_RC1R_GE_TX_DELAY_SHIFT 0

#define YTPHY_PAD_DRIVE_STRENGTH_REG   0xA010
#define   YT8531_RGMII_RXC_DS_MASK     (0x7U << 13)   /* bits [15:13] */
#define   YT8531_RGMII_RXC_DS_SHIFT    13
#define   YT8531_RGMII_RXD_DS_HI_MASK  BIT12          /* bit 2 of rxd_ds */
#define   YT8531_RGMII_RXD_DS_LOW_MASK (0x3U << 4)    /* bits 1..0 of rxd_ds */
#define   YT8531_RGMII_RXD_DS_LOW_SHIFT 4

/*
 * Delay code 13 == 1.950 ns, which is mainline's default for both rx and tx
 * when the devicetree carries no {rx,tx}-internal-delay-ps.  Neither the ROCK
 * 4D nor the CM5-IO devicetree specifies one, so the default is what Linux
 * ends up programming on these boards too.
 */
#define YT8521_RC1R_RGMII_1_950_NS     13

/* Mainline YT8531_RGMII_RX_DS_DEFAULT */
#define YT8531_RGMII_RX_DS_DEFAULT     0x3

STATIC
UINT16
YtPhyReadExt (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT16                ExtReg
  )
{
  UINT16  Value;

  PhyWrite (GmacBase, 0, YTPHY_PAGE_SELECT, ExtReg);
  PhyRead (GmacBase, 0, YTPHY_PAGE_DATA, &Value);

  return Value;
}

STATIC
VOID
YtPhyWriteExt (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT16                ExtReg,
  IN UINT16                Value
  )
{
  PhyWrite (GmacBase, 0, YTPHY_PAGE_SELECT, ExtReg);
  PhyWrite (GmacBase, 0, YTPHY_PAGE_DATA, Value);
}

STATIC
VOID
YtPhyModifyExt (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT16                ExtReg,
  IN UINT16                Mask,
  IN UINT16                Value
  )
{
  UINT16  Old;

  Old = YtPhyReadExt (GmacBase, ExtReg);
  YtPhyWriteExt (GmacBase, ExtReg, (UINT16)((Old & ~Mask) | (Value & Mask)));
}

/**
  Program the RGMII clock delays and RX pad drive strength.

  GMAC0 on both supported boards runs RGMII-ID — the delays are generated in
  the PHY, not the MAC — which in mainline terms means RXC delay enable stays
  on and both the rx and ge_tx delay fields get programmed.
**/
STATIC
VOID
Yt8531PhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase
  )
{
  UINT16  Val;
  UINT16  Ds;

  /* RGMII-ID: keep the RXC delay block enabled. */
  YtPhyModifyExt (
    GmacBase,
    YT8521_CHIP_CONFIG_REG,
    YT8521_CCR_RXC_DLY_EN,
    YT8521_CCR_RXC_DLY_EN
    );

  /*
   * rx and ge_tx delay, both at the 1.950 ns default.  FE (10/100) tx delay is
   * deliberately left alone — mainline notes it generally needs no adjustment.
   */
  Val = (UINT16)((YT8521_RC1R_RGMII_1_950_NS << YT8521_RC1R_RX_DELAY_SHIFT) |
                 (YT8521_RC1R_RGMII_1_950_NS << YT8521_RC1R_GE_TX_DELAY_SHIFT));
  YtPhyModifyExt (
    GmacBase,
    YT8521_RGMII_CONFIG1_REG,
    (UINT16)(YT8521_RC1R_RX_DELAY_MASK | YT8521_RC1R_GE_TX_DELAY_MASK),
    Val
    );

  /* RGMII RX clock pad drive strength. */
  Ds = YT8531_RGMII_RX_DS_DEFAULT;
  YtPhyModifyExt (
    GmacBase,
    YTPHY_PAD_DRIVE_STRENGTH_REG,
    YT8531_RGMII_RXC_DS_MASK,
    (UINT16)(Ds << YT8531_RGMII_RXC_DS_SHIFT)
    );

  /*
   * RGMII RX data pad drive strength.  The three-bit field is split: bit 2
   * lives at bit 12, bits 1..0 at bits 5..4.
   */
  Val = (UINT16)((((Ds >> 2) & 0x1U) ? YT8531_RGMII_RXD_DS_HI_MASK : 0) |
                 ((Ds & 0x3U) << YT8531_RGMII_RXD_DS_LOW_SHIFT));
  YtPhyModifyExt (
    GmacBase,
    YTPHY_PAD_DRIVE_STRENGTH_REG,
    (UINT16)(YT8531_RGMII_RXD_DS_HI_MASK | YT8531_RGMII_RXD_DS_LOW_MASK),
    Val
    );

  MicroSecondDelay (10000);
}

EFI_STATUS
EFIAPI
MotorcommPhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT32                PhyId
  )
{
  switch (PhyId) {
    case 0x4F51E91B:
      DEBUG ((DEBUG_INFO, "%a: Found Motorcomm YT8531 GbE PHY\n", __func__));
      Yt8531PhyInit (GmacBase);
      break;
    case 0x4F51E91A:
      DEBUG ((DEBUG_INFO, "%a: Found Motorcomm YT8531S GbE PHY\n", __func__));
      Yt8531PhyInit (GmacBase);
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  DEBUG ((
    DEBUG_WARN,
    "%a: YT8531 init is UNVERIFIED on hardware, and the crystal-less part on "
    "CM5-IO additionally needs 25 MHz on clk_mac_refout, which is not "
    "programmed here — do not read a dead link as a bug in this code alone\n",
    __func__
    ));

  return EFI_SUCCESS;
}
