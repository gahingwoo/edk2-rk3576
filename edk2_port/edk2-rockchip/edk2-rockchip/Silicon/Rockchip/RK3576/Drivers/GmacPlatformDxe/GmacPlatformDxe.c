/** @file
 *
 *  RK3576 GMAC initializer
 *
 *  Configures the SDGMAC GRF (0x26038000) for RGMII interface mode,
 *  sets clock source to CRU, optionally applies TX/RX delays via
 *  IOC_GRF (0x26040000), resets the PHY GPIO, initialises the PHY
 *  via MDIO, and installs a DwcEqosPlatformDeviceProtocol so that
 *  DwcEqosSnpDxe can drive the MAC.
 *
 *  Hardware overview (rk3576.dtsi / rk3576-rock-4d.dts):
 *    gmac0: 0x2A220000 — RTL8211F PHY, RGMII-ID (delays in PHY)
 *    gmac1: 0x2A230000 — not connected on ROCK 4D
 *    sdgmac_grf: 0x26038000
 *      CON0 (gmac0), CON1 (gmac1): RW with HIWORD write-mask
 *        bit 3  = RMII mode (0 = RGMII)
 *        bit[6:5] = GMII clk divider select (00 = 125 MHz / 1 Gbps)
 *        bit 7  = IO clk select (0 = CRU, 1 = IO pin)
 *    ioc_grf: 0x26040000
 *      VCCIO0_1_3_IOC_CON2/3  (gmac0 m0/m1, offsets 0x6408/0x640C)
 *      VCCIO0_1_3_IOC_CON4/5  (gmac1 m0/m1, offsets 0x6410/0x6414)
 *        bit 7  = TX delay enable
 *        bit 15 = RX delay enable
 *        bits[6:0]  = TX delay value
 *        bits[14:8] = RX delay value
 *
 *  Copyright (c) 2021-2022, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseCryptLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/NetLib.h>
#include <Library/OtpLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DwcEqosPlatformDevice.h>

#include "EthernetPhy.h"

/* HIWORD write-mask helper: set/clear bits with top 16 bits as write enables */
#define HIWORD_UPDATE(val, mask, shift) \
        (((val) << (shift)) | ((mask) << ((shift) + 16)))

/* SDGMAC_GRF — syscon @ 0x26038000 */
#define RK3576_SDGMAC_GRF_BASE  0x26038000UL
#define RK3576_GRF_GMAC_CON0    (RK3576_SDGMAC_GRF_BASE + 0x0020)  /* gmac0 */
#define RK3576_GRF_GMAC_CON1    (RK3576_SDGMAC_GRF_BASE + 0x0024)  /* gmac1 */

/* CON0/CON1 bit fields (HIWORD write-mask format) */
#define RK3576_GMAC_RMII_MODE_SHIFT  3
#define RK3576_GMAC_RMII_MODE_MASK   0x1U
#define RK3576_GMAC_CLK_SEL_SHIFT    5
#define RK3576_GMAC_CLK_SEL_MASK     0x3U
#define RK3576_GMAC_IO_CLK_SHIFT     7
#define RK3576_GMAC_IO_CLK_MASK      0x1U

/* GMII clock divider values for CON0/CON1 bits [6:5] */
#define RK3576_GMAC_CLK_125M  0  /* 1 Gbps */
#define RK3576_GMAC_CLK_25M   3  /* 100 Mbps */
#define RK3576_GMAC_CLK_2_5M  2  /* 10 Mbps */

/* IOC_GRF — syscon @ 0x26040000, VCCIO0_1_3_IOC registers */
#define RK3576_IOC_GRF_BASE            0x26040000UL
#define RK3576_VCCIO0_1_3_IOC_CON2    (RK3576_IOC_GRF_BASE + 0x6408)  /* gmac0 m0 */
#define RK3576_VCCIO0_1_3_IOC_CON3    (RK3576_IOC_GRF_BASE + 0x640C)  /* gmac0 m1 */
#define RK3576_VCCIO0_1_3_IOC_CON4    (RK3576_IOC_GRF_BASE + 0x6410)  /* gmac1 m0 */
#define RK3576_VCCIO0_1_3_IOC_CON5    (RK3576_IOC_GRF_BASE + 0x6414)  /* gmac1 m1 */

/* IOC delay bits */
#define RK3576_GMAC_TXCLK_DLY_ENABLE   ((1U << (7 + 16)) | (1U << 7))
#define RK3576_GMAC_TXCLK_DLY_DISABLE  ((1U << (7 + 16)) | (0U << 7))
#define RK3576_GMAC_RXCLK_DLY_ENABLE   ((1U << (15 + 16)) | (1U << 15))
#define RK3576_GMAC_RXCLK_DLY_DISABLE  ((1U << (15 + 16)) | (0U << 15))
#define RK3576_GMAC_CLK_TX_DL_CFG(v)   HIWORD_UPDATE(v, 0x7F, 0)
#define RK3576_GMAC_CLK_RX_DL_CFG(v)   HIWORD_UPDATE(v, 0x7F, 8)

/* GMAC MDIO/MAC register offsets (same for both gmac0 and gmac1) */
#define GMAC_MAC_MDIO_ADDRESS              0x0200
#define   GMAC_MAC_MDIO_ADDRESS_PA_SHIFT   21
#define   GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT  16
#define   GMAC_MAC_MDIO_ADDRESS_CR_SHIFT   8
#define   GMAC_MAC_MDIO_ADDRESS_CR_100_150 (1U << GMAC_MAC_MDIO_ADDRESS_CR_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT  2
#define   GMAC_MAC_MDIO_ADDRESS_GOC_READ   (3U << GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GOC_WRITE  (1U << GMAC_MAC_MDIO_ADDRESS_GOC_SHIFT)
#define   GMAC_MAC_MDIO_ADDRESS_GB         BIT0
#define GMAC_MAC_MDIO_DATA                 0x0204

/* PHY ID registers */
#define MII_PHYIDR1  0x02
#define MII_PHYIDR2  0x03

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          VendorDP;
  UINT8                       ControllerId;
  MAC_ADDR_DEVICE_PATH        MacAddrDP;
  EFI_DEVICE_PATH_PROTOCOL    End;
} GMAC_DEVICE_PATH;
#pragma pack ()

typedef struct {
  UINT32                               Signature;
  UINT8                                Id;
  EFI_PHYSICAL_ADDRESS                 BaseAddress;

  BOOLEAN                              Supported;
  UINT8                                TxDelay;
  UINT8                                RxDelay;

  DWC_EQOS_PLATFORM_DEVICE_PROTOCOL    EqosPlatform;
  GMAC_DEVICE_PATH                     DevicePath;
} GMAC_DEVICE;

#define GMAC_DEVICE_SIGNATURE  SIGNATURE_32 ('G', 'M', 'a', 'C')

#define GMAC_DEVICE_FROM_EQOS_PLATFORM(a) \
  CR (a, GMAC_DEVICE, EqosPlatform, GMAC_DEVICE_SIGNATURE)

#define GMAC_DEVICE_INIT(_Id, _BaseAddress)                     \
  {                                                             \
    .Signature    = GMAC_DEVICE_SIGNATURE,                      \
    .Id           = _Id,                                        \
    .BaseAddress  = _BaseAddress,                               \
    .Supported    = FixedPcdGetBool(PcdGmac##_Id##Supported),   \
    .TxDelay      = FixedPcdGet8(PcdGmac##_Id##TxDelay),        \
    .RxDelay      = FixedPcdGet8(PcdGmac##_Id##RxDelay)         \
  }

STATIC GMAC_DEVICE_PATH  mGmacDevicePathTemplate = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(OFFSET_OF (GMAC_DEVICE_PATH, MacAddrDP)),
        (UINT8)((OFFSET_OF (GMAC_DEVICE_PATH, MacAddrDP)) >> 8)
      }
    },
    EFI_CALLER_ID_GUID
  },
  0,
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_MAC_ADDR_DP,
      {
        (UINT8)(sizeof (MAC_ADDR_DEVICE_PATH)),
        (UINT8)((sizeof (MAC_ADDR_DEVICE_PATH)) >> 8)
      }
    },
    {
      { 0 }
    },
    NET_IFTYPE_ETHERNET
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      sizeof (EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};

STATIC GMAC_DEVICE  mGmacDevices[] = {
  GMAC_DEVICE_INIT (0, 0x2A220000),  /* gmac0: RTL8211F on ROCK 4D */
  GMAC_DEVICE_INIT (1, 0x2A230000),  /* gmac1: not connected on ROCK 4D */
};

STATIC ETHERNET_PHY_INIT  mPhyInitList[] = {
  RealtekPhyInit,
};

VOID
PhyRead (
  IN  EFI_PHYSICAL_ADDRESS  GmacBase,
  IN  UINT8                 Phy,
  IN  UINT16                Reg,
  OUT UINT16                *Value
  )
{
  UINT32  Addr;
  UINTN   Retry;

  *Value = 0xFFFFU;

  Addr = GMAC_MAC_MDIO_ADDRESS_CR_100_150 |
         (Phy << GMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
         (Reg << GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT) |
         GMAC_MAC_MDIO_ADDRESS_GOC_READ |
         GMAC_MAC_MDIO_ADDRESS_GB;
  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_ADDRESS, Addr);

  MicroSecondDelay (10000);

  for (Retry = 1000; Retry > 0; Retry--) {
    Addr = MmioRead32 (GmacBase + GMAC_MAC_MDIO_ADDRESS);
    if ((Addr & GMAC_MAC_MDIO_ADDRESS_GB) == 0) {
      *Value = MmioRead32 (GmacBase + GMAC_MAC_MDIO_DATA) & 0xFFFFu;
      break;
    }

    MicroSecondDelay (10);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "MDIO: PHY read timeout!\n"));
    ASSERT (FALSE);
  }
}

VOID
PhyWrite (
  IN EFI_PHYSICAL_ADDRESS  GmacBase,
  IN UINT8                 Phy,
  IN UINT16                Reg,
  IN UINT16                Value
  )
{
  UINT32  Addr;
  UINTN   Retry;

  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_DATA, Value);

  Addr = GMAC_MAC_MDIO_ADDRESS_CR_100_150 |
         (Phy << GMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
         (Reg << GMAC_MAC_MDIO_ADDRESS_RDA_SHIFT) |
         GMAC_MAC_MDIO_ADDRESS_GOC_WRITE |
         GMAC_MAC_MDIO_ADDRESS_GB;
  MmioWrite32 (GmacBase + GMAC_MAC_MDIO_ADDRESS, Addr);

  MicroSecondDelay (10000);

  for (Retry = 1000; Retry > 0; Retry--) {
    Addr = MmioRead32 (GmacBase + GMAC_MAC_MDIO_ADDRESS);
    if ((Addr & GMAC_MAC_MDIO_ADDRESS_GB) == 0) {
      break;
    }

    MicroSecondDelay (10);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "MDIO: PHY write timeout!\n"));
    ASSERT (FALSE);
  }
}

STATIC
VOID
EFIAPI
PhyInit (
  IN EFI_PHYSICAL_ADDRESS  GmacBase
  )
{
  EFI_STATUS  Status;
  UINT16      PhyIdReg;
  UINT32      PhyId;
  UINT32      Index;

  PhyRead (GmacBase, 0, MII_PHYIDR1, &PhyIdReg);
  PhyId = PhyIdReg << 16;
  PhyRead (GmacBase, 0, MII_PHYIDR2, &PhyIdReg);
  PhyId |= PhyIdReg;

  for (Index = 0; Index < ARRAY_SIZE (mPhyInitList); Index++) {
    Status = mPhyInitList[Index](GmacBase, PhyId);
    if (Status == EFI_UNSUPPORTED) {
      continue;
    } else if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: mPhyInitList[%d]() failed: %r\n", __func__, Index, Status));
    }

    return;
  }

  DEBUG ((DEBUG_ERROR, "%a: Unknown PHY ID %08X\n", __func__, PhyId));
}

/*
 * Configure SDGMAC_GRF for RGMII mode (bit 3 = 0) and CRU clock (bit 7 = 0).
 * Use HIWORD write-mask: upper 16 bits are the write-enable mask.
 */
STATIC
VOID
GmacSetToRgmii (
  IN UINT32  Id,
  IN UINT8   TxDelay,
  IN UINT8   RxDelay
  )
{
  EFI_PHYSICAL_ADDRESS  ConReg;
  EFI_PHYSICAL_ADDRESS  IocConM0;
  EFI_PHYSICAL_ADDRESS  IocConM1;
  UINT32                Val;

  ConReg  = (Id == 1) ? RK3576_GRF_GMAC_CON1 : RK3576_GRF_GMAC_CON0;
  IocConM0 = (Id == 1) ? RK3576_VCCIO0_1_3_IOC_CON4 : RK3576_VCCIO0_1_3_IOC_CON2;
  IocConM1 = (Id == 1) ? RK3576_VCCIO0_1_3_IOC_CON5 : RK3576_VCCIO0_1_3_IOC_CON3;

  /* RGMII mode (bit 3 = 0), CRU clock sel (bit 7 = 0), clk div 125 MHz (bits[6:5] = 00) */
  Val = HIWORD_UPDATE (0, RK3576_GMAC_RMII_MODE_MASK, RK3576_GMAC_RMII_MODE_SHIFT) |
        HIWORD_UPDATE (0, RK3576_GMAC_CLK_SEL_MASK,   RK3576_GMAC_CLK_SEL_SHIFT)   |
        HIWORD_UPDATE (0, RK3576_GMAC_IO_CLK_MASK,    RK3576_GMAC_IO_CLK_SHIFT);
  MmioWrite32 (ConReg, Val);

  /* TX/RX delay via IOC_GRF (RGMII-ID: both delays in PHY → 0 from MAC side) */
  if (TxDelay) {
    MmioWrite32 (IocConM0, RK3576_GMAC_TXCLK_DLY_ENABLE | RK3576_GMAC_CLK_TX_DL_CFG (TxDelay));
    MmioWrite32 (IocConM1, RK3576_GMAC_TXCLK_DLY_ENABLE | RK3576_GMAC_CLK_TX_DL_CFG (TxDelay));
  } else {
    MmioWrite32 (IocConM0, RK3576_GMAC_TXCLK_DLY_DISABLE);
    MmioWrite32 (IocConM1, RK3576_GMAC_TXCLK_DLY_DISABLE);
  }

  if (RxDelay) {
    MmioWrite32 (IocConM0, RK3576_GMAC_RXCLK_DLY_ENABLE | RK3576_GMAC_CLK_RX_DL_CFG (RxDelay));
    MmioWrite32 (IocConM1, RK3576_GMAC_RXCLK_DLY_ENABLE | RK3576_GMAC_CLK_RX_DL_CFG (RxDelay));
  } else {
    MmioWrite32 (IocConM0, RK3576_GMAC_RXCLK_DLY_DISABLE);
    MmioWrite32 (IocConM1, RK3576_GMAC_RXCLK_DLY_DISABLE);
  }
}

STATIC
VOID
EFIAPI
GmacPlatformGetConfig (
  IN DWC_EQOS_PLATFORM_DEVICE_PROTOCOL  *This,
  IN DWC_EQOS_CONFIG                    *Config
  )
{
  Config->CsrClockRate  = 125000000;
  Config->AxiBusWidth   = EqosAxiBusWidth64;
  Config->AxiFixedBurst = FALSE;
  Config->AxiMixedBurst = TRUE;
  Config->AxiWrOsrLmt   = 4;
  Config->AxiRdOsrLmt   = 8;
  Config->AxiBlen       = EqosAxiBlen16 | EqosAxiBlen8 | EqosAxiBlen4;
}

STATIC
EFI_STATUS
EFIAPI
GmacPlatformSetInterfaceSpeed (
  IN DWC_EQOS_PLATFORM_DEVICE_PROTOCOL  *This,
  IN UINT32                             Speed
  )
{
  GMAC_DEVICE           *Gmac = GMAC_DEVICE_FROM_EQOS_PLATFORM (This);
  EFI_PHYSICAL_ADDRESS  ConReg;
  UINT32                ClkSel;

  ConReg = (Gmac->Id == 1) ? RK3576_GRF_GMAC_CON1 : RK3576_GRF_GMAC_CON0;

  switch (Speed) {
    case 10:
      ClkSel = RK3576_GMAC_CLK_2_5M;
      break;
    case 100:
      ClkSel = RK3576_GMAC_CLK_25M;
      break;
    case 1000:
      ClkSel = RK3576_GMAC_CLK_125M;
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  MmioWrite32 (ConReg, HIWORD_UPDATE (ClkSel, RK3576_GMAC_CLK_SEL_MASK, RK3576_GMAC_CLK_SEL_SHIFT));

  return EFI_SUCCESS;
}

STATIC
VOID
GmacGetOtpMacAddress (
  OUT EFI_MAC_ADDRESS  *MacAddress
  )
{
  UINT8  OtpData[32];
  UINT8  Hash[SHA256_DIGEST_SIZE];

  /* Derive MAC addresses from the first 32 bytes in the OTP */
  OtpRead (0x00, sizeof (OtpData), OtpData);
  Sha256HashAll (OtpData, sizeof (OtpData), Hash);

  /* Clear multicast bit, set locally administered bit */
  Hash[0] &= 0xFE;
  Hash[0] |= 0x02;

  ZeroMem (MacAddress, sizeof (EFI_MAC_ADDRESS));
  CopyMem (MacAddress, Hash, NET_ETHER_ADDR_LEN);
}

EFI_STATUS
EFIAPI
GmacPlatformDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS       Status;
  EFI_MAC_ADDRESS  MacAddress;
  UINT32           Index;
  GMAC_DEVICE      *Gmac;
  EFI_HANDLE       Handle;

  GmacGetOtpMacAddress (&MacAddress);

  for (Index = 0; Index < ARRAY_SIZE (mGmacDevices); Index++) {
    Gmac = &mGmacDevices[Index];
    if (!Gmac->Supported) {
      continue;
    }

    /* Configure pins */
    GmacIomux (Gmac->Id);

    /* Setup GRF: RGMII interface, CRU clock, delays */
    GmacSetToRgmii (Gmac->Id, Gmac->TxDelay, Gmac->RxDelay);

    /* Assert PHY reset (active-low) */
    GmacIoPhyReset (Gmac->Id, TRUE);
    MicroSecondDelay (20000);

    /* Deassert PHY reset */
    GmacIoPhyReset (Gmac->Id, FALSE);
    MicroSecondDelay (200000);

    /* Initialise PHY via MDIO */
    PhyInit (Gmac->BaseAddress);

    Gmac->EqosPlatform.BaseAddress       = Gmac->BaseAddress;
    Gmac->EqosPlatform.GetConfig         = GmacPlatformGetConfig;
    Gmac->EqosPlatform.SetInterfaceSpeed = GmacPlatformSetInterfaceSpeed;

    CopyMem (&Gmac->EqosPlatform.MacAddress, &MacAddress, NET_ETHER_ADDR_LEN);
    Gmac->EqosPlatform.MacAddress.Addr[5] += Gmac->Id;

    DEBUG ((
      DEBUG_INFO,
      "%a: GMAC%u MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
      __func__,
      Gmac->Id,
      Gmac->EqosPlatform.MacAddress.Addr[0],
      Gmac->EqosPlatform.MacAddress.Addr[1],
      Gmac->EqosPlatform.MacAddress.Addr[2],
      Gmac->EqosPlatform.MacAddress.Addr[3],
      Gmac->EqosPlatform.MacAddress.Addr[4],
      Gmac->EqosPlatform.MacAddress.Addr[5]
      ));

    CopyMem (&Gmac->DevicePath, &mGmacDevicePathTemplate, sizeof (GMAC_DEVICE_PATH));
    CopyMem (&Gmac->DevicePath.MacAddrDP.MacAddress, &Gmac->EqosPlatform.MacAddress, NET_ETHER_ADDR_LEN);
    Gmac->DevicePath.ControllerId = Gmac->Id;

    Handle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gDwcEqosPlatformDeviceProtocolGuid,
                    &Gmac->EqosPlatform,
                    &gEfiDevicePathProtocolGuid,
                    &Gmac->DevicePath,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to install GMAC%u EQOS device. Status=%r\n",
        __func__,
        Gmac->Id,
        Status
        ));
    }
  }

  return EFI_SUCCESS;
}
