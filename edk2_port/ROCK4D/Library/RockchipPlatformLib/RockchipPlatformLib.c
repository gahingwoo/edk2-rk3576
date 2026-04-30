/** @file
 *
 *  RockchipPlatformLib.c — Board-specific init for Radxa ROCK 4D (RK3576)
 *
 *  Ported from ROCK 5B (RK3588) RockchipPlatformLib.c.
 *  Key differences from RK3588/ROCK5B:
 *    - SoC: RK3576 (4×A72 + 4×A53, not 4×A76+4×A55)
 *    - PMIC: RK806 @ i2c1 addr 0x23 (same chip, different voltage rails)
 *    - RTC: HYM8563 @ i2c2 addr 0x51
 *    - GPIO: Different pin assignments (from rk3576-rock-4d.dts)
 *    - PCIe: pcie0 @ 0x22000000 (combphy0_ps), reset GPIO2 PB4
 *    - USB host power: GPIO0 PD3
 *    - LEDs: green=GPIO0 PB4 (active high), blue=GPIO0 PC4 (active low)
 *    - IOC mux registers differ (rk3576 vs rk3588 BUS_IOC base)
 *
 *  All GPIO pins verified from:
 *    u-boot/dts/upstream/src/arm64/rockchip/rk3576-rock-4d.dts
 *
 *  Copyright (c) 2021, Rockchip Limited.
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/GpioLib.h>
#include <Library/RK806.h>
#include <Library/PWMLib.h>
#include <Soc.h>
#include <VarStoreData.h>

/*
 * RK806 PMIC voltage init for ROCK 4D (RK3576)
 *
 * Rail mapping from rk3576-rock-4d.dts regulators section:
 *   DCDC1  → vdd_cpu_big_s0  (A72 cluster, 0.55V-0.95V)
 *   DCDC2  → vdd_npu_s0      (NPU, 0.55V-0.95V)
 *   DCDC3  → vdd_cpu_lit_s0  (A53 cluster, 0.55V-0.95V)
 *   DCDC4  → vcc_3v3_s3      (3.3V always-on)
 *   DCDC5  → vdd_gpu_s0      (GPU, 0.55V-0.90V)
 *   DCDC6  → vddq_ddr_s0     (DDR I/O)
 *   DCDC7  → vdd_logic_s0    (Logic, 0.55V-0.80V)
 *   DCDC8  → vcc_1v8_s3      (1.8V always-on)
 *   DCDC9  → vdd2_ddr_s3     (DDR Vdd2)
 *   DCDC10 → vdd_ddr_s0      (DDR main, 0.55V-1.2V)
 *   PLDO1  → vcca_1v8_s0     (1.8V analog)
 *   PLDO2  → vcca1v8_pldo2_s0 (1.8V)
 *   PLDO3  → vdda_1v2_s0     (1.2V)
 *   PLDO4  → vcca_3v3_s0     (3.3V analog)
 *   PLDO5  → vccio_sd_s0     (SD I/O, 1.8V/3.3V switchable)
 *   PLDO6  → vcca1v8_pldo6_s3 (1.8V always-on)
 *   NLDO1  → vdd_0v75_s3     (0.75V always-on)
 *   NLDO2  → vdda_ddr_pll_s0 (0.85V)
 *   NLDO3  → vdda0v75_hdmi_s0 (0.8375V)
 *   NLDO4  → vdda_0v85_s0    (0.85V)
 *   NLDO5  → vdda_0v75_s0    (0.75V)
 */
static struct regulator_init_data rk806_init_data[] = {
  /* DCDC rails */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK1,   750000),   /* vdd_cpu_big_s0 initial */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK3,   750000),   /* vdd_cpu_lit_s0 initial */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK4,  3300000),   /* vcc_3v3_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK7,   750000),   /* vdd_logic_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK8,  1800000),   /* vcc_1v8_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK10, 1800000),   /* vcc_1v8_s0 (from pldo path) */

  /* PLDO rails */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO1,  1800000),   /* vcca_1v8_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO2,  1800000),   /* vcca1v8_pldo2_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO3,  1200000),   /* vdda_1v2_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO4,  3300000),   /* vcca_3v3_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO6,  1800000),   /* vcca1v8_pldo6_s3 */

  /* NLDO rails */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO1,   750000),   /* vdd_0v75_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO2,   850000),   /* vdda_ddr_pll_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO3,   837500),   /* vdda0v75_hdmi_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO4,   850000),   /* vdda_0v85_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO5,   750000),   /* vdda_0v75_s0 */
};

/*
 * SdmmcIoMux — SD card iomux (sdmmc: mmc@2a310000)
 *
 * RK3576 SD card pins differ from RK3588. The BUS_IOC base for RK3576
 * is at sys_grf area. Exact iomux register offsets must be confirmed
 * from RK3576 TRM chapter "IO MUX".
 *
 * TODO: Replace placeholder with real RK3576 BUS_IOC register writes
 *       once TRM section for GPIO4 iomux is available.
 *       Reference: rk3576.dtsi sdmmc pinctrl node.
 */
VOID
EFIAPI
SdmmcIoMux (
  VOID
  )
{
  /*
   * SDMMC (mmc@2a310000) clock enable — from clk-rk3576.c:
   *
   * Parent clock (SDGMAC domain):
   *   HCLK_SDGMAC_ROOT → CLKGATE_CON(42), bit 0
   *
   * SDMMC clocks:
   *   CCLK_SRC_SDMMC0  → CLKGATE_CON(43), bit 1  (card clock)
   *   HCLK_SDMMC0      → CLKGATE_CON(43), bit 2  (AHB bus clock)
   *
   * Pinmux: SPL already configures sdmmc0 pins, retained into UEFI.
   */

  /* Ungate SDGMAC root clock */
  MmioWrite32 (CRU_CLKGATE_CON(42),
    (1U << (0 + 16)) | (0U << 0));   /* HCLK_SDGMAC_ROOT: ungate */

  /* Ungate SDMMC0 clocks */
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (1 + 16)) | (0U << 1));   /* CCLK_SRC_SDMMC0: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (2 + 16)) | (0U << 2));   /* HCLK_SDMMC0: ungate */

  DEBUG ((DEBUG_INFO, "SdmmcIoMux: SDMMC0 clocks ungated, pinmux from SPL\n"));
}

/*
 * SdhciEmmcIoMux — eMMC iomux (sdhci: mmc@2a330000)
 * Same note as SdmmcIoMux — placeholder until TRM addresses confirmed.
 */
VOID
EFIAPI
SdhciEmmcIoMux (
  VOID
  )
{
  /*
   * eMMC (sdhci: mmc@2a330000) clock enable — from clk-rk3576.c:
   *
   * Parent clocks (NVM domain, shared with SFC):
   *   HCLK_NVM_ROOT  → CLKGATE_CON(33), bit 0  (may already be ungated by SFC)
   *   ACLK_NVM_ROOT  → CLKGATE_CON(33), bit 1
   *
   * eMMC clocks:
   *   CCLK_SRC_EMMC  → CLKGATE_CON(33), bit 8   (card clock)
   *   HCLK_EMMC      → CLKGATE_CON(33), bit 9   (AHB bus clock)
   *   ACLK_EMMC      → CLKGATE_CON(33), bit 10  (AXI clock)
   *   BCLK_EMMC      → CLKGATE_CON(33), bit 11  (block clock)
   *   TCLK_EMMC      → CLKGATE_CON(33), bit 12  (timer clock)
   *
   * Pinmux: SPL already configures emmc pins, retained into UEFI.
   */

  /* Ungate NVM root clocks (idempotent if already done by SFC) */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (0 + 16)) | (0U << 0));   /* HCLK_NVM_ROOT: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (1 + 16)) | (0U << 1));   /* ACLK_NVM_ROOT: ungate */

  /* Ungate eMMC clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (8 + 16)) | (0U << 8));   /* CCLK_SRC_EMMC: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (9 + 16)) | (0U << 9));   /* HCLK_EMMC: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (10 + 16)) | (0U << 10)); /* ACLK_EMMC: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (11 + 16)) | (0U << 11)); /* BCLK_EMMC: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (12 + 16)) | (0U << 12)); /* TCLK_EMMC: ungate */

  DEBUG ((DEBUG_INFO, "SdhciEmmcIoMux: eMMC clocks ungated, pinmux from SPL\n"));
}

/*
 * RK806 SPI iomux for PMIC communication
 * RK806 on ROCK 4D is connected via SPI to the PMU domain.
 * From rk3576-rock-4d.dts: pmic@23 on i2c1 with interrupt on GPIO0 pin 6.
 *
 * NOTE: RK806 on this board uses I2C (not SPI), so the SPI iomux
 * function here is a no-op. The I2C1 iomux is handled by I2cIomux().
 */
VOID
EFIAPI
Rk806SpiIomux (
  VOID
  )
{
  /* ROCK 4D: RK806 uses I2C1, not SPI. No SPI iomux needed. */
}

VOID
EFIAPI
Rk806Configure (
  VOID
  )
{
  UINTN  RegCfgIndex;

  RK806Init ();

  /* dvs1 → null (no DVS on ROCK 4D, matches dvs1-null-pins in DTS) */
  RK806PinSetFunction (MASTER, 1, 0);  /* gpio_pwrctrl1 = pin_fun0 (null) */

  for (RegCfgIndex = 0; RegCfgIndex < ARRAY_SIZE (rk806_init_data); RegCfgIndex++) {
    RK806RegulatorInit (rk806_init_data[RegCfgIndex]);
  }
}

VOID
EFIAPI
SetCPULittleVoltage (
  IN UINT32  Microvolts
  )
{
  /* A53 cluster = MASTER_BUCK3 (vdd_cpu_lit_s0) */
  struct regulator_init_data Rk806CpuLittleSupply =
    RK8XX_VOLTAGE_INIT (MASTER_BUCK3, Microvolts);
  RK806RegulatorInit (Rk806CpuLittleSupply);
}

/*
 * NorFspiIomux — SPI NOR flash iomux (sfc0: spi@2a340000)
 * From rk3576-rock-4d.dts: sfc0 uses fspi0_pins + fspi0_csn0
 *
 * CRU base for RK3576: 0x27200000
 * SFC clock select register: TBD from RK3576 TRM
 * For now, rely on SPL-initialized iomux.
 */
VOID
EFIAPI
NorFspiIomux (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "NorFspiIomux: SFC0 @ 0x2A340000 (SPL iomux retained)\n"));
}

VOID
EFIAPI
NorFspiEnableClock (
  UINT32  *CruBase
  )
{
  /*
   * SFC0 (spi@2a340000) clock enable — from clk-rk3576.c:
   *
   * Parent clocks (NVM domain):
   *   HCLK_NVM_ROOT  → CLKGATE_CON(33), bit 0
   *   ACLK_NVM_ROOT  → CLKGATE_CON(33), bit 1
   *
   * SFC0 clocks:
   *   SCLK_FSPI_X2   → CLKGATE_CON(33), bit 6  (SFC core clock)
   *   HCLK_FSPI      → CLKGATE_CON(33), bit 7  (SFC AHB bus clock)
   *
   * Rockchip write-enable: bits[31:16] = mask, write 0 to bit = ungate
   */

  /* Ungate NVM root clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (0 + 16)) | (0U << 0));   /* HCLK_NVM_ROOT: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (1 + 16)) | (0U << 1));   /* ACLK_NVM_ROOT: ungate */

  /* Ungate SFC0 clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (6 + 16)) | (0U << 6));   /* SCLK_FSPI_X2: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (7 + 16)) | (0U << 7));   /* HCLK_FSPI: ungate */

  DEBUG ((DEBUG_INFO, "NorFspiEnableClock: CLKGATE_CON(33) bits 0,1,6,7 ungated\n"));
}

/*
 * GmacIomux — GMAC iomux (gmac0: RTL8211F)
 * From rk3576-rock-4d.dts: eth0m0_miim, eth0m0_tx_bus2, etc.
 * RK3576_GMAC_ENABLE = FALSE in ROCK4D.dsc so this is not called.
 */
VOID
EFIAPI
GmacIomux (
  IN UINT32  Id
  )
{
  DEBUG ((DEBUG_INFO, "GmacIomux: GMAC disabled in UEFI (RK3576_GMAC_ENABLE=FALSE)\n"));
}

/*
 * I2cIomux — I2C pin configuration
 * From rk3576.dtsi and rk3576-rock-4d.dts:
 *   i2c1 @ 0x2AC40000: RK806 PMIC (address 0x23, interrupt GPIO0 pin 6)
 *   i2c2 @ 0x2AC50000: HYM8563 RTC (address 0x51, interrupt GPIO0 PA0)
 *
 * Actual pin function numbers from RK3576 pinctrl DTS:
 *   i2c1m0 default mux (from pinctrl-rk3576.c in kernel)
 *
 * PLACEHOLDER: Use GpioPinSetFunction with correct mux values from TRM.
 */
VOID
EFIAPI
I2cIomux (
  UINT32  id
  )
{
  switch (id) {
    case 1:
      /* i2c1: RK806 PMIC — mux values TBD from RK3576 pinctrl */
      /* GpioPinSetFunction (?, GPIO_PIN_P??, ?); i2c1_scl */
      /* GpioPinSetFunction (?, GPIO_PIN_P??, ?); i2c1_sda */
      DEBUG ((DEBUG_INFO, "I2cIomux: I2C1 (PMIC) — SPL iomux retained\n"));
      break;
    case 2:
      /* i2c2: HYM8563 RTC — mux values TBD from RK3576 pinctrl */
      DEBUG ((DEBUG_INFO, "I2cIomux: I2C2 (RTC) — SPL iomux retained\n"));
      break;
    default:
      break;
  }
}

/*
 * UsbPortPowerEnable — USB host power
 * From rk3576-rock-4d.dts: vcc_5v0_host enable-gpios = GPIO0 PD3 active-high
 */
VOID
EFIAPI
UsbPortPowerEnable (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "UsbPortPowerEnable: GPIO0 PD3 → high (USB HOST 5V)\n"));
  /* GPIO0 PD3 = bank 0, pin 27 (PD=24+3=27) */
  GpioPinWrite (0, GPIO_PIN_PD3, TRUE);
  GpioPinSetDirection (0, GPIO_PIN_PD3, GPIO_PIN_OUTPUT);
}

/*
 * Usb2PhyResume — USB2 PHY wakeup + USB clock enable
 *
 * CORRECTED: USB2 PHY registers are in usb2phy_grf syscon @ 0x2602E000
 * (NOT 0x2B000000 — that address was WRONG, copied from RK3588).
 *
 * Per mainline rk3576.dtsi:
 *   usb2phy_grf: syscon@2602e000  (size 0x4000)
 *     u2phy0 @ offset 0x0000 (size 0x10)
 *     u2phy1 @ offset 0x2000 (size 0x10)
 *
 * USB DWC3 clocks (from clk-rk3576.c):
 *   ACLK_USB_ROOT       → CLKGATE_CON(47), bit 1
 *   PCLK_USB_ROOT       → CLKGATE_CON(47), bit 2
 *   ACLK_USB3OTG0       → CLKGATE_CON(47), bit 5
 *   CLK_REF_USB3OTG0    → CLKGATE_CON(47), bit 6
 *   CLK_SUSPEND_USB3OTG0 → CLKGATE_CON(47), bit 7
 */
VOID
EFIAPI
Usb2PhyResume (
  VOID
  )
{
  /*
   * Step 1+2: Ungate ALL USB-related clocks in CLKGATE_CON(47)
   * (covers USB_ROOT, USB3OTG0, USB3OTG1 for both DWC3 controllers).
   * Write 0xFFFF<<16 mask + all-zero data = ungate every bit.
   */
  MmioWrite32 (CRU_CLKGATE_CON(47), 0xFFFF0000);

  /*
   * Step 3: USB2 PHY — minimal init
   *
   * Match RK3588 EDK2 style (ROCK5B): just deassert SIDDQ to power on
   * the analog block. DWC3 core will do GUSB2PHYCFG_PHYSOFTRST in
   * Dwc3CoreSoftReset() which handles the rest.
   *
   * RK3576 SIDDQ: usb2phy_grf + 0x10 bit 13
   *   Write upper-mask format: mask=bit13<<16=0x20000000, data=0
   */
  MmioWrite32 (USB2PHY0_BASE + 0x10, 0x20000000);
  MmioWrite32 (USB2PHY1_BASE + 0x10, 0x20000000);

  /* Also enable USB OTG 5V: GPIO2 PD2 (vcc5v0_otg, active-high) */
  GpioPinWrite (2, GPIO_PIN_PD2, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PD2, GPIO_PIN_OUTPUT);

  DEBUG ((DEBUG_INFO, "Usb2PhyResume: clocks ungated, both PHYs powered on, OTG 5V enabled\n"));
  DEBUG ((DEBUG_INFO, "  u2phy0 GRF @ 0x%lx, u2phy1 GRF @ 0x%lx\n",
    (UINT64)USB2PHY0_BASE, (UINT64)USB2PHY1_BASE));
}

/*
 * PcieIoInit — PCIe GPIO setup
 * From rk3576-rock-4d.dts:
 *   pcie0: reset = GPIO2 PB4 (active-high in DTS: reset-gpios = GPIO_ACTIVE_HIGH)
 *          power = GPIO2 PD3 (vcc_3v3_pcie enable-gpios, active-high)
 */
VOID
EFIAPI
PcieIoInit (
  UINT32  Segment
  )
{
  switch (Segment) {
    case 0:  /* pcie0 — combphy0_ps, M.2 slot */
      /* PCIe power enable: GPIO2 PD3 (bank 2, PD=24+3=27) */
      GpioPinSetDirection (2, GPIO_PIN_PD3, GPIO_PIN_OUTPUT);
      /* PCIe PERST#: GPIO2 PB4 (bank 2, PB=8+4=12) */
      GpioPinSetDirection (2, GPIO_PIN_PB4, GPIO_PIN_OUTPUT);
      break;
    default:
      break;
  }
}

VOID
EFIAPI
PciePowerEn (
  UINT32   Segment,
  BOOLEAN  Enable
  )
{
  switch (Segment) {
    case 0:  /* GPIO2 PD3 — vcc_3v3_pcie */
      GpioPinWrite (2, GPIO_PIN_PD3, Enable);
      break;
    default:
      break;
  }
}

VOID
EFIAPI
PciePeReset (
  UINT32   Segment,
  BOOLEAN  Enable
  )
{
  switch (Segment) {
    case 0:  /* GPIO2 PB4 — pcie0 reset (active-high in DTS) */
      GpioPinWrite (2, GPIO_PIN_PB4, Enable);
      break;
    default:
      break;
  }
}

/*
 * HdmiTxIomux — HDMI TX pin mux & Clock ungating
 * ROCK 4D has one HDMI port (hdmi: hdmi@27da0000).
 * This function also handles ungating VOP2 and HDMI clocks
 * because the Display DXE drivers don't know RK3576's CRU layout.
 */
VOID
EFIAPI
HdmiTxIomux (
  IN UINT32  Id
  )
{
  switch (Id) {
    case 0:
      /*
       * Ungate VOP2 & HDMI clocks (from clk-rk3576.c)
       * CRU_BASE = 0x27200000
       */
      
      /* CLKGATE_CON(61): VOP Root Clocks
       * BIT[8]:  HCLK_VOP
       * BIT[9]:  ACLK_VOP
       * BIT[0]:  ACLK_VOP_ROOT
       * BIT[2]:  HCLK_VOP_ROOT
       * BIT[3]:  PCLK_VOP_ROOT
       * BIT[10]: DCLK_VP0_SRC
       * BIT[11]: DCLK_VP1_SRC
       * BIT[12]: DCLK_VP2_SRC
       * BIT[13]: DCLK_VP0
       */
      MmioWrite32 (CRU_CLKGATE_CON(61),
        0x3F0D0000 | 0x0000);  /* write-enable mask top 16 bits, value 0 to ungate */

      /* CLKGATE_CON(62): VOP VP1/VP2
       * BIT[0]: DCLK_VP1
       * BIT[1]: DCLK_VP2
       */
      MmioWrite32 (CRU_CLKGATE_CON(62),
        0x00030000 | 0x0000);

      /* CLKGATE_CON(63): VO0 Root Clocks
       * BIT[0]: ACLK_VO0_ROOT
       * BIT[1]: HCLK_VO0_ROOT
       * BIT[3]: PCLK_VO0_ROOT
       */
      MmioWrite32 (CRU_CLKGATE_CON(63),
        0x000B0000 | 0x0000);

      /* CLKGATE_CON(64): HDMI TX Clocks
       * BIT[7]: PCLK_HDMITX0
       * BIT[8]: CLK_HDMITX0_EARC
       * BIT[9]: CLK_HDMITX0_REF
       */
      MmioWrite32 (CRU_CLKGATE_CON(64),
        0x03800000 | 0x0000);
        
      /* Note: PMU clocks (PCLK_HDPTX_APB, CLK_HDMITXHDP, PHY clocks) 
       * are in the PMU CRU. U-Boot SPL should have already ungated them.
       */

      DEBUG ((DEBUG_INFO, "HdmiTxIomux: VOP and HDMI clocks ungated\n"));
      break;
    default:
      break;
  }
}

/*
 * PwmFanIoSetup / PwmFanSetSpeed
 * ROCK 4D has no on-board fan connector exposed in mainline DTS.
 * These are no-ops.
 */
VOID
EFIAPI
PwmFanIoSetup (
  VOID
  )
{
}

VOID
EFIAPI
PwmFanSetSpeed (
  IN UINT32  Percentage
  )
{
}

/*
 * PlatformInitLeds — Initialize board LEDs
 * From rk3576-rock-4d.dts:
 *   power-led: GPIO0 PB4 (active-high, green, default-state=on)
 *   user-led:  GPIO0 PC4 (active-low,  blue,  heartbeat trigger)
 */
VOID
EFIAPI
PlatformInitLeds (
  VOID
  )
{
  /* Power LED: GPIO0 PB4 = bank 0, pin 12 */
  GpioPinWrite (0, GPIO_PIN_PB4, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PB4, GPIO_PIN_OUTPUT);

  /* User LED: GPIO0 PC4 = bank 0, pin 20 — keep off initially */
  GpioPinWrite (0, GPIO_PIN_PC4, TRUE);  /* active-low → TRUE = off */
  GpioPinSetDirection (0, GPIO_PIN_PC4, GPIO_PIN_OUTPUT);
}

VOID
EFIAPI
PlatformSetStatusLed (
  IN BOOLEAN  Enable
  )
{
  /* Power LED: GPIO0 PB4 active-high */
  GpioPinWrite (0, GPIO_PIN_PB4, Enable);
}

/*
 * PlatformEarlyInit — Early board initialization
 * Called before driver dispatch. Enable WiFi power for M.2 WiFi module.
 * From rk3576-rock-4d.dts:
 *   wifi rfkill shutdown-gpios = GPIO2 PD1 (active-high)
 *   vcc_3v3_wifi enable-gpios  = GPIO2 PC7 (active-high)
 */
VOID
EFIAPI
PlatformEarlyInit (
  VOID
  )
{
  /* Enable WiFi/BT power supply: GPIO2 PC7 (bank 2, PC=16+7=23) */
  GpioPinWrite (2, GPIO_PIN_PC7, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PC7, GPIO_PIN_OUTPUT);

  /* Enable WiFi: GPIO2 PD1 (bank 2, PD=24+1=25) */
  GpioPinWrite (2, GPIO_PIN_PD1, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PD1, GPIO_PIN_OUTPUT);

  DEBUG ((DEBUG_INFO, "ROCK 4D: PlatformEarlyInit done\n"));
}

/*
 * PlatformGetDtbFileGuid — Return GUID for embedded DTB based on compat mode
 * GUIDs match the FILE_GUID in DeviceTree/Vendor.inf and DeviceTree/Mainline.inf
 */
CONST EFI_GUID *
EFIAPI
PlatformGetDtbFileGuid (
  IN UINT32  CompatMode
  )
{
  STATIC CONST EFI_GUID VendorDtbFileGuid = {
    /* DeviceTree/Vendor.inf FILE_GUID */
    0xa1b2c3d4, 0xe5f6, 0x4a7b, { 0x8c, 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d }
  };
  STATIC CONST EFI_GUID MainlineDtbFileGuid = {
    /* DeviceTree/Mainline.inf FILE_GUID */
    0xb2c3d4e5, 0xf6a7, 0x4b8c, { 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e }
  };

  switch (CompatMode) {
    case FDT_COMPAT_MODE_VENDOR:
      return &VendorDtbFileGuid;
    case FDT_COMPAT_MODE_MAINLINE:
      return &MainlineDtbFileGuid;
  }

  return NULL;
}
