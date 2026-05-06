/** @file
 *
 *  PCI Host Bridge Init for Rockchip RK3576
 *
 *  Adapted from RK3588 PciHostBridgeInit.c
 *
 *  Copyright 2017, 2020 NXP
 *  Copyright 2021-2023, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rk3576Pcie.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <IndustryStandard/Pci.h>
#include <VarStoreData.h>

#include "PciHostBridgeInit.h"

/* APB Registers */
#define PCIE_CLIENT_GENERAL_CON         0x0000
#define  DEVICE_TYPE_SHIFT              4
#define  DEVICE_TYPE_MASK               (0xFU << DEVICE_TYPE_SHIFT)
#define  DEVICE_TYPE_RC                 (4 << DEVICE_TYPE_SHIFT)
#define  LINK_REQ_RST_GRT               BIT3
#define  LTSSM_ENABLE                   BIT2
#define PCIE_CLIENT_INTR_MASK_LEGACY    0x001C
#define PCIE_CLIENT_POWER_CON           0x002C
#define  APP_CLK_REQ_N                  BIT0
#define  CLK_REQ_N_BYPASS               BIT12
#define  CLK_REQ_N_CON                  BIT13
#define PCIE_CLIENT_HOT_RESET_CTRL      0x0180
#define  APP_LSSTM_ENABLE_ENHANCE       BIT4
#define PCIE_CLIENT_LTSSM_STATUS        0x0300
#define  RDLH_LINK_UP                   BIT17
#define  SMLH_LINK_UP                   BIT16
#define  SMLH_LTSSM_STATE_MASK          0x3f
#define  SMLH_LTSSM_STATE_LINK_UP       0x11

/* DBI Registers */
#define PCI_DEVICE_CLASS          0x000A
#define PCI_BAR0                  0x0010
#define PCI_BAR1                  0x0014
#define PCIE_LINK_CAPABILITY      0x007C
#define PCIE_LINK_STATUS          0x0080
#define  LINK_STATUS_WIDTH_SHIFT  20
#define  LINK_STATUS_WIDTH_MASK   (0xFU << LINK_STATUS_WIDTH_SHIFT)
#define  LINK_STATUS_SPEED_SHIFT  16
#define  LINK_STATUS_SPEED_MASK   (0xFU << LINK_STATUS_SPEED_SHIFT)
#define PCIE_LINK_CTL_2           0x00A0
#define PL_PORT_LINK_CTRL_OFF     0x0710
#define  LINK_CAPABLE_SHIFT       16
#define  LINK_CAPABLE_MASK        (0x3FU << LINK_CAPABLE_SHIFT)
#define  FAST_LINK_MODE           BIT7
#define  DLL_LINK_EN              BIT5
#define PL_GEN2_CTRL_OFF          0x080C
#define  DIRECT_SPEED_CHANGE      BIT17
#define  NUM_OF_LANES_SHIFT       8
#define  NUM_OF_LANES_MASK        (0x1FU << NUM_OF_LANES_SHIFT)
#define PL_MISC_CONTROL_1_OFF     0x08BC
#define  DBI_RO_WR_EN             BIT0

#define PCIE_TYPE0_HDR_DBI2_OFFSET  0x100000

/* ATU Registers */
#define ATU_CAP_BASE  0x300000
#define IATU_REGION_CTRL_OUTBOUND(n)  (ATU_CAP_BASE + ((n) << 9))
#define IATU_REGION_CTRL_1_OFF      0x000
#define  IATU_TYPE_MEM              0
#define  IATU_TYPE_IO               2
#define  IATU_TYPE_CFG0             4
#define  IATU_TYPE_CFG1             5
#define IATU_REGION_CTRL_2_OFF      0x004
#define  IATU_ENABLE                BIT31
#define  IATU_CFG_SHIFT_MODE        BIT28
#define IATU_LWR_BASE_ADDR_OFF      0x008
#define IATU_UPPER_BASE_ADDR_OFF    0x00C
#define IATU_LIMIT_ADDR_OFF         0x010
#define IATU_LWR_TARGET_ADDR_OFF    0x014
#define IATU_UPPER_TARGET_ADDR_OFF  0x018

/*
 * RK3576 CRU clock gate registers.
 * CRU_BASE = 0x27200000
 * CRU_CLKGATE_CON(n) = CRU_BASE + 0x800 + n * 4
 *
 * Hiword-mask write convention (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE):
 *   bit = 1 means gated; bit = 0 means ungated.
 *   To UNGATE bit b: write (1U << (b + 16)) to the register — set mask, clear data.
 */
#define CRU_CLKGATE_CON(n)       (0x27200000U + 0x800U + (UINT32)(n) * 4U)
#define CRU_UNGATE(addr, bit)    MmioWrite32 ((addr), (1U << (UINTN)((bit) + 16)))

BOOLEAN
IsPcieNumEnabled (
  UINTN  PcieNum
  )
{
  switch (PcieNum) {
    case PCIE_SEGMENT_PCIE0:
      return (PcdGet32 (PcdComboPhy0ModeDefault) == COMBO_PHY_MODE_PCIE);
    case PCIE_SEGMENT_PCIE1:
      return (PcdGet32 (PcdComboPhy1ModeDefault) == COMBO_PHY_MODE_PCIE);
    default:
      return FALSE;
  }
}

STATIC
VOID
PciSetupClocks (
  IN UINTN  Segment
  )
{
  /*
   * ACLK_PHP_ROOT (CRU_CLKGATE_CON(34) bit 7) feeds all ACLK_PCIEx signals.
   * Ungate it first regardless of segment.
   */
  CRU_UNGATE (CRU_CLKGATE_CON (34), 7);

  if (Segment == PCIE_SEGMENT_PCIE0) {
    /* CLK_PCIE0_AUX: CRU_CLKGATE_CON(34) bit 14 */
    CRU_UNGATE (CRU_CLKGATE_CON (34), 14);
    /* ACLK_PCIE0_MST: CRU_CLKGATE_CON(34) bit 15 */
    CRU_UNGATE (CRU_CLKGATE_CON (34), 15);
    /* ACLK_PCIE0_SLV: CRU_CLKGATE_CON(35) bit 0 */
    CRU_UNGATE (CRU_CLKGATE_CON (35), 0);
    /* ACLK_PCIE0_DBI: CRU_CLKGATE_CON(35) bit 1 */
    CRU_UNGATE (CRU_CLKGATE_CON (35), 1);
  } else {
    /* CLK_PCIE1_AUX: CRU_CLKGATE_CON(36) bit 8 */
    CRU_UNGATE (CRU_CLKGATE_CON (36), 8);
    /* ACLK_PCIE1_MST: CRU_CLKGATE_CON(36) bit 9 */
    CRU_UNGATE (CRU_CLKGATE_CON (36), 9);
    /* ACLK_PCIE1_SLV: CRU_CLKGATE_CON(36) bit 10 */
    CRU_UNGATE (CRU_CLKGATE_CON (36), 10);
    /* ACLK_PCIE1_DBI: CRU_CLKGATE_CON(36) bit 11 */
    CRU_UNGATE (CRU_CLKGATE_CON (36), 11);
  }
}

STATIC
VOID
PciSetRcMode (
  IN EFI_PHYSICAL_ADDRESS  ApbBase
  )
{
  MmioWrite32 (ApbBase + PCIE_CLIENT_INTR_MASK_LEGACY, 0xFFFF0000);
  MmioWrite32 (
    ApbBase + PCIE_CLIENT_HOT_RESET_CTRL,
    (APP_LSSTM_ENABLE_ENHANCE << 16) | APP_LSSTM_ENABLE_ENHANCE
    );

  /* Disable clock request requirement (ROCK 4D has no CLKREQ# signal routed) */
  MmioWrite32 (ApbBase + PCIE_CLIENT_POWER_CON, APP_CLK_REQ_N << 16);
  MmioWrite32 (
    ApbBase + PCIE_CLIENT_POWER_CON,
    ((CLK_REQ_N_BYPASS|CLK_REQ_N_CON) << 16) | CLK_REQ_N_BYPASS
    );

  MmioWrite32 (
    ApbBase + PCIE_CLIENT_GENERAL_CON,
    (DEVICE_TYPE_MASK << 16) | DEVICE_TYPE_RC
    );
}

STATIC
VOID
PciSetupBars (
  IN EFI_PHYSICAL_ADDRESS  DbiBase
  )
{
  MmioWrite16 (DbiBase + PCI_DEVICE_CLASS, (PCI_CLASS_BRIDGE << 8) | PCI_CLASS_BRIDGE_P2P);

  /* Disable BAR0 + BAR1 of root port — they are unused */
  MmioWrite32 (DbiBase + PCIE_TYPE0_HDR_DBI2_OFFSET + PCI_BAR0, 0x0);
  MmioWrite32 (DbiBase + PCIE_TYPE0_HDR_DBI2_OFFSET + PCI_BAR1, 0x0);
}

STATIC
VOID
PciDirectSpeedChange (
  IN EFI_PHYSICAL_ADDRESS  DbiBase
  )
{
  MmioOr32 (DbiBase + PL_GEN2_CTRL_OFF, DIRECT_SPEED_CHANGE);
}

STATIC
VOID
PciSetupLinkSpeed (
  IN EFI_PHYSICAL_ADDRESS  DbiBase,
  IN UINT32                Speed,
  IN UINT32                NumLanes
  )
{
  /* Select target link speed */
  MmioAndThenOr32 (DbiBase + PCIE_LINK_CTL_2, ~0xFU, Speed);
  MmioAndThenOr32 (DbiBase + PCIE_LINK_CAPABILITY, ~0xFU, Speed);

  /* Disable fast link mode, select number of lanes, enable link init */
  MmioAndThenOr32 (
    DbiBase + PL_PORT_LINK_CTRL_OFF,
    ~(LINK_CAPABLE_MASK | FAST_LINK_MODE),
    DLL_LINK_EN | (((NumLanes * 2) - 1) << LINK_CAPABLE_SHIFT)
    );

  /* Select link width */
  MmioAndThenOr32 (
    DbiBase + PL_GEN2_CTRL_OFF,
    ~NUM_OF_LANES_MASK,
    NumLanes << NUM_OF_LANES_SHIFT
    );
}

STATIC
VOID
PciGetLinkSpeedWidth (
  IN EFI_PHYSICAL_ADDRESS  DbiBase,
  OUT UINT32               *Speed,
  OUT UINT32               *Width
  )
{
  UINT32  Val;

  Val    = MmioRead32 (DbiBase + PCIE_LINK_STATUS);
  *Speed = (Val & LINK_STATUS_SPEED_MASK) >> LINK_STATUS_SPEED_SHIFT;
  *Width = (Val & LINK_STATUS_WIDTH_MASK) >> LINK_STATUS_WIDTH_SHIFT;
}

STATIC
VOID
PciPrintLinkSpeedWidth (
  IN UINT32  Speed,
  IN UINT32  Width
  )
{
  char  LinkSpeedBuf[6];

  switch (Speed) {
    case 1:
      AsciiStrCpyS (LinkSpeedBuf, sizeof (LinkSpeedBuf), "2.5");
      break;
    case 2:
      AsciiStrCpyS (LinkSpeedBuf, sizeof (LinkSpeedBuf), "5.0");
      break;
    case 3:
      AsciiStrCpyS (LinkSpeedBuf, sizeof (LinkSpeedBuf), "8.0");
      break;
    default:
      AsciiSPrint (LinkSpeedBuf, sizeof (LinkSpeedBuf), "?");
      break;
  }

  DEBUG ((DEBUG_INIT, "PCIe: Link up (x%u, %a GT/s)\n", Width, LinkSpeedBuf));
}

STATIC
VOID
PciEnableLtssm (
  IN EFI_PHYSICAL_ADDRESS  ApbBase,
  IN BOOLEAN               Enable
  )
{
  UINT32  Val;

  Val  = (LINK_REQ_RST_GRT | LTSSM_ENABLE) << 16;
  Val |= LINK_REQ_RST_GRT;
  if (Enable) {
    Val |= LTSSM_ENABLE;
  }

  MmioWrite32 (ApbBase + PCIE_CLIENT_GENERAL_CON, Val);
}

STATIC
BOOLEAN
PciIsLinkUp (
  IN EFI_PHYSICAL_ADDRESS  ApbBase
  )
{
  STATIC UINT32  LastVal = 0xFFFFFFFF;
  UINT32         Val;

  Val = MmioRead32 (ApbBase + PCIE_CLIENT_LTSSM_STATUS);
  if (Val != LastVal) {
    DEBUG ((DEBUG_INIT, "PCIe: LTSSM_STATUS=0x%08X\n", Val));
    LastVal = Val;
  }

  if ((Val & RDLH_LINK_UP) == 0) {
    return FALSE;
  }

  if ((Val & SMLH_LINK_UP) == 0) {
    return FALSE;
  }

  return (Val & SMLH_LTSSM_STATE_MASK) == SMLH_LTSSM_STATE_LINK_UP;
}

STATIC
VOID
PciSetupAtu (
  IN EFI_PHYSICAL_ADDRESS  DbiBase,
  IN UINT32                Index,
  IN UINT32                Type,
  IN UINT64                CpuBase,
  IN UINT64                BusBase,
  IN UINT64                Length
  )
{
  UINT32  Ctrl2Off = IATU_ENABLE;

  if ((Type == IATU_TYPE_CFG0) || (Type == IATU_TYPE_CFG1)) {
    Ctrl2Off |= IATU_CFG_SHIFT_MODE;
  }

  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_LWR_BASE_ADDR_OFF,
    (UINT32)CpuBase
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_UPPER_BASE_ADDR_OFF,
    (UINT32)(CpuBase >> 32)
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_LIMIT_ADDR_OFF,
    (UINT32)(CpuBase + Length - 1)
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_LWR_TARGET_ADDR_OFF,
    (UINT32)BusBase
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_UPPER_TARGET_ADDR_OFF,
    (UINT32)(BusBase >> 32)
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_REGION_CTRL_1_OFF,
    Type
    );
  MmioWrite32 (
    DbiBase + IATU_REGION_CTRL_OUTBOUND (Index) + IATU_REGION_CTRL_2_OFF,
    Ctrl2Off
    );

  gBS->Stall (10000);
}

STATIC
VOID
PciValidateCfg0 (
  IN  UINT32                Segment,
  IN  EFI_PHYSICAL_ADDRESS  Cfg0Base
  )
{
  EFI_STATUS  Status;

  /*
   * Check if the downstream device appears mirrored (due to 64 KB iATU granularity)
   * and needs config accesses shifted for single-device ECAM mode in ACPI.
   */
  if (  (MmioRead32 (Cfg0Base + 0x8000) == 0xffffffff)
     && (MmioRead32 (Cfg0Base) != 0xffffffff))
  {
    Status = PcdSet32S (
               PcdPcieEcamCompliantSegmentsMask,
               PcdGet32 (PcdPcieEcamCompliantSegmentsMask) | (1U << Segment)
               );
    ASSERT_EFI_ERROR (Status);
    DEBUG ((DEBUG_INFO, "PCIe: Working CFG0 TLP filtering for connected device!\n"));
  }
}

EFI_STATUS
InitializePciHost (
  UINT32  Segment
  )
{
  EFI_PHYSICAL_ADDRESS  ApbBase;
  EFI_PHYSICAL_ADDRESS  DbiBase;
  EFI_PHYSICAL_ADDRESS  CfgBase;
  UINTN                 Retry;
  UINT32                LinkSpeed;
  UINT32                LinkWidth;

  if (Segment >= NUM_PCIE_CONTROLLER) {
    DEBUG ((DEBUG_WARN, "PCIe: Invalid segment %u\n", Segment));
    return EFI_INVALID_PARAMETER;
  }

  ApbBase = PCIE_APB_BASE (Segment);
  DbiBase = PCIE_DBI_BASE (Segment);

  DEBUG ((DEBUG_INIT, "\nPCIe: Segment %u\n", Segment));
  DEBUG ((DEBUG_INIT, "PCIe: ApbBase 0x%lx\n", ApbBase));
  DEBUG ((DEBUG_INIT, "PCIe: DbiBase 0x%lx\n", DbiBase));

  /* Board-specific power enable (no-op on ROCK 4D) */
  PcieIoInit (Segment);
  PciePowerEn (Segment, TRUE);
  gBS->Stall (100000);

  /*
   * ComboPHY for PCIe is configured earlier by RK3576Dxe.
   * PCLK_PHP_ROOT and PCLK_PCIEx are also ungated there.
   * Here we ungate the AXI/AUX clocks needed for DWC controller operation.
   */
  DEBUG ((DEBUG_INIT, "PCIe: Setup clocks\n"));
  PciSetupClocks (Segment);

  DEBUG ((DEBUG_INIT, "PCIe: Switching to RC mode\n"));
  PciSetRcMode (ApbBase);

  /* Allow writing RO registers through the DBI */
  MmioOr32 (DbiBase + PL_MISC_CONTROL_1_OFF, DBI_RO_WR_EN);

  DEBUG ((DEBUG_INIT, "PCIe: Setup BARs\n"));
  PciSetupBars (DbiBase);

  /*
   * Setup iATU windows.
   * RK3576 PCIe windows are identity-mapped (CPU addr == bus addr).
   * CfgBase points to ECAM space for buses 0-15 (PCIe0) or 32-47 (PCIe1).
   * Bus 0 is the root port itself — no iATU needed.
   * Bus 1 uses type CFG0; buses 2+ use type CFG1.
   * Identity mapping means BusBase equals CpuBase for all windows.
   */
  DEBUG ((DEBUG_INIT, "PCIe: Setup iATU\n"));
  CfgBase = PCIE_CFG_BASE (Segment);

  PciSetupAtu (
    DbiBase,
    0,
    IATU_TYPE_CFG0,
    CfgBase + SIZE_1MB,    // Bus 1, 64 KB window (32 KB granule, 64 KB actual)
    CfgBase + SIZE_1MB,
    SIZE_64KB
    );
  PciSetupAtu (
    DbiBase,
    1,
    IATU_TYPE_CFG1,
    CfgBase + SIZE_2MB,    // Bus 2 and above
    CfgBase + SIZE_2MB,
    (PCIE_BUS_COUNT - 2) * SIZE_1MB
    );
  PciSetupAtu (
    DbiBase,
    2,
    IATU_TYPE_IO,
    PCIE_IO_BASE (Segment),
    PCIE_IO_BASE (Segment),   // identity-mapped
    PCIE_IO_SIZE
    );
  PciSetupAtu (
    DbiBase,
    3,
    IATU_TYPE_MEM,
    PCIE_MEM32_BASE (Segment),
    PCIE_MEM32_BASE (Segment), // identity-mapped
    PCIE_MEM32_SIZE
    );

  /* RK3576 is PCIe Gen2 x1 (combo PHY only supports Gen1/Gen2) */
  DEBUG ((DEBUG_INIT, "PCIe: Set link speed (Gen2 x1)\n"));
  PciSetupLinkSpeed (DbiBase, 2, 1);
  PciDirectSpeedChange (DbiBase);

  /* Disallow writing RO registers through the DBI */
  MmioAnd32 (DbiBase + PL_MISC_CONTROL_1_OFF, ~DBI_RO_WR_EN);

  DEBUG ((DEBUG_INIT, "PCIe: Assert PERST#\n"));
  PciePeReset (Segment, TRUE);

  DEBUG ((DEBUG_INIT, "PCIe: Start LTSSM\n"));
  PciEnableLtssm (ApbBase, TRUE);

  gBS->Stall (100000);
  DEBUG ((DEBUG_INIT, "PCIe: Deassert PERST#\n"));
  PciePeReset (Segment, FALSE);

  /* Wait for link up */
  DEBUG ((DEBUG_INIT, "PCIe: Waiting for link...\n"));
  for (Retry = 10; Retry != 0; Retry--) {
    if (PciIsLinkUp (ApbBase)) {
      break;
    }

    gBS->Stall (100000);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "PCIe: Link up timeout!\n"));
    return EFI_TIMEOUT;
  }

  PciGetLinkSpeedWidth (DbiBase, &LinkSpeed, &LinkWidth);
  PciPrintLinkSpeedWidth (LinkSpeed, LinkWidth);

  PciValidateCfg0 (Segment, CfgBase + SIZE_1MB);

  return EFI_SUCCESS;
}
