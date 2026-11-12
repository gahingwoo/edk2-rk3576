/** @file
 *
 *  Maxio MAE0621A-Q3C GbE PHY: identify it and report its state. No writes
 *  beyond the page select register.
 *
 *  The LCKFB TaishanPi 3M fits this PHY (PCB BOM: MAE0621A-Q3C, LCSC
 *  C49308068) on GMAC0. It reports ID 0x7b744412. Before this file
 *  PhyInit() logged "Unknown PHY ID" for it at DEBUG_ERROR.
 *
 *  Why nothing is programmed:
 *
 *  - RGMII delays. The datasheet documents no register for them; they are
 *    set by straps only (Table 7-3, "CONFIG Pins", page 16: RXD1 = TXDLY,
 *    RXD0 = RXDLY). On the TaishanPi 3M v1.4 PCB, RXD0 has a 4.7k pull-up
 *    and RXD1 a 4.7k pull-down, so the PHY adds 2 ns on RXC and nothing on
 *    TXC. That is phy-mode = "rgmii-rxid" with tx_delay = <0x24> in the MAC,
 *    which is what PcdGmac0TxDelay / PcdGmac0RxDelay already program.
 *
 *  - CLKOUT. PHYCR2 (page 0xA43, reg 0x19) bits 0 and 11 select a 125 MHz
 *    CLKOUT (datasheet Table 8-18, page 32). The proposed mainline driver
 *    sets them only when the device tree asks for it ([PATCH net-next v3
 *    2/5] "net: phy: Add support for the Maxio MAE0621A", maxio.c lines
 *    61-79), and this board needs it in Linux because mainline's DTS says
 *    clock_in_out = "input", which makes dwmac-rk take the MAC TX clock from
 *    the PHY's CLKOUT pin (SDGMAC_GRF GMAC_CON0 bit 7 = IO). This firmware
 *    sets that bit to 0 (CRU) in GmacSetToRgmii(), the same choice as
 *    LCKFB's own vendor DTS (rk3576-lckfb-tspi-3m-eth.dtsi: clock_in_out =
 *    "output"), so the MAC never uses CLKOUT here and its frequency does not
 *    matter to UEFI.
 *
 *  - Analog tuning. Armbian's out-of-tree vendor driver
 *    (patch/kernel/archive/rockchip64-7.3/
 *    general-drv-net-phy-Add-maxio-phys-driver.patch, lines 416-452) writes
 *    about 25 undocumented registers at config_init. Neither the datasheet
 *    nor the proposed mainline driver has them; they are not copied.
 *
 *  The registers dumped below are the three whose defaults decide whether
 *  UEFI networking works with no writes at all: CLKOUT (PHYCR2), the MDIO
 *  address 0 broadcast response this driver relies on (PHYCR1 bit 13), and
 *  MIICR, which holds the only documented RGMII in-band bit. DwcEqosSnpDxe
 *  takes link state from the MAC's PHYIF_CONTROL_STATUS register, i.e. from
 *  RGMII in-band status sent by the PHY.
 *
 *  Not run on hardware.
 *
 *  Reference: MAE0621A-Q3C, MAE0621A-Q3I Gigabit Ethernet Transceiver
 *  Datasheet, Version 1.1, 2024-8 (LCSC C49308069).
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/DebugLib.h>

#include "EthernetPhy.h"

/*
 * Datasheet Table 8-2 register map, pages 21-22: register 31 is PAGSR, and
 * page 0 (or 0xA42, the default) maps the IEEE standard registers 0 to 15.
 */
#define MAXIO_PAGSR       0x1F
#define MAXIO_PAGE_IEEE   0x0000

/* Datasheet 8.4.14, Table 8-17, page 31 */
#define MAXIO_PHYCR1_PAGE            0x0A43
#define MAXIO_PHYCR1                 0x18
#define   MAXIO_PHYCR1_PHYAD0_EN     BIT13

/* Datasheet 8.4.15, Table 8-18, page 32 */
#define MAXIO_PHYCR2_PAGE            0x0A43
#define MAXIO_PHYCR2                 0x19
#define   MAXIO_PHYCR2_CLKOUT_125M   BIT11
#define   MAXIO_PHYCR2_RXC_EN        BIT1
#define   MAXIO_PHYCR2_CLKOUT_EN     BIT0

/* Datasheet 8.4.21, Table 8-24, page 35 */
#define MAXIO_MIICR_PAGE             0x0D08
#define MAXIO_MIICR                  0x15
#define   MAXIO_MIICR_INBAND_CRS_EN  BIT6

STATIC
UINT16
MaxioReadPaged (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT16                Page,
  IN UINT16                Reg
  )
{
  UINT16  Value;

  /* Datasheet 7.4.1 "Change Page", page 13: write the page to register 31. */
  PhyWrite (GmacBase, 0, MAXIO_PAGSR, Page);
  PhyRead (GmacBase, 0, Reg, &Value);

  return Value;
}

STATIC
VOID
Mae0621aQ3cReport (
  IN EFI_PHYSICAL_ADDRESS  GmacBase
  )
{
  UINT16  Phycr1;
  UINT16  Phycr2;
  UINT16  Miicr;

  Phycr1 = MaxioReadPaged (GmacBase, MAXIO_PHYCR1_PAGE, MAXIO_PHYCR1);
  Phycr2 = MaxioReadPaged (GmacBase, MAXIO_PHYCR2_PAGE, MAXIO_PHYCR2);
  Miicr  = MaxioReadPaged (GmacBase, MAXIO_MIICR_PAGE, MAXIO_MIICR);

  /*
   * Back to the IEEE registers (datasheet 7.4.1 step 3, and Table 8-2
   * note 2, pages 21-22), so nothing that follows reads a vendor page.
   */
  PhyWrite (GmacBase, 0, MAXIO_PAGSR, MAXIO_PAGE_IEEE);

  DEBUG ((
    DEBUG_INFO,
    "%a: PHYCR1=0x%04x (addr 0 broadcast %a) PHYCR2=0x%04x (CLKOUT %a %a MHz, RXC %a) MIICR=0x%04x (in-band CRS %a)\n",
    __func__,
    Phycr1,
    (Phycr1 & MAXIO_PHYCR1_PHYAD0_EN) ? "on" : "off",
    Phycr2,
    (Phycr2 & MAXIO_PHYCR2_CLKOUT_EN) ? "on" : "off",
    (Phycr2 & MAXIO_PHYCR2_CLKOUT_125M) ? "125" : "25",
    (Phycr2 & MAXIO_PHYCR2_RXC_EN) ? "on" : "off",
    Miicr,
    (Miicr & MAXIO_MIICR_INBAND_CRS_EN) ? "on" : "off"
    ));
}

EFI_STATUS
EFIAPI
MaxioPhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT32                PhyId
  )
{
  switch (PhyId) {
    /*
     * 0x7b744412 is the -Q3C ID in the proposed mainline binding
     * (maxio,mae0621a.yaml, [PATCH net-next v3 1/5]) and in the Armbian
     * vendor driver (patch lines 495-497, "MAE0621A/B-Q3C(I)"). The -Q2C part
     * (0x7b744411) has a different vendor init and no datasheet here, so
     * it is not matched.
     */
    case 0x7B744412:
      DEBUG ((DEBUG_INFO, "%a: Found Maxio MAE0621A-Q3C GbE PHY, strap defaults kept\n", __func__));
      Mae0621aQ3cReport (GmacBase);
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}
