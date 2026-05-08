/** @file
 *
 *  RockchipPlatformLib.c — Board-specific init for FriendlyElec NanoPi M5 (RK3576)
 *
 *  Based on Radxa ROCK 4D RockchipPlatformLib.c (same RK3576 SoC).
 *  Key differences from ROCK 4D:
 *    - GMAC1 is CONNECTED (2x 1Gbps Ethernet) — eth1m0 pins on GPIO bank 4
 *    - 3 LEDs: SYS (GPIO0 PB4 active-high), LED1/LED2 (active-low, TBD)
 *    - PCIe power/reset GPIOs: using ROCK4D defaults (TODO: verify NanoPi M5 schematic)
 *    - USB host power: GPIO0 PD3 active-high (same as ROCK 4D)
 *    - WiFi power: GPIO2 PC7 + GPIO2 PD1 (same as ROCK 4D — shared M.2 E-Key layout)
 *
 *  GPIO pins verified from:
 *    FriendlyElec wiki: https://wiki.friendlyelec.com/wiki/index.php/NanoPi_M5
 *    FriendlyElec U-Boot DTS (nanopi5-v2017.09 branch)
 *    rk3576-pinctrl.dtsi (eth1m0 groups)
 *
 *  Copyright (c) 2021, Rockchip Limited.
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, FriendlyElec NanoPi M5 EDK2 Port
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
 * RK806 PMIC voltage init for NanoPi M5 (RK3576)
 *
 * Rail mapping is identical to ROCK 4D — both boards use the same
 * RK806 PMIC configuration for the RK3576 SoC.
 *
 *   DCDC1  → vdd_cpu_big_s0  (A72 cluster)
 *   DCDC3  → vdd_cpu_lit_s0  (A53 cluster)
 *   DCDC4  → vcc_3v3_s3
 *   DCDC7  → vdd_logic_s0
 *   DCDC8  → vcc_1v8_s3
 *   PLDO1  → vcca_1v8_s0
 *   PLDO2  → vcca1v8_pldo2_s0
 *   PLDO3  → vdda_1v2_s0
 *   PLDO4  → vcca_3v3_s0
 *   PLDO6  → vcca1v8_pldo6_s3
 *   NLDO1  → vdd_0v75_s3
 *   NLDO2  → vdda_ddr_pll_s0
 *   NLDO3  → vdda0v75_hdmi_s0
 *   NLDO4  → vdda_0v85_s0
 *   NLDO5  → vdda_0v75_s0
 */
static struct regulator_init_data rk806_init_data[] = {
  RK8XX_VOLTAGE_INIT (MASTER_BUCK1,   750000),   /* vdd_cpu_big_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK3,   750000),   /* vdd_cpu_lit_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK4,  3300000),   /* vcc_3v3_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK7,   750000),   /* vdd_logic_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK8,  1800000),   /* vcc_1v8_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK10, 1800000),   /* vcc_1v8_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO1,  1800000),   /* vcca_1v8_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO2,  1800000),   /* vcca1v8_pldo2_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO3,  1200000),   /* vdda_1v2_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO4,  3300000),   /* vcca_3v3_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_PLDO6,  1800000),   /* vcca1v8_pldo6_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO1,   750000),   /* vdd_0v75_s3 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO2,   850000),   /* vdda_ddr_pll_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO3,   837500),   /* vdda0v75_hdmi_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO4,   850000),   /* vdda_0v85_s0 */
  RK8XX_VOLTAGE_INIT (MASTER_NLDO5,   750000),   /* vdda_0v75_s0 */
};

/*
 * SdmmcIoMux — SD card iomux (sdmmc: mmc@2a310000)
 * Same GPIO mapping as ROCK 4D (shared pinctrl group on RK3576).
 */
VOID
EFIAPI
SdmmcIoMux (
  VOID
  )
{
  /* Ungate SDGMAC root clock */
  MmioWrite32 (CRU_CLKGATE_CON(42), (1U << (0 + 16)) | (0U << 0));

  /* Ungate SDMMC0 clocks */
  MmioWrite32 (CRU_CLKGATE_CON(43), (1U << (1 + 16)) | (0U << 1));
  MmioWrite32 (CRU_CLKGATE_CON(43), (1U << (2 + 16)) | (0U << 2));

  /* sdmmc0_bus4: GPIO2 PA0-PA3 function 1 */
  GpioPinSetFunction (2, GPIO_PIN_PA0, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA1, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA2, 1);
  GpioPinSetFunction (2, GPIO_PIN_PA3, 1);
  /* sdmmc0_cmd: GPIO2 PA4 function 1 */
  GpioPinSetFunction (2, GPIO_PIN_PA4, 1);
  /* sdmmc0_clk: GPIO2 PA5 function 1 */
  GpioPinSetFunction (2, GPIO_PIN_PA5, 1);
  /* sdmmc0_pwren: GPIO0 PB6 function 1 */
  GpioPinSetFunction (0, GPIO_PIN_PB6, 1);
  /* sdmmc0_det: GPIO0 PA7 function 1 */
  GpioPinSetFunction (0, GPIO_PIN_PA7, 1);

  DEBUG ((DEBUG_INFO, "NanoPi M5: SdmmcIoMux done\n"));
}

/*
 * SdhciEmmcIoMux — eMMC iomux (sdhci: mmc@2a330000)
 * NanoPi M5 has no onboard eMMC; UFS is managed by SPL.
 * Keep this function as a no-op (same pattern as other boards with no eMMC).
 */
VOID
EFIAPI
SdhciEmmcIoMux (
  VOID
  )
{
  /* NanoPi M5: no onboard eMMC — UFS is managed by SPL, not UEFI */
  DEBUG ((DEBUG_INFO, "NanoPi M5: SdhciEmmcIoMux (no eMMC, UFS managed by SPL)\n"));
}

/*
 * Rk806SpiIomux — NanoPi M5 uses RK806 via I2C1 (not SPI)
 */
VOID
EFIAPI
Rk806SpiIomux (
  VOID
  )
{
  /* NanoPi M5: RK806 uses I2C1, not SPI. No SPI iomux needed. */
}

VOID
EFIAPI
Rk806Configure (
  VOID
  )
{
  UINTN  RegCfgIndex;

  RK806Init ();

  RK806PinSetFunction (MASTER, 1, 0);  /* dvs1 → null */

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
  struct regulator_init_data Rk806CpuLittleSupply =
    RK8XX_VOLTAGE_INIT (MASTER_BUCK3, Microvolts);
  RK806RegulatorInit (Rk806CpuLittleSupply);
}

/*
 * NorFspiIomux — SPI NOR flash iomux (SFC0 @ 0x2A340000)
 * SPL already initializes the SFC iomux; retain here for completeness.
 */
VOID
EFIAPI
NorFspiIomux (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "NanoPi M5: NorFspiIomux (SPL iomux retained)\n"));
}

VOID
EFIAPI
NorFspiEnableClock (
  UINT32  *CruBase
  )
{
  /* NVM domain: HCLK + ACLK root clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33), (1U << (0 + 16)) | (0U << 0));
  MmioWrite32 (CRU_CLKGATE_CON(33), (1U << (1 + 16)) | (0U << 1));

  /* SFC0: SCLK_FSPI_X2 + HCLK_FSPI */
  MmioWrite32 (CRU_CLKGATE_CON(33), (1U << (6 + 16)) | (0U << 6));
  MmioWrite32 (CRU_CLKGATE_CON(33), (1U << (7 + 16)) | (0U << 7));

  DEBUG ((DEBUG_INFO, "NanoPi M5: NorFspiEnableClock done\n"));
}

/*
 * GmacIomux — GMAC iomux for NanoPi M5
 *
 * GMAC0 (eth0): RTL8211F RGMII-ID, eth0m0 pins (GPIO bank 3, all function 3)
 *   Identical to ROCK 4D GMAC0 wiring.
 *
 * GMAC1 (eth1): RTL8211F RGMII-ID, eth1m0 pins (GPIO bank 4, all function 3)
 *   This is the KEY addition over ROCK 4D!
 *   Pin mapping from rk3576-pinctrl.dtsi eth1m0_* groups:
 *     ethm1_clk0_25m_out: GPIO4 PA2  (25 MHz ref out to PHY)
 *     eth1m0_miim:        GPIO4 PA3 (MDC), GPIO4 PA4 (MDIO)
 *     eth1m0_rx_bus2:     GPIO4 PB5 (RXCTL), GPIO4 PB3 (RXD0), GPIO4 PB4 (RXD1)
 *     eth1m0_tx_bus2:     GPIO4 PB6 (TXCTL), GPIO4 PC1 (TXD0), GPIO4 PC0 (TXD1)
 *     eth1m0_rgmii_clk:   GPIO4 PB1 (RXCLK), GPIO4 PB7 (TXCLK)
 *     eth1m0_rgmii_bus:   GPIO4 PC5 (RXD2),  GPIO4 PC4 (RXD3)
 *                         GPIO4 PC3 (TXD2),  GPIO4 PC2 (TXD3)
 */
VOID
EFIAPI
GmacIomux (
  IN UINT32  Id
  )
{
  switch (Id) {
    case 0:
      /* GMAC0: eth0m0 pins on GPIO bank 3 (identical to ROCK 4D) */

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

      /* ethm0_clk0_25m_out */
      GpioPinSetFunction (3, GPIO_PIN_PA4, 3);  /* 25 MHz ref out */

      DEBUG ((DEBUG_INFO, "NanoPi M5: GmacIomux(0) GMAC0 eth0m0 done\n"));
      break;

    case 1:
      /*
       * GMAC1: eth1m0 pins on GPIO bank 4, all function 3
       * NanoPi M5 unique feature — ROCK 4D does not wire GMAC1.
       */

      /* ethm1_clk0_25m_out: GPIO4 PA2 — 25 MHz reference clock to PHY */
      GpioPinSetFunction (4, GPIO_PIN_PA2, 3);

      /* eth1m0_miim: MDC + MDIO */
      GpioPinSetFunction (4, GPIO_PIN_PA3, 3);  /* eth1_mdc_m0  */
      GpioPinSetFunction (4, GPIO_PIN_PA4, 3);  /* eth1_mdio_m0 */

      /* eth1m0_rgmii_clk: RX and TX clocks */
      GpioPinSetFunction (4, GPIO_PIN_PB1, 3);  /* eth1_rxclk_m0 */
      GpioPinSetFunction (4, GPIO_PIN_PB7, 3);  /* eth1_txclk_m0 */

      /* eth1m0_rx_bus2: RX data + control */
      GpioPinSetFunction (4, GPIO_PIN_PB3, 3);  /* eth1_rxd0_m0  */
      GpioPinSetFunction (4, GPIO_PIN_PB4, 3);  /* eth1_rxd1_m0  */
      GpioPinSetFunction (4, GPIO_PIN_PB5, 3);  /* eth1_rxctl_m0 */

      /* eth1m0_tx_bus2: TX data + control */
      GpioPinSetFunction (4, GPIO_PIN_PB6, 3);  /* eth1_txctl_m0 */
      GpioPinSetFunction (4, GPIO_PIN_PC0, 3);  /* eth1_txd1_m0  */
      GpioPinSetFunction (4, GPIO_PIN_PC1, 3);  /* eth1_txd0_m0  */

      /* eth1m0_rgmii_bus: extended RGMII data bits */
      GpioPinSetFunction (4, GPIO_PIN_PC2, 3);  /* eth1_txd3_m0 */
      GpioPinSetFunction (4, GPIO_PIN_PC3, 3);  /* eth1_txd2_m0 */
      GpioPinSetFunction (4, GPIO_PIN_PC4, 3);  /* eth1_rxd3_m0 */
      GpioPinSetFunction (4, GPIO_PIN_PC5, 3);  /* eth1_rxd2_m0 */

      DEBUG ((DEBUG_INFO, "NanoPi M5: GmacIomux(1) GMAC1 eth1m0 done\n"));
      break;

    default:
      break;
  }
}

/*
 * GmacIoPhyReset — Assert / deassert the ETH PHY reset GPIO.
 *
 * GMAC0 PHY: GPIO2 PB5, active-low (same as ROCK 4D)
 *   reset-gpios = <&gpio2 RK_PB5 GPIO_ACTIVE_LOW>
 *
 * GMAC1 PHY: TODO — NanoPi M5 schematic needed to confirm PHY reset GPIO.
 *   Using GPIO4 PB0 as placeholder (common choice near eth1 bank).
 *   Update once FriendlyElec publishes schematic or DTS includes reset-gpios.
 */
VOID
EFIAPI
GmacIoPhyReset (
  IN UINT32   Id,
  IN BOOLEAN  Enable
  )
{
  switch (Id) {
    case 0:
      /* GMAC0 PHY reset: GPIO2 PB5, active-low */
      GpioPinSetDirection (2, GPIO_PIN_PB5, GPIO_PIN_OUTPUT);
      GpioPinWrite (2, GPIO_PIN_PB5, !Enable);
      break;

    case 1:
      /*
       * GMAC1 PHY reset: TODO — verify from NanoPi M5 schematic.
       * Placeholder: GPIO4 PB0, active-low (typical FriendlyElec pattern).
       * If ETH1 fails to come up, check this GPIO and update accordingly.
       */
      GpioPinSetDirection (4, GPIO_PIN_PB0, GPIO_PIN_OUTPUT);
      GpioPinWrite (4, GPIO_PIN_PB0, !Enable);
      DEBUG ((DEBUG_INFO, "NanoPi M5: GmacIoPhyReset(1) GPIO4 PB0 — verify from schematic!\n"));
      break;

    default:
      break;
  }
}

/*
 * I2cIomux — I2C pin configuration
 *   I2C1: RK806 PMIC @ addr 0x23 — GPIO0 PB2/PB3 function 11
 *   I2C2: HYM8563 RTC @ addr 0x51 — GPIO0 PB7/PC0 function 9
 * Same as ROCK 4D (same RK3576 default I2C pin assignment).
 */
VOID
EFIAPI
I2cIomux (
  UINT32  id
  )
{
  switch (id) {
    case 1:
      GpioPinSetFunction (0, GPIO_PIN_PB2, 11);  /* i2c1_scl_m0 (PMIC) */
      GpioPinSetFunction (0, GPIO_PIN_PB3, 11);  /* i2c1_sda_m0 (PMIC) */
      DEBUG ((DEBUG_INFO, "NanoPi M5: I2cIomux I2C1 (PMIC) done\n"));
      break;
    case 2:
      GpioPinSetFunction (0, GPIO_PIN_PB7, 9);   /* i2c2_scl_m0 (RTC) */
      GpioPinSetFunction (0, GPIO_PIN_PC0, 9);   /* i2c2_sda_m0 (RTC) */
      DEBUG ((DEBUG_INFO, "NanoPi M5: I2cIomux I2C2 (RTC) done\n"));
      break;
    default:
      break;
  }
}

/*
 * UsbPortPowerEnable — USB host power
 * GPIO0 PD3 active-high: vcc_5v0_host (same as ROCK 4D)
 */
VOID
EFIAPI
UsbPortPowerEnable (
  VOID
  )
{
  GpioPinWrite (0, GPIO_PIN_PD3, TRUE);
  GpioPinSetDirection (0, GPIO_PIN_PD3, GPIO_PIN_OUTPUT);
  DEBUG ((DEBUG_INFO, "NanoPi M5: USB HOST 5V enabled (GPIO0 PD3)\n"));
}

/*
 * Usb2PhyResume — USB2 PHY wakeup + USB clock enable
 * Identical to ROCK 4D (same RK3576 USB2PHY layout: u2phy0 + u2phy1).
 */
VOID
EFIAPI
Usb2PhyResume (
  VOID
  )
{
  /* Ungate all USB clocks in CLKGATE_CON(47) */
  MmioWrite32 (CRU_CLKGATE_CON(47), 0xFFFF0000);

  /* Deassert SIDDQ (power on PHY analog block) */
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x20000000);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x20000000);

  /* First PHY reset (10 µs assert, then 150 µs wait) */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);
  MicroSecondDelay (150);

  /* HS signal quality tuning */
  MmioWrite32 (USB2PHY0_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY1_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x00180010);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x00180010);

  /* Clear phy_sus (exit USB suspend mode) */
  MmioWrite32 (USB2PHY0_BASE + 0x0000, 0x01FF0000);
  MmioWrite32 (USB2PHY1_BASE + 0x0000, 0x01FF0000);

  /* Second PHY reset after phy_sus clear */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);
  MicroSecondDelay (2000);

  /* Enable USB OTG 5V: GPIO2 PD2 (same as ROCK 4D) */
  GpioPinWrite (2, GPIO_PIN_PD2, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PD2, GPIO_PIN_OUTPUT);

  DEBUG ((DEBUG_INFO, "NanoPi M5: Usb2PhyResume done\n"));
}

/*
 * PcieIoInit / PciePowerEn / PciePeReset — PCIe GPIO setup
 *
 * TODO: Verify NanoPi M5 PCIe power and reset GPIO from schematic.
 * Using ROCK 4D defaults as placeholders:
 *   power: GPIO2 PD3 (vcc_3v3_pcie active-high)
 *   reset: GPIO2 PB4 (pcie0 reset active-high)
 */
VOID
EFIAPI
PcieIoInit (
  UINT32  Segment
  )
{
  switch (Segment) {
    case 0:
      GpioPinSetDirection (2, GPIO_PIN_PD3, GPIO_PIN_OUTPUT);
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
    case 0:
      GpioPinWrite (2, GPIO_PIN_PD3, Enable);  /* TODO: verify pin */
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
    case 0:
      GpioPinWrite (2, GPIO_PIN_PB4, Enable);  /* TODO: verify pin */
      break;
    default:
      break;
  }
}

/*
 * HdmiTxIomux — HDMI TX clock ungating
 * Same CRU layout as ROCK 4D (shared RK3576 VOP2 + HDPTX IP).
 */
VOID
EFIAPI
HdmiTxIomux (
  IN UINT32  Id
  )
{
  switch (Id) {
    case 0:
      /* VOP Root Clocks: CLKGATE_CON(61) */
      MmioWrite32 (CRU_CLKGATE_CON(61), 0x3F0D0000 | 0x0000);
      /* VOP VP1/VP2: CLKGATE_CON(62) */
      MmioWrite32 (CRU_CLKGATE_CON(62), 0x00030000 | 0x0000);
      /* VO0 Root Clocks: CLKGATE_CON(63) */
      MmioWrite32 (CRU_CLKGATE_CON(63), 0x000B0000 | 0x0000);
      /* HDMI TX Clocks: CLKGATE_CON(64) */
      MmioWrite32 (CRU_CLKGATE_CON(64), 0x03800000 | 0x0000);
      DEBUG ((DEBUG_INFO, "NanoPi M5: HdmiTxIomux VOP+HDMI clocks ungated\n"));
      break;
    default:
      break;
  }
}

VOID
EFIAPI
PwmFanIoSetup (
  VOID
  )
{
  /* NanoPi M5: no onboard fan connector in current UEFI port */
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
 *
 * NanoPi M5 has 3 LEDs:
 *   SYS  LED: GPIO0 PB4, active-high (green power/activity LED)
 *   LED1:     GPIO0 PC5, active-low  (user LED 1) — TODO: verify from schematic
 *   LED2:     GPIO0 PC4, active-low  (user LED 2) — TODO: verify from schematic
 *
 * ROCK 4D has 2 LEDs at GPIO0 PB4 and GPIO0 PC4.
 * NanoPi M5 adds a third at GPIO0 PC5 (to be confirmed from hardware docs).
 */
VOID
EFIAPI
PlatformInitLeds (
  VOID
  )
{
  /* SYS LED: GPIO0 PB4 active-high — keep off initially */
  GpioPinWrite (0, GPIO_PIN_PB4, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PB4, GPIO_PIN_OUTPUT);

  /* LED1: GPIO0 PC5 active-low — keep off initially (drive HIGH) */
  GpioPinWrite (0, GPIO_PIN_PC5, TRUE);
  GpioPinSetDirection (0, GPIO_PIN_PC5, GPIO_PIN_OUTPUT);

  /* LED2: GPIO0 PC4 active-low — keep off initially (drive HIGH) */
  GpioPinWrite (0, GPIO_PIN_PC4, TRUE);
  GpioPinSetDirection (0, GPIO_PIN_PC4, GPIO_PIN_OUTPUT);
}

VOID
EFIAPI
PlatformSetStatusLed (
  IN BOOLEAN  Enable
  )
{
  /* SYS LED: GPIO0 PB4 active-high */
  GpioPinWrite (0, GPIO_PIN_PB4, Enable);
}

/*
 * PlatformEarlyInit — Early board initialization
 * Enable WiFi/BT power for M.2 E-Key SDIO module.
 * Using same GPIO as ROCK 4D — both boards share a similar M.2 E-Key layout.
 *   vcc_3v3_wifi: GPIO2 PC7 active-high
 *   WiFi rfkill:  GPIO2 PD1 active-high
 * TODO: verify from NanoPi M5 schematic if different.
 */
VOID
EFIAPI
PlatformEarlyInit (
  VOID
  )
{
  /* Enable WiFi/BT power supply: GPIO2 PC7 */
  GpioPinWrite (2, GPIO_PIN_PC7, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PC7, GPIO_PIN_OUTPUT);

  /* Enable WiFi (rfkill deassert): GPIO2 PD1 */
  GpioPinWrite (2, GPIO_PIN_PD1, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PD1, GPIO_PIN_OUTPUT);

  DEBUG ((DEBUG_INFO, "NanoPi M5: PlatformEarlyInit done\n"));
}

/*
 * PlatformGetDtbFileGuid — Return GUID for embedded DTB based on compat mode.
 * GUIDs must match the FILE_GUID values in DeviceTree/Vendor.inf (and
 * DeviceTree/Mainline.inf if/when a mainline DTS is added).
 */
CONST EFI_GUID *
EFIAPI
PlatformGetDtbFileGuid (
  IN UINT32  CompatMode
  )
{
  STATIC CONST EFI_GUID VendorDtbFileGuid = {
    /* DeviceTree/Vendor.inf FILE_GUID: e5f6a7b8-c9d0-4e1f-2a3b-4c5d6e7f8091 */
    0xe5f6a7b8, 0xc9d0, 0x4e1f, { 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x7f, 0x80, 0x91 }
  };

  /*
   * Mainline DTB: not yet available — rk3576-nanopi-m5.dts is not in
   * mainline Linux as of May 2026. Return NULL so the FDT path falls back
   * to ACPI mode when FDT_COMPAT_MODE_MAINLINE is selected.
   * TODO: add Mainline.inf and a mainline GUID when DTS is upstreamed.
   */

  switch (CompatMode) {
    case FDT_COMPAT_MODE_VENDOR:
      return &VendorDtbFileGuid;
    case FDT_COMPAT_MODE_MAINLINE:
      return NULL;  /* No mainline DTS yet */
  }

  return NULL;
}
