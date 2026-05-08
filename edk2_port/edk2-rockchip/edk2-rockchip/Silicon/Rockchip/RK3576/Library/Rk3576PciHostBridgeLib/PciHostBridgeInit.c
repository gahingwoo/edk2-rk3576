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
/*
 * CRU soft-reset registers (hiword-mask format: bit = 1 resets, 0 deasserts).
 * CRU_SOFTRST_CON(n) = CRU_BASE + 0xA00 + n*4.  Reset ID = n*16 + bit.
 *   PCIe0 "pipe" reset: ID=0xF1 = 15*16+1 → CON(15) bit  1
 *   PCIe0 "pwr"  reset: ID=0xF2 = 15*16+2 → CON(15) bit  2
 *   PCIe1 "pipe" reset: ID=0xF8 = 15*16+8 → CON(15) bit  8
 *   PCIe1 "pwr"  reset: ID=0xF9 = 15*16+9 → CON(15) bit  9
 * Both must be deasserted before any DBI (0x22000000/0x22400000) access;
 * otherwise the AXI slave never responds → infinite AXI bus stall.
 */
#define CRU_SOFTRST_CON(n)       (0x27200000U + 0xA00U + (UINT32)(n) * 4U)
#define CRU_DEASSERT(addr, bit)  MmioWrite32 ((addr), (1U << (UINTN)((bit) + 16)))

/*
 * RK3576 PMU power domain registers for PD_PHP.
 * From Linux: pmu@27380000 { ... power-controller { ... power-domain@RK3576_PD_PHP } }
 * rk3576_pmu: pwr_offset=0x210, repair_status_offset=0x570,
 *             req_offset=0x110, ack_offset=0x120, idle_offset=0x128
 * PD_PHP:    pwr_mask=BIT(9), repair_status_mask=BIT(9), req/idle/ack=BIT(15)
 *
 * Without deassert of the PHP NIU bus idle (bit 15 of PHP_PMU_IDLE_REQ), any
 * AXI access to the PHP subsystem (including PCIe0 DBI at 0x22000000) will
 * stall silently forever.  The APB bus (pclk, different path) is unaffected.
 */
#define RK3576_PMU_BASE         0x27380000U
#define PHP_PMU_PWR_CON         (RK3576_PMU_BASE + 0x210U)  /* power on/off */
#define PHP_PMU_REPAIR_STATUS   (RK3576_PMU_BASE + 0x570U)  /* 1=powered on */
#define PHP_PMU_IDLE_REQ        (RK3576_PMU_BASE + 0x110U)  /* 1=idle requested */
#define PHP_PMU_IDLE_ACK        (RK3576_PMU_BASE + 0x120U)  /* 1=idle acknowledged */
#define PHP_PMU_IDLE_STS        (RK3576_PMU_BASE + 0x128U)  /* 1=currently idle */
#define  PHP_PMU_PWR_BIT        BIT9
#define  PHP_PMU_IDLE_BIT       BIT15
#define  PHP_HIWORD(bit)        ((bit) << 16)             /* hiword-mask field */

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
  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetupClocks enter; CRU_CLKGATE_CON34=0x%08X CRU_CLKGATE_CON35=0x%08X\n",
          (UINT32)Segment,
          MmioRead32 (CRU_CLKGATE_CON (34)),
          MmioRead32 (CRU_CLKGATE_CON (35))));

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

  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetupClocks done; CRU_CLKGATE_CON34=0x%08X CRU_CLKGATE_CON35=0x%08X\n",
          (UINT32)Segment,
          MmioRead32 (CRU_CLKGATE_CON (34)),
          MmioRead32 (CRU_CLKGATE_CON (35))));
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

  DEBUG ((DEBUG_WARN, "PCIe: Link up (x%u, %a GT/s)\n", Width, LinkSpeedBuf));
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
    DEBUG ((DEBUG_INFO, "PCIe: LTSSM_STATUS=0x%08X\n", Val));
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
  /*
   * Check if the downstream device appears mirrored (due to 64 KB iATU granularity)
   * and needs config accesses shifted for single-device ECAM mode in ACPI.
   * Note: PcdPcieEcamCompliantSegmentsMask runtime update skipped here;
   * ACPI Pcie.asl uses a fixed value. Adjust if full ECAM compliance needed.
   */
  if (  (MmioRead32 (Cfg0Base + 0x8000) == 0xffffffff)
     && (MmioRead32 (Cfg0Base) != 0xffffffff))
  {
    DEBUG ((DEBUG_INFO, "PCIe: Working CFG0 TLP filtering for connected device!\n"));
  }
}

STATIC
VOID
PciPhpPowerDomain (
  IN UINTN  Segment
  )
{
  UINTN   Timeout;

  DEBUG ((DEBUG_WARN,
          "PCIe%u: PHP PMU state — PWR_CON=0x%08X REPAIR_STS=0x%08X IDLE_REQ=0x%08X IDLE_ACK=0x%08X IDLE_STS=0x%08X\n",
          (UINT32)Segment,
          MmioRead32 (PHP_PMU_PWR_CON),
          MmioRead32 (PHP_PMU_REPAIR_STATUS),
          MmioRead32 (PHP_PMU_IDLE_REQ),
          MmioRead32 (PHP_PMU_IDLE_ACK),
          MmioRead32 (PHP_PMU_IDLE_STS)));

  /*
   * Step 1: Power on PD_PHP if not already on.
   * repair_status bit 9 = 1 means powered on; 0 means off.
   * Hiword-mask write to PWR_CON: mask=BIT(9), data=0 → power on.
   */
  if (!(MmioRead32 (PHP_PMU_REPAIR_STATUS) & PHP_PMU_PWR_BIT)) {
    DEBUG ((DEBUG_WARN, "PCIe%u: PD_PHP not powered, enabling now...\n", (UINT32)Segment));
    MmioWrite32 (PHP_PMU_PWR_CON, PHP_HIWORD (PHP_PMU_PWR_BIT) | 0U);

    for (Timeout = 100000; Timeout != 0; Timeout--) {
      if (MmioRead32 (PHP_PMU_REPAIR_STATUS) & PHP_PMU_PWR_BIT) {
        break;
      }
    }
    if (Timeout == 0) {
      DEBUG ((DEBUG_WARN, "PCIe%u: PD_PHP power-on timeout! REPAIR_STS=0x%08X\n",
              (UINT32)Segment, MmioRead32 (PHP_PMU_REPAIR_STATUS)));
    } else {
      DEBUG ((DEBUG_WARN, "PCIe%u: PD_PHP powered on OK\n", (UINT32)Segment));
    }
  } else {
    DEBUG ((DEBUG_WARN, "PCIe%u: PD_PHP already powered on\n", (UINT32)Segment));
  }

  /*
   * Step 2: Deassert PHP AXI NIU bus idle.
   * Without this, the AXI interconnect to 0x22000000 (PCIe0 DBI) is
   * held idle after BL31 and any AXI read will stall the bus silently.
   * APB (PCLK path) is unaffected by this idle bit.
   * Hiword-mask write to IDLE_REQ: mask=BIT(15), data=0 → not idle.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: Deassert PHP NIU bus idle...\n", (UINT32)Segment));
  MmioWrite32 (PHP_PMU_IDLE_REQ, PHP_HIWORD (PHP_PMU_IDLE_BIT) | 0U);

  /* Wait for idle ACK to clear (bit 15 = 0). */
  for (Timeout = 100000; Timeout != 0; Timeout--) {
    if (!(MmioRead32 (PHP_PMU_IDLE_ACK) & PHP_PMU_IDLE_BIT)) {
      break;
    }
  }
  if (Timeout == 0) {
    DEBUG ((DEBUG_WARN, "PCIe%u: PHP idle ACK timeout! ACK=0x%08X\n",
            (UINT32)Segment, MmioRead32 (PHP_PMU_IDLE_ACK)));
  }

  /* Wait for idle status to clear (bit 15 = 0). */
  for (Timeout = 100000; Timeout != 0; Timeout--) {
    if (!(MmioRead32 (PHP_PMU_IDLE_STS) & PHP_PMU_IDLE_BIT)) {
      break;
    }
  }
  if (Timeout == 0) {
    DEBUG ((DEBUG_WARN, "PCIe%u: PHP idle status timeout! STS=0x%08X\n",
            (UINT32)Segment, MmioRead32 (PHP_PMU_IDLE_STS)));
  }

  DEBUG ((DEBUG_WARN,
          "PCIe%u: PHP PMU done — PWR_CON=0x%08X REPAIR_STS=0x%08X IDLE_REQ=0x%08X IDLE_STS=0x%08X\n",
          (UINT32)Segment,
          MmioRead32 (PHP_PMU_PWR_CON),
          MmioRead32 (PHP_PMU_REPAIR_STATUS),
          MmioRead32 (PHP_PMU_IDLE_REQ),
          MmioRead32 (PHP_PMU_IDLE_STS)));
}

STATIC
VOID
PciSetupResets (
  IN UINTN  Segment
  )
{
  UINT32  SrstBefore;
  UINT32  SrstAfter;
  UINT32  DbiId;

  /*
   * Cycle PCIe controller "pipe" (APB/PCLK) and "pwr" (core power-up)
   * soft-resets: ASSERT then DEASSERT.
   *
   * Correct register mapping from drivers/clk/rockchip/rst-rk3576.c
   * (mainline Linux).  The RK3576 CRU driver uses a lookup table, NOT
   * the simple formula (id / 16, id % 16) — the vendor-BSP IDs differ
   * from the mainline sequential IDs in the header.
   *
   * PCIe0:
   *   SRST_P_PCIE0        → CRU_SOFTRST_CON(34) bit 13
   *   SRST_PCIE0_POWER_UP → CRU_SOFTRST_CON(34) bit 15
   *   CRU_SOFTRST_CON(34) = 0x27200000 + 0xA00 + 34*4 = 0x27200A88
   *
   * PCIe1:
   *   SRST_P_PCIE1        → CRU_SOFTRST_CON(36) bit  7
   *   SRST_PCIE1_POWER_UP → CRU_SOFTRST_CON(36) bit  9
   *   CRU_SOFTRST_CON(36) = 0x27200000 + 0xA00 + 36*4 = 0x27200A90
   *
   * Hiword-mask write: (mask_bits << 16) | data_bits.
   *   data=1 → assert (in reset); data=0 → deassert (out of reset).
   *
   * A clean assert-then-deassert cycle is required so the DWC PCIe core
   * synchronises to the ComboPHY PIPE clock on reset exit.
   */
  if (Segment == PCIE_SEGMENT_PCIE0) {
    SrstBefore = MmioRead32 (CRU_SOFTRST_CON (34));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON34=0x%08X before assert\n",
            (UINT32)Segment, SrstBefore));
  } else {
    SrstBefore = MmioRead32 (CRU_SOFTRST_CON (36));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON36=0x%08X before assert\n",
            (UINT32)Segment, SrstBefore));
  }

  /* Step 1: ASSERT resets (put DWC into reset). */
  if (Segment == PCIE_SEGMENT_PCIE0) {
    /* SRST_P_PCIE0=bit13, SRST_PCIE0_POWER_UP=bit15 */
    MmioWrite32 (CRU_SOFTRST_CON (34),
                 (1U << (13 + 16)) | (1U << (15 + 16)) |
                 (1U << 13)        | (1U << 15));
  } else {
    /* SRST_P_PCIE1=bit7, SRST_PCIE1_POWER_UP=bit9 */
    MmioWrite32 (CRU_SOFTRST_CON (36),
                 (1U << (7 + 16)) | (1U << (9 + 16)) |
                 (1U << 7)        | (1U << 9));
  }

  if (Segment == PCIE_SEGMENT_PCIE0) {
    SrstAfter = MmioRead32 (CRU_SOFTRST_CON (34));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON34=0x%08X after assert (bits 13+15 should be 1)\n",
            (UINT32)Segment, SrstAfter));
  } else {
    SrstAfter = MmioRead32 (CRU_SOFTRST_CON (36));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON36=0x%08X after assert (bits 7+9 should be 1)\n",
            (UINT32)Segment, SrstAfter));
  }

  /* Hold reset for ≥1 ms. */
  gBS->Stall (1000);

  /* Step 2: DEASSERT resets (take DWC out of reset). */
  if (Segment == PCIE_SEGMENT_PCIE0) {
    MmioWrite32 (CRU_SOFTRST_CON (34),
                 (1U << (13 + 16)) | (1U << (15 + 16)) | 0U);
  } else {
    MmioWrite32 (CRU_SOFTRST_CON (36),
                 (1U << (7 + 16)) | (1U << (9 + 16)) | 0U);
  }

  if (Segment == PCIE_SEGMENT_PCIE0) {
    SrstAfter = MmioRead32 (CRU_SOFTRST_CON (34));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON34=0x%08X after deassert (bits 13+15 should be 0)\n",
            (UINT32)Segment, SrstAfter));
  } else {
    SrstAfter = MmioRead32 (CRU_SOFTRST_CON (36));
    DEBUG ((DEBUG_WARN, "PCIe%u: SOFTRST_CON36=0x%08X after deassert (bits 7+9 should be 0)\n",
            (UINT32)Segment, SrstAfter));
  }

  /* Allow ≥1 ms for DWC core to complete internal reset-exit sequence. */
  gBS->Stall (1000);

  /*
   * Probe DBI at offset 0 (PCIe Vendor+Device ID of root port).
   * Print BEFORE the read so we can tell from the log if it's the read that hangs.
   * A valid DWC PCIe root port returns 0x17CD or similar; 0xFFFFFFFF = no response.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: probing DBI @ 0x%08lX ...\n",
          (UINT32)Segment, (UINT64)PCIE_DBI_BASE (Segment)));
  DbiId = MmioRead32 (PCIE_DBI_BASE (Segment));
  DEBUG ((DEBUG_WARN, "PCIe%u: DBI VID:DID = 0x%08X\n", (UINT32)Segment, DbiId));
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

  DEBUG ((DEBUG_WARN, "\nPCIe%u: InitializePciHost start\n", Segment));
  DEBUG ((DEBUG_WARN, "PCIe%u: ApbBase=0x%08lX DbiBase=0x%08lX\n",
          Segment, (UINT64)ApbBase, (UINT64)DbiBase));

  /*
   * Board-specific power / PERST# init.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: PcieIoInit...\n", Segment));
  PcieIoInit (Segment);
  DEBUG ((DEBUG_WARN, "PCIe%u: PciePowerEn TRUE...\n", Segment));
  PciePowerEn (Segment, TRUE);
  DEBUG ((DEBUG_WARN, "PCIe%u: Power on done\n", Segment));

  /*
   * Re-assert the AXI/AUX clock gates (idempotent — already set by
   * RK3576Dxe's pre-ungate block, repeated here for safety).
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetupClocks...\n", Segment));
  PciSetupClocks (Segment);
  DEBUG ((DEBUG_WARN, "PCIe%u: Clocks done\n", Segment));

  /*
   * Enable PD_PHP power domain and deassert the PHP AXI NIU bus idle.
   * After BL31, the PHY bus idle bit (PMU IDLE_REQ bit 15) is asserted,
   * silently stalling all AXI traffic to 0x22000000 (PCIe0 DBI).
   * The APB path (PCLK) is a separate bus and is not affected, which is
   * why ComboPHY APB reads work while DBI AXI reads hang.
   * Linux resolves this via rockchip_pmu_set_idle_request(PD_PHP, false).
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: PHP power domain...\n", Segment));
  PciPhpPowerDomain (Segment);
  DEBUG ((DEBUG_WARN, "PCIe%u: PHP power domain done\n", Segment));

  /*
   * Deassert PCIe controller soft-resets so DBI is accessible.  Must happen
   * after clocks are ungated but before any DBI MMIO access.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetupResets...\n", Segment));
  PciSetupResets (Segment);
  DEBUG ((DEBUG_WARN, "PCIe%u: Resets done\n", Segment));

  /*
   * APB registers (0x2A200000+) confirmed accessible from RK3576Dxe.
   * Read LTSSM_STATUS to confirm APB is still reachable before writing.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: APB LTSSM_STATUS=0x%08X (pre-RcMode)\n",
          Segment, MmioRead32 (ApbBase + PCIE_CLIENT_LTSSM_STATUS)));

  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetRcMode...\n", Segment));
  PciSetRcMode (ApbBase);
  DEBUG ((DEBUG_WARN, "PCIe%u: RC mode done; GENERAL_CON=0x%08X\n",
          Segment, MmioRead32 (ApbBase + PCIE_CLIENT_GENERAL_CON)));

  /* Allow writing RO registers through the DBI */
  DEBUG ((DEBUG_WARN, "PCIe%u: DBI RO unlock (DBI+0x%03X)...\n",
          Segment, PL_MISC_CONTROL_1_OFF));
  MmioOr32 (DbiBase + PL_MISC_CONTROL_1_OFF, DBI_RO_WR_EN);
  DEBUG ((DEBUG_WARN, "PCIe%u: DBI RO unlock done\n", Segment));

  DEBUG ((DEBUG_WARN, "PCIe%u: PciSetupBars...\n", Segment));
  PciSetupBars (DbiBase);

  DEBUG ((DEBUG_WARN, "PCIe%u: iATU setup...\n", Segment));
  CfgBase = PCIE_CFG_BASE (Segment);

  /*
   * RK3576 DWC PCIe has only 2 outbound iATU windows (num-ob-windows=2
   * in vendor DTB).  Use them optimally for a direct-attached NVMe device:
   *   Window 0: CFG0 — type-0 config TLPs to bus 1 (NVMe device)
   *   Window 1: MEM32 — 32-bit memory TLPs for NVMe BAR MMIO
   * CFG1 and I/O windows are not needed; there is no PCIe switch on
   * ROCK 4D and NVMe has no I/O BAR.
   * Bus 2+ ECAM reads (above CFG0 window) hit MEM32 ATU → UR (0xFFFF)
   * → PciBusDxe treats as "no device" and skips silently.
   */
  PciSetupAtu (
    DbiBase,
    0,
    IATU_TYPE_CFG0,
    CfgBase + SIZE_1MB,    // Bus 1, 64 KB (NVMe device config space)
    CfgBase + SIZE_1MB,
    SIZE_64KB
    );
  PciSetupAtu (
    DbiBase,
    1,
    IATU_TYPE_MEM,
    PCIE_MEM32_BASE (Segment),
    PCIE_MEM32_BASE (Segment), // identity-mapped: NVMe BAR MMIO
    PCIE_MEM32_SIZE
    );

  /*
   * Link training — Phase 1: establish a stable Gen1 link first.
   *
   * IMPORTANT: Do NOT call PciDirectSpeedChange() before LTSSM runs.
   * That call sets the DIRECT_SPEED_CHANGE bit in PL_GEN2_CTRL_OFF,
   * which causes the DWC to immediately enter Recovery as soon as any
   * Gen1 link trains — attempting a Gen2 speed change.  On RK3576
   * ComboPHY0 this results in LTSSM state 0x0D (Recovery.RcvrCfg)
   * without a successful Gen2 acquisition, dropping the link back to
   * Detect.Quiet and causing a link-up timeout.
   *
   * The correct two-phase sequence is:
   *   Phase 1 — train at Gen1 (reliable), confirm stable L0.
   *   Phase 2 — upgrade to Gen2 via directed speed change once Gen1
   *             is confirmed; PCIe spec mandates fallback to Gen1 if
   *             Gen2 negotiation fails, so this is safe.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: Set link speed Gen1 x1 (initial training)...\n", Segment));
  PciSetupLinkSpeed (DbiBase, 1, 1);

  /* Disallow writing RO registers through the DBI */
  MmioAnd32 (DbiBase + PL_MISC_CONTROL_1_OFF, ~DBI_RO_WR_EN);

  DEBUG ((DEBUG_WARN, "PCIe%u: Assert PERST#...\n", Segment));
  PciePeReset (Segment, TRUE);

  DEBUG ((DEBUG_WARN, "PCIe%u: Enable LTSSM...\n", Segment));
  PciEnableLtssm (ApbBase, TRUE);

  /* PCIe spec T_PVPERL: hold PERST# asserted for at least 100 ms after
   * the reference clock is stable so the endpoint can power up correctly. */
  gBS->Stall (100000);
  DEBUG ((DEBUG_WARN, "PCIe%u: Deassert PERST# (100ms done)...\n", Segment));
  PciePeReset (Segment, FALSE);

  /* Short post-deassert delay — NVMe drives may need a few milliseconds
   * of internal initialization time before they respond to Polling.Active
   * ordered-sets (PCIe spec: endpoint must be link-training-capable within
   * T_PVPERL = 100 ms after PERST# deassert; 20 ms covers most drives). */
  gBS->Stall (20000);

  /* Phase 1: wait for Gen1 link up (up to 1 s) */
  DEBUG ((DEBUG_WARN, "PCIe%u: Waiting for Gen1 link up (up to 1s)...\n", Segment));
  for (Retry = 10; Retry != 0; Retry--) {
    if (PciIsLinkUp (ApbBase)) {
      break;
    }

    gBS->Stall (100000);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "PCIe%u: Gen1 link up timeout — asserting DWC reset (SOFTRST_CON%u)\n",
            (UINT32)Segment, (Segment == PCIE_SEGMENT_PCIE0) ? 34U : 36U));
    /*
     * Assert-only (no deassert): leave the DWC controller in reset so it
     * does not hold the PCIe AXI/PIPE bus in an indeterminate state while
     * Linux boots.  Without this, the unclocked-but-running DWC can cause
     * spurious AXI errors that prevent Linux pcie-dw-rockchip from probing.
     *
     * PCIe0: SRST_P_PCIE0=CON34[13], SRST_PCIE0_POWER_UP=CON34[15]
     * PCIe1: SRST_P_PCIE1=CON36[7],  SRST_PCIE1_POWER_UP=CON36[9]
     */
    if (Segment == PCIE_SEGMENT_PCIE0) {
      MmioWrite32 (CRU_SOFTRST_CON (34),
                   (1U << (13 + 16)) | (1U << (15 + 16)) |
                   (1U << 13)        | (1U << 15));
    } else {
      MmioWrite32 (CRU_SOFTRST_CON (36),
                   (1U << (7 + 16)) | (1U << (9 + 16)) |
                   (1U << 7)        | (1U << 9));
    }
    return EFI_TIMEOUT;
  }

  /*
   * Phase 2: attempt Gen2 speed upgrade (5 GT/s).
   *
   * Gen1 link is stable at L0.  Unlock DBI RO registers, set the Gen2
   * target speed in LINK_CONTROL2 / LINK_CAPABILITY, then pulse
   * DIRECT_SPEED_CHANGE.  The DWC enters Recovery; if both ComboPHY0
   * and the endpoint support Gen2 the link retrains at 5 GT/s.  If
   * Gen2 fails the PCIe spec mandates hardware fallback to the highest
   * mutually-supported speed (Gen1), so this attempt cannot break a
   * working Gen1 link.
   */
  DEBUG ((DEBUG_WARN, "PCIe%u: Gen1 link stable — attempting Gen2 upgrade...\n", Segment));
  MmioOr32  (DbiBase + PL_MISC_CONTROL_1_OFF, DBI_RO_WR_EN);    /* unlock */
  PciSetupLinkSpeed (DbiBase, 2, 1);
  PciDirectSpeedChange (DbiBase);
  MmioAnd32 (DbiBase + PL_MISC_CONTROL_1_OFF, ~DBI_RO_WR_EN);   /* re-lock */

  /* Wait up to 500 ms for the link to stabilise at Gen2 (or Gen1 fallback). */
  for (Retry = 5; Retry != 0; Retry--) {
    if (PciIsLinkUp (ApbBase)) {
      break;
    }

    gBS->Stall (100000);
  }

  if (Retry == 0) {
    DEBUG ((DEBUG_WARN, "PCIe%u: Gen2 upgrade timed out; link remains at Gen1.\n", Segment));
  }

  PciGetLinkSpeedWidth (DbiBase, &LinkSpeed, &LinkWidth);
  PciPrintLinkSpeedWidth (LinkSpeed, LinkWidth);

  PciValidateCfg0 (Segment, CfgBase + SIZE_1MB);

  return EFI_SUCCESS;
}
