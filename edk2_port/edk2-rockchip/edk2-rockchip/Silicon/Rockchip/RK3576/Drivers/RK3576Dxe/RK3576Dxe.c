/** @file
 *
 *  RK3576 SoC DXE init driver — Radxa ROCK 4D
 *
 *  Initializes: ComboPHY, LED, early platform config
 *  More sub-modules (CPU perf menu, display) to be added in future iterations.
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/GpioLib.h>
#include <VarStoreData.h>
/*
 * Force RK3576's own Soc.h via relative path so the shared __SOC_H__ guard
 * fires here first — preventing RK3588/Include/Soc.h (which appears earlier
 * on the compiler search path due to RK3588.dec in [Packages]) from shadowing
 * the RK3576-specific register definitions.
 */
#include "../../Include/Soc.h"

STATIC EFI_HANDLE  mRK3576DxeHandle = NULL;

/*
 * Seed PcdDisplayConnectorsMask from the connector list in
 * PcdDisplayConnectors (set in ROCK4D.dsc).
 *
 * On RK3588 boards this is done by RK3588Dxe/Display.c with the full
 * variable-store / mode-preset machinery.  RK3576Dxe is a slimmed-down
 * port; we only need the bare mask seeding so that DwHdmiQpLib,
 * DwDpLib and DwMipiDsi2Lib can see "HDMI0 enabled" and register the
 * controller.  Without this the mask stays 0 → no display output.
 */
STATIC
VOID
RK3576InitDisplay (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      *Connectors;
  UINTN       ConnectorsCount;
  UINT32      ConnectorsMask;
  UINTN       Index;

  Connectors      = PcdGetPtr (PcdDisplayConnectors);
  ConnectorsCount = PcdGetSize (PcdDisplayConnectors) / sizeof (*Connectors);

  ConnectorsMask = 0;
  for (Index = 0; Index < ConnectorsCount; Index++) {
    ConnectorsMask |= Connectors[Index];
  }

  Status = PcdSet32S (PcdDisplayConnectorsMask, ConnectorsMask);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: PcdSet32S(ConnectorsMask) failed: %r\n", Status));
    return;
  }

  DEBUG ((DEBUG_INFO,
    "RK3576Dxe: Display connectors=%u, mask=0x%08x\n",
    (UINT32)ConnectorsCount, ConnectorsMask));
}

/*
 * Rk3576CombPhy1InitUsb3 — initialise Naneng combphy1 (0x2b060000) for USB3 host.
 *
 * This drives the PIPE3 interface of DWC3 DRD1 (USB-A port).
 * Register values from U-Boot mainline:
 *   drivers/phy/rockchip/phy-rockchip-naneng-combphy.c  rk3576_combphy_cfg() USB3 branch
 *   drivers/clk/rockchip/clk-rk3576.c                  gate/reset register offsets
 *
 * Clock gates (bit=0 = running, bit=1 = gated; bits[31:16] = write-enable mask):
 *   PCLK_PHP_ROOT        CRU_CLKGATE_CON(34) bit 0  (@0x27200888)
 *   PCLK_PCIE1           CRU_CLKGATE_CON(36) bit 7  (@0x27200890)
 *   PCLK_PHPPHY_ROOT     PHP_CRU_CLKGATE_CON(0) bit 2  (@0x27208800)  CLK_IS_CRITICAL
 *   PCLK_PCIE2_COMBOPHY1 PHP_CRU_CLKGATE_CON(0) bit 7  (@0x27208800)
 *   CLK_PCIE_100M_SRC    PHP_CRU_CLKGATE_CON(1) bit 1  (@0x27208804)
 *   CLK_REF_PCIE1_PHY    PHP_CRU_CLKGATE_CON(1) bit 8  (@0x27208804)  mux@CLKSEL_CON(0)[15:14]
 *
 * Soft resets (bit=1 = held-in-reset; deassert = write 0 with mask):
 *   SRST_P_PCIE2_COMBOPHY1  PHP_CRU_SOFTRST_CON(0) bit 7 (@0x27208A00)
 *   SRST_PCIE1_PIPE_PHY     PHP_CRU_SOFTRST_CON(1) bit 8 (@0x27208A04)
 *
 * GRFs:
 *   PIPE_PHY1_GRF_BASE  0x2602A000  (pipe_phy1_grf — mode/clk select)
 *   PHP_GRF_BASE        0x26020000  (php_grf — u3otg port enable)
 *
 * NOTE: These defines live in Silicon/Rockchip/RK3576/Include/Soc.h
 * but RK3588/Include/Soc.h shadows it when RK3588.dec is in [Packages].
 * Declare them locally with guards so compilation always succeeds.
 */
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

/*
 * Rk3576CombPhy0InitPcie — initialise Naneng combphy0 (0x2b050000) for PCIe.
 *
 * combphy0 is wired exclusively to the PCIe2x1l0 controller on RK3576
 * (no USB capability — no u3otg0_port_en in rk3576_combphy_grfcfgs).
 * This clocks and configures the PHY so the PCIe controller can use it
 * once a host bridge driver is present.
 *
 * Clock gates (bit=0 = running, bits[31:16] = write-enable mask):
 *   PCLK_PHP_ROOT        CRU_CLKGATE_CON(34) bit 0
 *   PCLK_PCIE0           CRU_CLKGATE_CON(34) bit 13
 *   PCLK_PHPPHY_ROOT     PHP_CRU_CLKGATE_CON(0) bit 2
 *   PCLK_PCIE2_COMBOPHY0 PHP_CRU_CLKGATE_CON(0) bit 5
 *   CLK_PCIE_100M_SRC    PHP_CRU_CLKGATE_CON(1) bit 1
 *   CLK_REF_PCIE0_PHY    PHP_CRU_CLKGATE_CON(1) bit 5   mux@CLKSEL_CON(0)[13:12]
 *
 * Soft resets (deassert = write 0 with mask):
 *   SRST_P_PCIE2_COMBOPHY0  PHP_CRU_SOFTRST_CON(0) bit 5
 *   SRST_PCIE0_PIPE_PHY     PHP_CRU_SOFTRST_CON(1) bit 5
 *
 * GRF: PIPE_PHY0_GRF_BASE = 0x26028000
 * PHY register values from rk3576_combphy_cfg() PCIe branch in U-Boot mainline.
 */
STATIC
VOID
Rk3576CombPhy0InitPcie (
  VOID
  )
{
  UINTN  Phy    = COMBPHY0_BASE;       /* 0x2B050000 */
  UINTN  PhyGrf = PIPE_PHY0_GRF_BASE; /* 0x26028000 */
  UINT32 Val;

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY0 PCIe init start\n"));

  /*
   * 1. Enable APB / ref clocks.
   */
  /* PCLK_PHP_ROOT bit0 + PCLK_PCIE0 bit13 */
  MmioWrite32 (CRU_CLKGATE_CON (34),
               (1U << (0 + 16)) | (1U << (13 + 16)) | 0U);
  /* PCLK_PHPPHY_ROOT bit2 + PCLK_PCIE2_COMBOPHY0 bit5 */
  MmioWrite32 (PHP_CRU_CLKGATE_CON (0),
               (1U << (2 + 16)) | (1U << (5 + 16)) | 0U);
  /* CLK_PCIE_100M_SRC bit1 + CLK_REF_PCIE0_PHY bit5 */
  MmioWrite32 (PHP_CRU_CLKGATE_CON (1),
               (1U << (1 + 16)) | (1U << (5 + 16)) | 0U);

  /*
   * 2. Select clk_pcie_100m_src (100 MHz) for CLK_REF_PCIE0_PHY mux.
   *    PHP_CRU_CLKSEL_CON(0) bits[13:12] = 0b00.
   */
  MmioWrite32 (PHP_CRU_CLKSEL_CON (0),
               ((UINT32)0x3U << (12 + 16)) | (0U << 12));

  /*
   * 3a. Assert PIPE PHY reset BEFORE writing any configuration registers.
   *
   *     After POR, PHP_CRU_SOFTRST_CON(1) bit5 defaults to 0 (deasserted) —
   *     the PHY core and PLL are already running in an undefined default state.
   *     Writing PLL tuning registers (KVCO, rx_trim, su_trim) to a live PHY
   *     does NOT reliably take effect: the analog PLL ignores new register
   *     values while it is already locked.
   *
   *     Linux kernel asserts SRST_PCIE0_PIPE_PHY in rockchip_combphy_probe()
   *     before any configuration writes, then deasserts in combphy_init() after
   *     all writes are complete.  Without this explicit assert, the deassert
   *     in step 6 is a no-op (bit was already 0) and the PHY PLL is never
   *     properly reset+restarted with the new configuration.
   *
   *     PHP_CRU_SOFTRST_CON(1) bit 5 = 1 (SRST_PCIE0_PIPE_PHY = reset).
   */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (5 + 16)) | (1U << 5));

  /*
   * 3b. Deassert APB reset so PHY MMIO registers become accessible.
   *     PHP_CRU_SOFTRST_CON(0) bit 5 = 0.
   *     Wait 1 ms for the APB bus to settle before writing MMIO registers.
   */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (0), (1U << (5 + 16)) | 0U);
  MicroSecondDelay (1 * 1000);

  /*
   * 4. Program pipe-phy0-grf: PCIe mode configuration words.
   *    Rockchip write-enable register format: bits[31:16]=mask, bits[15:0]=val.
   */
  MmioWrite32 (PhyGrf + 0x0000U, (0xFFFFU << 16) | 0x1000U); /* con0_for_pcie */
  MmioWrite32 (PhyGrf + 0x0004U, (0xFFFFU << 16) | 0x0000U); /* con1_for_pcie */
  MmioWrite32 (PhyGrf + 0x0008U, (0xFFFFU << 16) | 0x0101U); /* con2_for_pcie */
  MmioWrite32 (PhyGrf + 0x000CU, (0xFFFFU << 16) | 0x0200U); /* con3_for_pcie */
  /* pipe_clk_100m {0x0004, 14,13, enable=0x02}: mask=0x6000, val=2<<13=0x4000 */
  MmioWrite32 (PhyGrf + 0x0004U, (0x6000U << 16) | 0x4000U);

  /*
   * 5. PCIe MMIO PHY tuning registers.
   *    Source: rk3576_combphy_cfg() PHY_TYPE_PCIE + REF_CLOCK_100MHz in
   *    drivers/phy/rockchip/phy-rockchip-naneng-combphy.c (Linux mainline).
   *
   *    Two bugs compared to Linux mainline that were preventing link training:
   *      (a) KVCO value: RK3576 PCIe requires KVCO=4 (bits[4:2] of reg 0x80),
   *          not KVCO=2 which is the RK3568 value.  Wrong KVCO causes PLL
   *          mis-tuning → TX signal quality too poor for the endpoint to lock.
   *      (b) SSC_DOWNWARD not written: Linux sets spread-spectrum direction to
   *          "downward" (bits[5:4]=01) in reg 0x7C before GRF con writes.
   *          Without this the SSC block defaults to upward/off which can cause
   *          clock recovery issues on some NVMe drives.
   */

  /* Set SSC downward spread spectrum: reg[0x7C] bits[7:4], clear then set 0x10 */
  Val = MmioRead32 (Phy + 0x7CU);
  Val &= ~0xF0U;
  Val |=  (1U << 4);                                        /* SSC_DOWNWARD: bits[5:4]=01 */
  MmioWrite32 (Phy + 0x7CU, Val);

  /* gate_tx_pck_sel length select for L1SS: reg[0x74] = 0xC0 */
  MmioWrite32 (Phy + 0x74U, 0xC0U);

  /* PLL KVCO tuning: reg[0x80] bits[4:2] = 4 (RK3576 PCIe value, not 2) */
  Val = MmioRead32 (Phy + 0x80U);
  Val &= ~(7U << 2);
  Val |=  (4U << 2);                                        /* KVCO=4 for RK3576 PCIe */
  MmioWrite32 (Phy + 0x80U, Val);

  /* rx_trim: PLL LPF C1=85pf R1=1.25kohm */
  MmioWrite32 (Phy + 0x6CU, 0x4CU);

  /* SU adjust signal (su_trim[31:0]): KVCO_min | LPF_R1_adj | CKRCV_adj | CKDRV_adj */
  MmioWrite32 (Phy + 0x28U, 0x90U);                        /* su_trim[7:0]   */
  MmioWrite32 (Phy + 0x2CU, 0x43U);                        /* su_trim[15:8]  */
  MmioWrite32 (Phy + 0x30U, 0x88U);                        /* su_trim[23:16] */
  MmioWrite32 (Phy + 0x34U, 0x56U);                        /* su_trim[31:24] */

  /*
   * 6. Deassert PHY pipe reset — triggers PLL lock sequence.
   *
   *    The PHY was explicitly put into reset in step 3a.  All configuration
   *    registers (KVCO, SSC, rx_trim, su_trim) are now latched.  Deassert
   *    brings the PHY out of reset; the PLL starts locking to the 100 MHz
   *    reference clock with the new tuning parameters.
   *
   *    PHP_CRU_SOFTRST_CON(1) bit 5 = 0 (SRST_PCIE0_PIPE_PHY = running).
   */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (5 + 16)) | 0U);

  /* 7. Wait ~10 ms for PLL lock. */
  MicroSecondDelay (10 * 1000);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY0 PCIe init done\n"));
}

STATIC
VOID
Rk3576CombPhy1InitUsb3 (
  VOID
  )
{
  UINTN  Phy    = COMBPHY1_BASE;      /* 0x2B060000 */
  UINTN  PhyGrf = PIPE_PHY1_GRF_BASE; /* 0x2602A000 */
  UINTN  PhpGrf = PHP_GRF_BASE;       /* 0x26020000 */
  UINT32 Val;

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 USB3 init start\n"));

  /*
   * 1. Enable APB / ref clocks.
   *    Write-enable bit (N+16) must be set; gate value 0 = running.
   */
  /* PCLK_PHP_ROOT: CRU_CLKGATE_CON(34) bit 0 */
  MmioWrite32 (CRU_CLKGATE_CON (34), (1U << (0 + 16)) | 0U);
  /* PCLK_PCIE1: CRU_CLKGATE_CON(36) bit 7 */
  MmioWrite32 (CRU_CLKGATE_CON (36), (1U << (7 + 16)) | 0U);
  /* PCLK_PHPPHY_ROOT bit2 + PCLK_PCIE2_COMBOPHY1 bit7: PHP_CRU_CLKGATE_CON(0) */
  MmioWrite32 (PHP_CRU_CLKGATE_CON (0),
               (1U << (2 + 16)) | (1U << (7 + 16)) | 0U);
  /* CLK_PCIE_100M_SRC bit1 + CLK_REF_PCIE1_PHY bit8: PHP_CRU_CLKGATE_CON(1) */
  MmioWrite32 (PHP_CRU_CLKGATE_CON (1),
               (1U << (1 + 16)) | (1U << (8 + 16)) | 0U);

  /*
   * 2. Select clk_pcie_100m_src (100 MHz from PPLL) for CLK_REF_PCIE1_PHY mux.
   *    PHP_CRU_CLKSEL_CON(0) bits[15:14] = 0b00.
   */
  MmioWrite32 (PHP_CRU_CLKSEL_CON (0),
               ((UINT32)0x3U << (14 + 16)) | (0U << 14));

  /*
   * 3. Deassert APB reset so PHY MMIO registers become accessible.
   *    PHP_CRU_SOFTRST_CON(0) bit 7 = 0.
   */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (0), (1U << (7 + 16)) | 0U);
  MicroSecondDelay (1 * 1000);

  /*
   * 4. Program PHY MMIO tuning registers (USB3 mode).
   *    Source: rk3576_combphy_cfg() PHY_TYPE_USB3 branch in U-Boot.
   */
  /* Set SSC downward spread spectrum: reg[0x1f] bits[5:4] = 0b01 */
  Val = MmioRead32 (Phy + 0x7cU);
  Val &= ~(3U << 4);
  Val |=  (1U << 4);
  MmioWrite32 (Phy + 0x7cU, Val);

  /* Enable adaptive CTLE for USB3.0 Rx: reg[0x0e] bit0 = 1 */
  Val = MmioRead32 (Phy + 0x38U);
  Val |= 1U;
  MmioWrite32 (Phy + 0x38U, Val);

  /* Set PLL KVCO fine tuning: reg[0x20] bits[4:2] = 0b010 */
  Val = MmioRead32 (Phy + 0x80U);
  Val &= ~(7U << 2);
  Val |=  (2U << 2);
  MmioWrite32 (Phy + 0x80U, Val);

  /* Set PLL LPF R1 (su_trim[10:7]=1001): reg[0x0b] = 0x04 */
  MmioWrite32 (Phy + 0x2cU, 0x04U);

  /* Set PLL input clock divider 1/2: reg[0x05] bits[7:6] = 0b01 */
  Val = MmioRead32 (Phy + 0x14U);
  Val &= ~(3U << 6);
  Val |=  (1U << 6);
  MmioWrite32 (Phy + 0x14U, Val);

  /* Set PLL loop divider: reg[0x11] = 0x32 */
  MmioWrite32 (Phy + 0x44U, 0x32U);

  /* Set PLL KVCO=min + charge pump=max: reg[0x0a] = 0xf0 */
  MmioWrite32 (Phy + 0x28U, 0xf0U);

  /* Set Rx squelch input filler bandwidth: reg[0x14] = 0x0d */
  MmioWrite32 (Phy + 0x50U, 0x0dU);

  /*
   * 5. pipe-phy1-grf writes (Rockchip write-enable: bits[31:16]=mask, bits[15:0]=val).
   *
   *    pipe_txcomp_sel {0x0008, 15,15, disable=0x00, enable=0x01} — write disable:
   *      mask=(1<<15)=0x8000, val=0  →  0x80000000
   *    pipe_txelec_sel {0x0008, 12,12, disable=0x00, enable=0x01} — write disable:
   *      mask=(1<<12)=0x1000, val=0  →  combined with above: 0x90000000
   */
  MmioWrite32 (PhyGrf + 0x0008U,
               (1U << (15 + 16)) | (1U << (12 + 16)) | 0U);   /* txcomp+txelec sel=0 */

  /*
   *    usb_mode_set {0x0000, 5,0, disable=0x00, enable=0x04} — write enable:
   *      mask=0x3F<<16, val=0x04  →  0x003F0004
   */
  MmioWrite32 (PhyGrf + 0x0000U,
               (0x3FU << 16) | 0x04U);                         /* usb_mode_set=4 */

  /*
   *    pipe_clk_100m {0x0004, 14,13, disable=0x00, enable=0x02} — write enable:
   *      mask=GENMASK(14,13)=0x6000, val=2<<13=0x4000  →  0x60004000
   */
  MmioWrite32 (PhyGrf + 0x0004U,
               (0x6000U << 16) | 0x4000U);                     /* pipe_clk=100M */

  /*
   * 6. php-grf: u3otg1_port_en {0x0038, 15,0, disable=0x0181, enable=0x1100}
   *      mask=0xFFFF<<16, val=0x1100  →  0xFFFF1100
   */
  MmioWrite32 (PhpGrf + 0x0038U,
               (0xFFFFU << 16) | 0x1100U);                     /* u3otg1_port_en */

  /*
   * 7. Deassert PHY pipe reset — starts PLL lock sequence.
   *    PHP_CRU_SOFTRST_CON(1) bit 8 = 0.
   */
  MmioWrite32 (PHP_CRU_SOFTRST_CON (1), (1U << (8 + 16)) | 0U);

  /* 8. Wait ~10 ms for PLL lock (typical lock time < 1 ms, 10 ms is ample). */
  MicroSecondDelay (10 * 1000);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: CombPHY1 USB3 init done\n"));
}

STATIC
VOID
RK3576InitComboPhy (
  VOID
  )
{
  UINT32 Phy0Mode = PcdGet32 (PcdComboPhy0ModeDefault);
  UINT32 Phy1Mode = PcdGet32 (PcdComboPhy1ModeDefault);

  DEBUG ((DEBUG_INFO, "RK3576Dxe: ComboPHY0 mode=%u, PHY1 mode=%u\n",
          Phy0Mode, Phy1Mode));

  /*
   * ComboPHY0 in PCIe mode (pcie2x1l0 lane 0).
   *   Phy0Mode 1 = PHY_TYPE_PCIE (per dt-bindings/phy/phy.h).
   *   Clocks and configures the Naneng combo PHY so that the PCIe
   *   host bridge controller can use it once a host bridge driver lands.
   */
  if (Phy0Mode == 1U) {
    Rk3576CombPhy0InitPcie ();

    /*
     * Pre-ungate the PCIe0 AXI clocks here (belt+suspenders over
     * PciSetupClocks() in PciHostBridgeLib) so that every DBI register
     * access in PciHostBridgeDxe is guaranteed safe from the very first
     * MmioRead/Write.  These are idempotent hiword-mask writes.
     *
     *  ACLK_PHP_ROOT   CRU_CLKGATE_CON(34) bit  7
     *  CLK_PCIE0_AUX   CRU_CLKGATE_CON(34) bit 14
     *  ACLK_PCIE0_MST  CRU_CLKGATE_CON(34) bit 15
     *  ACLK_PCIE0_SLV  CRU_CLKGATE_CON(35) bit  0
     *  ACLK_PCIE0_DBI  CRU_CLKGATE_CON(35) bit  1
     */
    MmioWrite32 (
      CRU_CLKGATE_CON (34),
      (1U << (7 + 16)) | (1U << (14 + 16)) | (1U << (15 + 16))
      );
    MmioWrite32 (
      CRU_CLKGATE_CON (35),
      (1U << (0 + 16)) | (1U << (1 + 16))
      );

    /*
     * Diagnostic read of the PCIe0 APB client status register.
     * PCLK_PCIE0 was ungated above; if this read returns data (not a
     * synchronous abort) the APB clock is confirmed running.
     * LTSSM has not been started yet — link training is deferred to
     * PciHostBridgeDxe, which has its own bounded 1-second wait loop.
     *
     * PCIE_APB_BASE(0) = 0x2A200000
     * PCIE_CLIENT_LTSSM_STATUS offset = 0x0300
     */
    DEBUG ((DEBUG_INFO,
      "RK3576Dxe: PCIe0 APB ok — LTSSM_STATUS=0x%08X"
      " (link training deferred to PciHostBridgeDxe)\n",
      MmioRead32 (0x2A200000U + 0x0300U)
      ));
  }

  /*
   * ComboPHY1 in USB3 mode (DRD1 USB-A PIPE3 interface).
   *   Phy1Mode 3 = PHY_TYPE_USB3 (per dt-bindings/phy/phy.h).
   *   Always init for USB-A regardless of PCD; the DWC3 PP=0 workaround
   *   for the absent ref-clock era is no longer needed after this init.
   */
  if (Phy1Mode == 3U) {
    Rk3576CombPhy1InitUsb3 ();
  }
}

STATIC
VOID
RK3576InitStatusLed (
  VOID
  )
{
  UINT8   LedGpio    = PcdGet8  (PcdStatusLedGpio);
  BOOLEAN LedPolarity = PcdGetBool (PcdStatusLedGpioPolarity);

  if (LedGpio == 0xFF) {
    return;  /* No status LED configured */
  }

  /* Power LED on (active high = TRUE means ON) */
  PlatformSetStatusLed (LedPolarity);
  DEBUG ((DEBUG_INFO, "RK3576Dxe: Status LED GPIO%u set to %u\n",
          LedGpio, LedPolarity));
}

EFI_STATUS
EFIAPI
RK3576EntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "\n"));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
  DEBUG ((DEBUG_INFO, "  ROCK 4D / RK3576 SoC DXE Init\n"));
  DEBUG ((DEBUG_INFO, "  GRF bases (remapped):\n"));
  DEBUG ((DEBUG_INFO, "    SYS_GRF (IOC)  = 0x26040000  IOC_HDMI_HPD_STATUS@0xA440 = 0x%08x\n",
          MmioRead32 (0x26040000 + 0xA440)));
  DEBUG ((DEBUG_INFO, "    VO1_GRF (VO0)  = 0x2601A000  VO0_GRF_SOC_CON14@0x0038 = 0x%08x\n",
          MmioRead32 (0x2601A000 + 0x0038)));
  DEBUG ((DEBUG_INFO, "    HDPTXPHY GRF   = 0x26032000  GRF_HDPTX_STATUS@0x80    = 0x%08x\n",
          MmioRead32 (0x26032000 + 0x80)));
  DEBUG ((DEBUG_INFO, "    PMU1CRU        = 0x27220000  SOFTRST_CON01@0xA04      = 0x%08x\n",
          MmioRead32 (0x27220000 + 0xA04)));
  DEBUG ((DEBUG_INFO, "    BUSCRU         = 0x27200000\n"));
  DEBUG ((DEBUG_INFO, "    VOP2 base      = 0x27D00000  REG_CFG_DONE             = 0x%08x\n",
          MmioRead32 (0x27D00000 + 0x0)));
  DEBUG ((DEBUG_INFO, "    VOP2 HDMI0_IF_CTRL@0x184                              = 0x%08x\n",
          MmioRead32 (0x27D00000 + 0x184)));
  DEBUG ((DEBUG_INFO, "================================================================\n"));
  DEBUG ((DEBUG_INFO, "RK3576Dxe: Entry (ROCK 4D, RK3576)\n"));

  /* Early board platform init (WiFi power, PCIe power) */
  PlatformEarlyInit ();

  /* Seed PcdDisplayConnectorsMask so HDMI/VOP2 drivers register HDMI0 */
  RK3576InitDisplay ();

  /* Initialize ComboPHY mode */
  RK3576InitComboPhy ();

  /* Initialize status LED */
  RK3576InitStatusLed ();

  /* Install protocol to signal platform config is applied */
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mRK3576DxeHandle,
                  &gRockchipPlatformConfigAppliedProtocolGuid, NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: Failed to install protocol: %r\n", Status));
  }

  DEBUG ((DEBUG_INFO, "RK3576Dxe: Init complete\n"));
  return EFI_SUCCESS;
}
