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
#include <Library/GpioLib.h>
#include <Library/RK806.h>
#include <Library/PWMLib.h>
#include <Library/TimerLib.h>
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
 * From rk3576-pinctrl.dtsi (sdmmc0 groups, all function 1):
 *   sdmmc0_bus4:  GPIO2 PA0-PA3  (D0-D3)
 *   sdmmc0_cmd:   GPIO2 PA4      (CMD)
 *   sdmmc0_clk:   GPIO2 PA5      (CLK)
 *   sdmmc0_pwren: GPIO0 PB6      (power enable)
 *   sdmmc0_det:   GPIO0 PA7      (hardware DETN — function 1)
 *
 * Card-presence polling (software) uses GPIO0 PA5 (configured separately
 * in RkSdmmcPlatformLib as plain input; not set via GpioPinSetFunction here).
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
   */

  /* Ungate SDGMAC root clock */
  MmioWrite32 (CRU_CLKGATE_CON(42),
    (1U << (0 + 16)) | (0U << 0));   /* HCLK_SDGMAC_ROOT: ungate */

  /* Ungate SDMMC0 clocks */
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (1 + 16)) | (0U << 1));   /* CCLK_SRC_SDMMC0: ungate */
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (2 + 16)) | (0U << 2));   /* HCLK_SDMMC0: ungate */

  /* sdmmc0_bus4: GPIO2 PA0-PA3 function 1 (data D0-D3) */
  GpioPinSetFunction (2, GPIO_PIN_PA0, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA1, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA2, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA3, 1);
  /* sdmmc0_cmd: GPIO2 PA4 function 1 */
  GpioPinSetFunction (2, GPIO_PIN_PA4, 1);
  /* sdmmc0_clk: GPIO2 PA5 function 1 */
  GpioPinSetFunction (2, GPIO_PIN_PA5, 1);
  /* sdmmc0_pwren: GPIO0 PB6 function 1 (hardware power enable) */
  GpioPinSetFunction (0, GPIO_PIN_PB6, 1);
  /* sdmmc0_det: GPIO0 PA7 function 1 (hardware DETN input) */
  GpioPinSetFunction (0, GPIO_PIN_PA7, 1);

  DEBUG ((DEBUG_INFO, "SdmmcIoMux: SDMMC0 clocks ungated + iomux set\n"));
}

/*
 * SdhciEmmcIoMux — eMMC iomux (sdhci: mmc@2a330000)
 *
 * From rk3576-pinctrl.dtsi (emmc groups, all function 1):
 *   emmc_bus8: GPIO1 PA0-PA7  (D0-D7)
 *   emmc_cmd:  GPIO1 PB0      (CMD)
 *   emmc_clk:  GPIO1 PB1      (CLK)
 *   emmc_strb: GPIO1 PB2      (STRB)
 *   emmc_rstnout: GPIO1 PB3   (RSTN)
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

  /* emmc_bus8: GPIO1 PA0-PA7 function 1 (data D0-D7) */
  GpioPinSetFunction (1, GPIO_PIN_PA0, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA1, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA2, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA3, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA4, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA5, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA6, 1);
  GpioPinSetFunction (1, GPIO_PIN_PA7, 1);
  /* emmc_cmd: GPIO1 PB0 function 1 */
  GpioPinSetFunction (1, GPIO_PIN_PB0, 1);
  /* emmc_clk: GPIO1 PB1 function 1 */
  GpioPinSetFunction (1, GPIO_PIN_PB1, 1);
  /* emmc_strb: GPIO1 PB2 function 1 */
  GpioPinSetFunction (1, GPIO_PIN_PB2, 1);
  /* emmc_rstnout: GPIO1 PB3 function 1 */
  GpioPinSetFunction (1, GPIO_PIN_PB3, 1);

  DEBUG ((DEBUG_INFO, "SdhciEmmcIoMux: eMMC clocks ungated + iomux set\n"));
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
 * GmacIomux — GMAC iomux for ROCK 4D
 *
 * gmac0 (ROCK 4D): RTL8211F in RGMII-ID mode, eth0 M0 pins, all function 3.
 * Pin mapping from rk3576-pinctrl.dtsi eth0m0_* groups:
 *   eth0m0_miim:       GPIO3_PA5 (MDIO), GPIO3_PA6 (MDC)
 *   eth0m0_rx_bus2:    GPIO3_PA7 (RXCTL), GPIO3_PB2 (RXD0), GPIO3_PB1 (RXD1)
 *   eth0m0_tx_bus2:    GPIO3_PB3 (TXCTL), GPIO3_PB5 (TXD0), GPIO3_PB4 (TXD1)
 *   eth0m0_rgmii_clk:  GPIO3_PD1 (RXCLK), GPIO3_PB6 (TXCLK)
 *   eth0m0_rgmii_bus:  GPIO3_PD3 (RXD2), GPIO3_PD2 (RXD3), GPIO3_PC3 (TXD2), GPIO3_PC2 (TXD3)
 *   ethm0_clk0_25m_out: GPIO3_PA4 (25 MHz ref out)
 * gmac1: not connected on ROCK 4D — no iomux needed.
 */
VOID
EFIAPI
GmacIomux (
  IN UINT32  Id
  )
{
  if (Id != 0) {
    return;  /* gmac1 not used on ROCK 4D */
  }

  /* eth0m0_miim */
  GpioPinSetFunction (3, GPIO_PIN_PA5, 3);  /* eth0_mdio_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PA6, 3);  /* eth0_mdc_m0  */

  /* eth0m0_rx_bus2 */
  GpioPinSetFunction (3, GPIO_PIN_PA7, 3);  /* eth0_rxctl_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PB2, 3);  /* eth0_rxd0_m0  */
  GpioPinSetFunction (3, GPIO_PIN_PB1, 3);  /* eth0_rxd1_m0  */

  /* eth0m0_tx_bus2 */
  GpioPinSetFunction (3, GPIO_PIN_PB3, 3);  /* eth0_txctl_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PB5, 3);  /* eth0_txd0_m0  */
  GpioPinSetFunction (3, GPIO_PIN_PB4, 3);  /* eth0_txd1_m0  */

  /* eth0m0_rgmii_clk */
  GpioPinSetFunction (3, GPIO_PIN_PD1, 3);  /* eth0_rxclk_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PB6, 3);  /* eth0_txclk_m0 */

  /* eth0m0_rgmii_bus */
  GpioPinSetFunction (3, GPIO_PIN_PD3, 3);  /* eth0_rxd2_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PD2, 3);  /* eth0_rxd3_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PC3, 3);  /* eth0_txd2_m0 */
  GpioPinSetFunction (3, GPIO_PIN_PC2, 3);  /* eth0_txd3_m0 */

  /* ethm0_clk0_25m_out — 25 MHz reference clock output to PHY */
  GpioPinSetFunction (3, GPIO_PIN_PA4, 3);  /* ethm0_clk0_25m_out */
}

/*
 * GmacIoPhyReset — Assert / deassert the ETH PHY reset GPIO.
 *
 * gmac0 PHY: GPIO2_PB5, GPIO_ACTIVE_LOW
 *   (from rk3576-rock-4d.dts: reset-gpios = <&gpio2 RK_PB5 GPIO_ACTIVE_LOW>)
 *   Enable=TRUE  → assert reset   → drive pin LOW  (active-low)
 *   Enable=FALSE → deassert reset → drive pin HIGH
 */
VOID
EFIAPI
GmacIoPhyReset (
  IN UINT32   Id,
  IN BOOLEAN  Enable
  )
{
  if (Id != 0) {
    return;
  }

  /* On first call (Enable=TRUE), ensure pin is configured as output */
  GpioPinSetDirection (2, GPIO_PIN_PB5, GPIO_PIN_OUTPUT);
  GpioPinWrite (2, GPIO_PIN_PB5, !Enable);  /* active-low: assert = write 0 */
}

/*
 * I2cIomux — I2C pin configuration
 * From rk3576.dtsi and rk3576-rock-4d.dts (and rk3576-pinctrl.dtsi m0 groups):
 *   i2c1 @ 0x2AC40000: RK806 PMIC (address 0x23, interrupt GPIO0 pin 6)
 *     i2c1m0_xfer: GPIO0 PB2 (fn 11) = SCL, GPIO0 PB3 (fn 11) = SDA
 *   i2c2 @ 0x2AC50000: HYM8563 RTC (address 0x51, interrupt GPIO0 PA0)
 *     i2c2m0_xfer: GPIO0 PB7 (fn  9) = SCL, GPIO0 PC0 (fn  9) = SDA
 */
VOID
EFIAPI
I2cIomux (
  UINT32  id
  )
{
  switch (id) {
    case 1:
      /* i2c1m0: RK806 PMIC — GPIO0 PB2/PB3, function 11 */
      GpioPinSetFunction (0, GPIO_PIN_PB2, 11);  /* i2c1_scl_m0 */
      GpioPinSetFunction (0, GPIO_PIN_PB3, 11);  /* i2c1_sda_m0 */
      DEBUG ((DEBUG_INFO, "I2cIomux: I2C1 (PMIC) GPIO0 PB2/PB3 fn11\n"));
      break;
    case 2:
      /* i2c2m0: HYM8563 RTC — GPIO0 PB7/PC0, function 9 */
      GpioPinSetFunction (0, GPIO_PIN_PB7, 9);   /* i2c2_scl_m0 */
      GpioPinSetFunction (0, GPIO_PIN_PC0, 9);   /* i2c2_sda_m0 */
      DEBUG ((DEBUG_INFO, "I2cIomux: I2C2 (RTC) GPIO0 PB7/PC0 fn9\n"));
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
 * Per mainline rk3576.dtsi + Linux rk3576_usb2phy_tuning():
 *   usb2phy_grf: syscon@2602e000  (size 0x4000)
 *     u2phy0 @ offset 0x0000  (USB-C OTG port, DWC3 @ 0x23000000)
 *     u2phy1 @ offset 0x2000  (USB-A HOST port, DWC3 @ 0x23400000)
 *
 * USB DWC3 clocks (from clk-rk3576.c):
 *   ACLK_USB_ROOT       → CLKGATE_CON(47), bit 1
 *   PCLK_USB_ROOT       → CLKGATE_CON(47), bit 2
 *   ACLK_USB3OTG0       → CLKGATE_CON(47), bit 5
 *   CLK_REF_USB3OTG0    → CLKGATE_CON(47), bit 6
 *   CLK_SUSPEND_USB3OTG0 → CLKGATE_CON(47), bit 7
 *
 * USB2 PHY resets in main CRU SOFTRST_CON28 (CRU_BASE + 0xA70):
 *   bit 0 = SRST_USB2PHY0_U2_0  (CRU reset ID 448)
 *   bit 1 = SRST_USB2PHY1_U2_0  (CRU reset ID 449)
 */
VOID
EFIAPI
Usb2PhyResume (
  VOID
  )
{
  /*
   * Step 1: Ungate ALL USB-related clocks in CLKGATE_CON(47).
   * Covers ACLK/PCLK_USB_ROOT, USB3OTG0 + USB3OTG1, REF and SUSPEND
   * for both DWC3 controllers. Write 0xFFFF mask + all-zero data.
   */
  MmioWrite32 (CRU_CLKGATE_CON(47), 0xFFFF0000);

  /*
   * Step 2: Deassert SIDDQ to power on PHY analog block.
   * GRF+0x0010 bit 13 (SIDDQ): mask=BIT(29) → write-enable bit13, data=0.
   * From Linux rk3576_usb2phy_tuning():
   *   regmap_write(grf, reg+0x0010, GENMASK(29,29) | 0x0000)
   */
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x20000000);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x20000000);

  /*
   * Step 3: First PHY CRU hard reset after SIDDQ deassert (IDDQ exit).
   * Linux rk3576_usb2phy_tuning() calls rockchip_usb2phy_reset() here.
   * Assert SRST_USB2PHY0_U2 (bit 0) + SRST_USB2PHY1_U2 (bit 1) in
   * SOFTRST_CON28, hold 10 µs, deassert, then wait 150 µs for PLL lock.
   * Write-mask: upper 16 bits = enable mask, lower 16 = data.
   */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);  /* assert  bits 0,1 */
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);  /* deassert bits 0,1 */
  MicroSecondDelay (150);

  /*
   * Step 4: HS signal quality tuning — applied BEFORE phy_sus clear so
   * the second reset (step 5b) latches the tuning values, matching Linux
   * where rk3576_usb2phy_tuning() runs in probe() before power_on():
   *   GRF+0x000C bits 11:8 = 0x9 → HS DC Voltage Level +5.89%
   *     mask=GENMASK(27,24)=0x0F000000, data=0x0900
   *   GRF+0x0010 bits 4:3  = 2   → HS TX Pre-Emphasis 2x
   *     mask=GENMASK(20,19)=0x00180000, data=0x0010
   */
  MmioWrite32 (USB2PHY0_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY1_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x00180010);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x00180010);

  /*
   * Step 5: Clear phy_sus to exit USB suspend mode.
   * GRF+0x0000 bits 8:0 (phy_sus): write mask=0x01FF, data=0 → all zero.
   * rk3576_phy_cfgs: phy_sus={0x0000, 8, 0, disable=0, enable=0x1d1}
   * Linux: property_enable(base, phy_sus, false) in rockchip_usb2phy_power_on().
   */
  MmioWrite32 (USB2PHY0_BASE + 0x0000, 0x01FF0000);
  MmioWrite32 (USB2PHY1_BASE + 0x0000, 0x01FF0000);

  /*
   * Step 5b: Second PHY CRU hard reset after phy_sus clear.
   * Linux rockchip_usb2phy_power_on() calls rockchip_usb2phy_reset() here
   * to re-initialize the PHY digital block now that the analog PLL is
   * running (phy_sus=0) and the UTMI clock is becoming active.
   */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);  /* assert  bits 0,1 */
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);  /* deassert bits 0,1 */
  MicroSecondDelay (150);

  /*
   * Step 5c: Wait for UTMI clock to stabilize before DWC3 can use the PHY.
   * Linux: usleep_range(1500, 2000) µs after the second reset in power_on().
   * Without this, UTMI bus glitches can prevent HS handshake / detection.
   */
  MicroSecondDelay (2000);

  /* Step 6: Enable USB OTG 5V power: GPIO2 PD2 (vcc5v0_otg, active-high) */
  GpioPinWrite (2, GPIO_PIN_PD2, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PD2, GPIO_PIN_OUTPUT);

  DEBUG ((DEBUG_INFO,
    "Usb2PhyResume: SIDDQ off, 1st reset, HS tuning, phy_sus off, "
    "2nd reset+2ms, OTG 5V on\n"));
  DEBUG ((DEBUG_INFO, "  u2phy0 GRF @ 0x%lx, u2phy1 GRF @ 0x%lx\n",
    (UINT64)USB2PHY0_BASE, (UINT64)USB2PHY1_BASE));

  /*
   * Readback diagnostics — confirm GRF writes landed and check PHY status:
   *   GRF+0x0000 [8:0]  = phy_sus  (should be 0)
   *   GRF+0x0010 [13]   = SIDDQ    (should be 0)
   *   GRF+0x0080 [5:4]  = utmi_ls  (00=SE0/idle, 10=J/FS-device present)
   *   GRF+0x0080 [1]    = utmi_avalid
   *   GRF+0x0080 [0]    = utmi_bvalid
   */
  DEBUG ((DEBUG_INFO,
    "  u2phy0 GRF[0x00]=0x%08x [0x10]=0x%08x [0x80]=0x%08x\n",
    MmioRead32 (USB2PHY0_BASE + 0x0000),
    MmioRead32 (USB2PHY0_BASE + 0x0010),
    MmioRead32 (USB2PHY0_BASE + 0x0080)));
  DEBUG ((DEBUG_INFO,
    "  u2phy1 GRF[0x00]=0x%08x [0x10]=0x%08x [0x80]=0x%08x\n",
    MmioRead32 (USB2PHY1_BASE + 0x0000),
    MmioRead32 (USB2PHY1_BASE + 0x0010),
    MmioRead32 (USB2PHY1_BASE + 0x0080)));
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
        
      /* Note: HDPTX PHY reference clock (CLK_PHY_REF_SRC, ID 526) and
       * APB clock (PCLK_HDPTX_APB, ID 545) are in the main CRU (not PMU CRU).
       * Both are typically ungated by U-Boot SPL during system init.
       * If HDPTX PLL lock times out, check CRU CLKGATE registers for these.
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
