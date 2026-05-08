/** @file
 *
 *  RK3576 DXE ComboPHY hardware initialisation and variable management.
 *
 *  Implements SetupComboPhyVariables() (NVRAM → PCD seeding) and
 *  ApplyComboPhyVariables() (hardware dispatch).  Hardware init code was
 *  originally in RK3576Dxe.c and is consolidated here.
 *
 *  RK3576 combo PHY topology:
 *    combphy0_ps  (0x2B050000)  — PCIe2x1l0 only  (M.2 NVMe slot)
 *    combphy1_psu (0x2B060000)  — DRD1 USB3 or PCIe2x1l1
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <VarStoreData.h>

/*
 * Force RK3576's own Soc.h so the shared __SOC_H__ guard fires first,
 * preventing RK3588/Include/Soc.h from shadowing RK3576 CRU/GRF defines.
 */
#include "../../Include/Soc.h"
#include "RK3576DxeFormSetGuid.h"
#include "ComboPhy.h"

/* PHY MMIO bases */
#ifndef COMBPHY0_BASE
#define COMBPHY0_BASE       0x2B050000UL
#endif
#ifndef PIPE_PHY0_GRF_BASE
#define PIPE_PHY0_GRF_BASE  0x26028000UL
#endif
#ifndef COMBPHY1_BASE
#define COMBPHY1_BASE       0x2B060000UL
#endif
#ifndef PIPE_PHY1_GRF_BASE
#define PIPE_PHY1_GRF_BASE  0x2602A000UL
#endif
#ifndef PHP_GRF_BASE
#define PHP_GRF_BASE        0x26020000UL
#endif

/* -----------------------------------------------------------------------
 * Rk3576CombPhy0InitPcie — Naneng combphy0 in PCIe mode
 * (pcie2x1l0, ROCK 4D M.2 NVMe slot)
 * ----------------------------------------------------------------------- */
STATIC
VOID
Rk3576CombPhy0InitPcie (
  VOID
  )
{
  UINTN  Phy    = COMBPHY0_BASE;
  UINTN  PhyGrf = PIPE_PHY0_GRF_BASE;
  UINT32 Val;

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY0 PCIe init start\n"));

  /* 1. Enable APB / ref clocks */
  MmioWrite32 (CRU_CLKGATE_CON (34),
               (1U << (0 + 16)) | (1U << (13 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (0),
               (1U << (2 + 16)) | (1U << (5 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (1),
               (1U << (1 + 16)) | (1U << (5 + 16)) | 0U);

  /* 2. Select 100 MHz (clk_pcie_100m_src) for CLK_REF_PCIE0_PHY mux */
  MmioWrite32 (PHP_CRU_CLKSEL_CON (0),
               ((UINT32)0x3U << (12 + 16)) | (0U << 12));

  /* 3a. Assert PIPE PHY0 reset (must hold before writing config regs) */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (5 + 16)) | (1U << 5));

  /* 3b. Deassert APB reset; allow APB bus to settle */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (0), (1U << (5 + 16)) | 0U);
  MicroSecondDelay (1 * 1000);

  /* 4. pipe-phy0-grf: PCIe mode configuration */
  MmioWrite32 (PhyGrf + 0x0000U, (0xFFFFU << 16) | 0x1000U);
  MmioWrite32 (PhyGrf + 0x0004U, (0xFFFFU << 16) | 0x0000U);
  MmioWrite32 (PhyGrf + 0x0008U, (0xFFFFU << 16) | 0x0101U);
  MmioWrite32 (PhyGrf + 0x000CU, (0xFFFFU << 16) | 0x0200U);
  /* pipe_clk_100m */
  MmioWrite32 (PhyGrf + 0x0004U, (0x6000U << 16) | 0x4000U);

  /* 5. PHY tuning registers (rk3576_combphy_cfg PCIe + 100 MHz) */
  /* SSC downward spread spectrum: reg[0x7C] bits[5:4] = 01 */
  Val  = MmioRead32 (Phy + 0x7CU);
  Val &= ~0xF0U;
  Val |=  (1U << 4);
  MmioWrite32 (Phy + 0x7CU, Val);

  /* gate_tx_pck_sel for L1SS */
  MmioWrite32 (Phy + 0x74U, 0xC0U);

  /* PLL KVCO = 4 (RK3576 PCIe, not RK3568's 2) */
  Val  = MmioRead32 (Phy + 0x80U);
  Val &= ~(7U << 2);
  Val |=  (4U << 2);
  MmioWrite32 (Phy + 0x80U, Val);

  /* rx_trim: PLL LPF C1=85pF R1=1.25kΩ */
  MmioWrite32 (Phy + 0x6CU, 0x4CU);

  /* su_trim */
  MmioWrite32 (Phy + 0x28U, 0x90U);
  MmioWrite32 (Phy + 0x2CU, 0x43U);
  MmioWrite32 (Phy + 0x30U, 0x88U);
  MmioWrite32 (Phy + 0x34U, 0x56U);

  /* 6. Deassert PHY pipe reset → PLL lock sequence starts */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (5 + 16)) | 0U);

  MicroSecondDelay (10 * 1000);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY0 PCIe init done\n"));
}

/* -----------------------------------------------------------------------
 * Rk3576CombPhy1InitPcie — Naneng combphy1 in PCIe mode
 * (pcie2x1l1; user opted to use USB-A PIPE as a second PCIe lane)
 * ----------------------------------------------------------------------- */
STATIC
VOID
Rk3576CombPhy1InitPcie (
  VOID
  )
{
  UINTN  Phy    = COMBPHY1_BASE;
  UINTN  PhyGrf = PIPE_PHY1_GRF_BASE;
  UINT32 Val;

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 PCIe init start\n"));

  /* 1. Enable APB / ref clocks */
  MmioWrite32 (CRU_CLKGATE_CON (34), (1U << (0 + 16)) | 0U);
  MmioWrite32 (CRU_CLKGATE_CON (36), (1U << (7 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (0),
               (1U << (2 + 16)) | (1U << (7 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (1),
               (1U << (1 + 16)) | (1U << (8 + 16)) | 0U);

  /* 2. Select 100 MHz for CLK_REF_PCIE1_PHY mux (bits[15:14] = 00) */
  MmioWrite32 (PHP_CRU_CLKSEL_CON (0),
               ((UINT32)0x3U << (14 + 16)) | (0U << 14));

  /* 3a. Assert PIPE PHY1 reset */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (8 + 16)) | (1U << 8));

  /* 3b. Deassert APB reset */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (0), (1U << (7 + 16)) | 0U);
  MicroSecondDelay (1 * 1000);

  /* 4. pipe-phy1-grf: PCIe mode (same values as PHY0) */
  MmioWrite32 (PhyGrf + 0x0000U, (0xFFFFU << 16) | 0x1000U);
  MmioWrite32 (PhyGrf + 0x0004U, (0xFFFFU << 16) | 0x0000U);
  MmioWrite32 (PhyGrf + 0x0008U, (0xFFFFU << 16) | 0x0101U);
  MmioWrite32 (PhyGrf + 0x000CU, (0xFFFFU << 16) | 0x0200U);
  MmioWrite32 (PhyGrf + 0x0004U, (0x6000U << 16) | 0x4000U);

  /* 5. PHY tuning (same Naneng IP settings as PHY0 PCIe) */
  Val  = MmioRead32 (Phy + 0x7CU);
  Val &= ~0xF0U;
  Val |=  (1U << 4);
  MmioWrite32 (Phy + 0x7CU, Val);

  MmioWrite32 (Phy + 0x74U, 0xC0U);

  Val  = MmioRead32 (Phy + 0x80U);
  Val &= ~(7U << 2);
  Val |=  (4U << 2);
  MmioWrite32 (Phy + 0x80U, Val);

  MmioWrite32 (Phy + 0x6CU, 0x4CU);
  MmioWrite32 (Phy + 0x28U, 0x90U);
  MmioWrite32 (Phy + 0x2CU, 0x43U);
  MmioWrite32 (Phy + 0x30U, 0x88U);
  MmioWrite32 (Phy + 0x34U, 0x56U);

  /* 6. Deassert PHY1 pipe reset */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (8 + 16)) | 0U);

  MicroSecondDelay (10 * 1000);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 PCIe init done\n"));
}

/* -----------------------------------------------------------------------
 * Rk3576CombPhy1InitUsb3 — Naneng combphy1 in USB3 mode
 * (DRD1 PIPE3 interface, ROCK 4D USB-A Type-A port)
 * ----------------------------------------------------------------------- */
STATIC
VOID
Rk3576CombPhy1InitUsb3 (
  VOID
  )
{
  UINTN  Phy    = COMBPHY1_BASE;
  UINTN  PhyGrf = PIPE_PHY1_GRF_BASE;
  UINTN  PhpGrf = PHP_GRF_BASE;
  UINT32 Val;

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 USB3 init start\n"));

  /* 1. Enable APB / ref clocks */
  MmioWrite32 (CRU_CLKGATE_CON (34), (1U << (0 + 16)) | 0U);
  MmioWrite32 (CRU_CLKGATE_CON (36), (1U << (7 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (0),
               (1U << (2 + 16)) | (1U << (7 + 16)) | 0U);
  MmioWrite32 (PHP_CRU_CLKGATE_CON (1),
               (1U << (1 + 16)) | (1U << (8 + 16)) | 0U);

  /* 2. Select 100 MHz for CLK_REF_PCIE1_PHY mux */
  MmioWrite32 (PHP_CRU_CLKSEL_CON (0),
               ((UINT32)0x3U << (14 + 16)) | (0U << 14));

  /* 3. Deassert APB reset */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (0), (1U << (7 + 16)) | 0U);
  MicroSecondDelay (1 * 1000);

  /* 4. PHY MMIO tuning registers (USB3 mode) */
  /* SSC downward: reg[0x7c] bits[5:4] = 01 */
  Val  = MmioRead32 (Phy + 0x7cU);
  Val &= ~(3U << 4);
  Val |=  (1U << 4);
  MmioWrite32 (Phy + 0x7cU, Val);

  /* Adaptive CTLE for USB3 Rx */
  Val  = MmioRead32 (Phy + 0x38U);
  Val |= 1U;
  MmioWrite32 (Phy + 0x38U, Val);

  /* PLL KVCO = 2 (USB3) */
  Val  = MmioRead32 (Phy + 0x80U);
  Val &= ~(7U << 2);
  Val |=  (2U << 2);
  MmioWrite32 (Phy + 0x80U, Val);

  /* PLL LPF R1 */
  MmioWrite32 (Phy + 0x2cU, 0x04U);

  /* PLL input clock divider 1/2 */
  Val  = MmioRead32 (Phy + 0x14U);
  Val &= ~(3U << 6);
  Val |=  (1U << 6);
  MmioWrite32 (Phy + 0x14U, Val);

  /* PLL loop divider */
  MmioWrite32 (Phy + 0x44U, 0x32U);

  /* KVCO min + charge pump max */
  MmioWrite32 (Phy + 0x28U, 0xf0U);

  /* Rx squelch input filler bandwidth */
  MmioWrite32 (Phy + 0x50U, 0x0dU);

  /* 5. pipe-phy1-grf writes */
  MmioWrite32 (PhyGrf + 0x0008U,
               (1U << (15 + 16)) | (1U << (12 + 16)) | 0U);
  MmioWrite32 (PhyGrf + 0x0000U,
               (0x3FU << 16) | 0x04U);
  MmioWrite32 (PhyGrf + 0x0004U,
               (0x6000U << 16) | 0x4000U);

  /* 6. php-grf: u3otg1_port_en */
  MmioWrite32 (PhpGrf + 0x0038U,
               (0xFFFFU << 16) | 0x1100U);

  /* 7. Deassert PHY1 pipe reset */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (8 + 16)) | 0U);

  MicroSecondDelay (10 * 1000);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 USB3 init done\n"));
}

/* -----------------------------------------------------------------------
 * SetupComboPhyVariables — read NVRAM variables (or use defaults) and
 * seed PcdComboPhy0/1Mode so ApplyComboPhyVariables() sees the right mode.
 * ----------------------------------------------------------------------- */
VOID
EFIAPI
SetupComboPhyVariables (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT32      Var32;

  //
  // PHY0
  //
  Size   = sizeof (UINT32);
  Status = gRT->GetVariable (
                  L"ComboPhy0Mode",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var32
                  );
  if (EFI_ERROR (Status) || !FixedPcdGetBool (PcdComboPhy0Switchable)) {
    Status = PcdSet32S (PcdComboPhy0Mode, FixedPcdGet32 (PcdComboPhy0ModeDefault));
    ASSERT_EFI_ERROR (Status);
  } else {
    Status = PcdSet32S (PcdComboPhy0Mode, Var32);
    ASSERT_EFI_ERROR (Status);
  }

  //
  // PHY1
  //
  Size   = sizeof (UINT32);
  Status = gRT->GetVariable (
                  L"ComboPhy1Mode",
                  &gRK3576DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Var32
                  );
  if (EFI_ERROR (Status) || !FixedPcdGetBool (PcdComboPhy1Switchable)) {
    Status = PcdSet32S (PcdComboPhy1Mode, FixedPcdGet32 (PcdComboPhy1ModeDefault));
    ASSERT_EFI_ERROR (Status);
  } else {
    Status = PcdSet32S (PcdComboPhy1Mode, Var32);
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "RK3576Dxe: ComboPHY0 mode=%u, PHY1 mode=%u (Switchable: PHY0=%u PHY1=%u)\n",
          PcdGet32 (PcdComboPhy0Mode), PcdGet32 (PcdComboPhy1Mode),
          FixedPcdGetBool (PcdComboPhy0Switchable),
          FixedPcdGetBool (PcdComboPhy1Switchable)));
}

/* -----------------------------------------------------------------------
 * ApplyComboPhyVariables — dispatch to hardware init based on PCD modes.
 * ----------------------------------------------------------------------- */
VOID
EFIAPI
ApplyComboPhyVariables (
  VOID
  )
{
  UINT32  Phy0Mode = PcdGet32 (PcdComboPhy0Mode);
  UINT32  Phy1Mode = PcdGet32 (PcdComboPhy1Mode);

  //
  // ComboPHY0: only PCIe-capable on RK3576 (no USB3/SATA wiring)
  //
  if (Phy0Mode == COMBO_PHY_MODE_PCIE) {
    Rk3576CombPhy0InitPcie ();

    //
    // Pre-ungate PCIe0 AXI clocks for PciHostBridgeDxe DBI access.
    //   ACLK_PHP_ROOT   CRU_CLKGATE_CON(34) bit  7
    //   CLK_PCIE0_AUX   CRU_CLKGATE_CON(34) bit 14
    //   ACLK_PCIE0_MST  CRU_CLKGATE_CON(34) bit 15
    //   ACLK_PCIE0_SLV  CRU_CLKGATE_CON(35) bit  0
    //   ACLK_PCIE0_DBI  CRU_CLKGATE_CON(35) bit  1
    //
    MmioWrite32 (
      CRU_CLKGATE_CON (34),
      (1U << (7 + 16)) | (1U << (14 + 16)) | (1U << (15 + 16))
      );
    MmioWrite32 (
      CRU_CLKGATE_CON (35),
      (1U << (0 + 16)) | (1U << (1 + 16))
      );

    DEBUG ((DEBUG_INFO,
      "RK3576Dxe: PCIe0 APB ok — LTSSM_STATUS=0x%08X"
      " (link training deferred to PciHostBridgeDxe)\n",
      MmioRead32 (0x2A200000U + 0x0300U)
      ));
  }

  //
  // ComboPHY1: USB3 (DRD1 USB-A) or PCIe (pcie2x1l1)
  //
  switch (Phy1Mode) {
    case COMBO_PHY_MODE_USB3:
      Rk3576CombPhy1InitUsb3 ();
      break;
    case COMBO_PHY_MODE_PCIE:
      Rk3576CombPhy1InitPcie ();
      break;
    default:
      DEBUG ((DEBUG_INFO, "RK3576Dxe: ComboPHY1 mode %u — skipping init\n", Phy1Mode));
      break;
  }
}
