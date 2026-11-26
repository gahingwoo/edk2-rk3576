/** @file
 *
 *  Copyright (c) 2022, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

#include "EthernetPhy.h"

/* Realtek RTL8211F PHY registers */
#define PAGSR      0x1F
#define LCR        0x10
#define LCR_VALUE  0x6940

/*
 * RGMII delays, page 0xD08, as Linux's rtl8211f_config_internal_delay()
 * sets them: TXCR bit 8 delays TXC, RXCR bit 3 delays RXC, by 2 ns each.
 * Their power-on values come from strap pins, so a board that relies on
 * "rgmii-id" must set them: without the TX delay the link comes up and
 * frames arrive, but every frame sent is sampled wrong by the PHY and
 * dropped by the peer (ROCK 4D under Windows: broadcast and multicast
 * received, no unicast ever, DHCP never answered).
 */
#define RTL8211F_DELAY_PAGE  0xD08
#define RTL8211F_TXCR        0x11
#define RTL8211F_TX_DELAY    BIT8
#define RTL8211F_RXCR        0x15
#define RTL8211F_RX_DELAY    BIT3

#define RK3576_GMAC0_BASE  0x2A220000

STATIC
VOID
RTL8211FSetDelay (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT16                Reg,
  IN UINT16                Bit,
  IN BOOLEAN               Enable
  )
{
  UINT16  Value;

  PhyRead (GmacBase, 0, Reg, &Value);
  Value = Enable ? (Value | Bit) : (Value & ~Bit);
  PhyWrite (GmacBase, 0, Reg, Value);
}

STATIC
VOID
RTL8211FPhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase
  )
{
  BOOLEAN  PhyTxDelay;
  BOOLEAN  PhyRxDelay;
  UINT16   Tx;
  UINT16   Rx;

  /* The PHY adds a delay the MAC does not: no MAC-side delay = "rgmii-id". */
  if (GmacBase == RK3576_GMAC0_BASE) {
    PhyTxDelay = FixedPcdGet8 (PcdGmac0TxDelay) == 0;
    PhyRxDelay = FixedPcdGet8 (PcdGmac0RxDelay) == 0;
  } else {
    PhyTxDelay = FixedPcdGet8 (PcdGmac1TxDelay) == 0;
    PhyRxDelay = FixedPcdGet8 (PcdGmac1RxDelay) == 0;
  }

  PhyWrite (GmacBase, 0, PAGSR, 0xD04);
  MicroSecondDelay (10000);
  PhyWrite (GmacBase, 0, LCR, LCR_VALUE);
  MicroSecondDelay (10000);

  PhyWrite (GmacBase, 0, PAGSR, RTL8211F_DELAY_PAGE);
  RTL8211FSetDelay (GmacBase, RTL8211F_TXCR, RTL8211F_TX_DELAY, PhyTxDelay);
  RTL8211FSetDelay (GmacBase, RTL8211F_RXCR, RTL8211F_RX_DELAY, PhyRxDelay);
  PhyRead (GmacBase, 0, RTL8211F_TXCR, &Tx);
  PhyRead (GmacBase, 0, RTL8211F_RXCR, &Rx);
  PhyWrite (GmacBase, 0, PAGSR, 0);

  DEBUG ((
    DEBUG_INFO,
    "%a: RGMII delay in PHY: TX %a (TXCR 0x%04x), RX %a (RXCR 0x%04x)\n",
    __func__,
    PhyTxDelay ? "on" : "off",
    Tx,
    PhyRxDelay ? "on" : "off",
    Rx
    ));
}

EFI_STATUS
EFIAPI
RealtekPhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT32                PhyId
  )
{
  switch (PhyId) {
    case 0x001CC916:
      DEBUG ((DEBUG_INFO, "%a: Found Realtek RTL8211F GbE PHY\n", __func__));
      RTL8211FPhyInit (GmacBase);
      break;
    case 0x001CC878:
      DEBUG ((DEBUG_INFO, "%a: Found Realtek RTL8211F-VD GbE PHY\n", __func__));
      RTL8211FPhyInit (GmacBase);
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}
