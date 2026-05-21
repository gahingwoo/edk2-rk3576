/** @file
 *
 *  RockchipPlatformLib.c — Board-specific init for ArmSoM CM5 on Waveshare CM4-IO-BASE-A (RK3576)
 *
 *  Ported from ArmSoM CM5-IO RockchipPlatformLib.c.
 *
 *  Key differences from CM5-IO (same RK3576 SoC, different carrier):
 *    - eMMC: onboard on CM5 module (same as CM5-IO)
 *    - USB HOST 5V:  always-on from carrier (no GPIO; CM5-IO uses GPIO4_PB0)
 *    - USB OTG 5V:   always-on from carrier (no GPIO; CM5-IO uses GPIO2_PB6)
 *    - PCIe reset:   GPIO2 PB1 active-high  (same as CM5-IO; verify with BASE-A schematic)
 *    - PCIe power:   GPIO0 PC3 active-high  (same as CM5-IO; verify with BASE-A schematic)
 *    - GMAC0 reset:  GPIO2 PB3 active-low   (same as CM5-IO; verify with BASE-A schematic)
 *    - GMAC0 mode:   rgmii-rxid, tx_delay=0x21 (same as CM5-IO)
 *    - HDMI 5V:      always-on on carrier board (same as CM5-IO)
 *    - WiFi reset:   GPIO1 PC6 active-low   (on CM5 module, same for all carriers)
 *    - BT power:     GPIO1 PC7 active-high  (on CM5 module, same for all carriers)
 *    - LED:          GPIO0 PB4 (work LED on CM5 module, active-high); no carrier LEDs
 *
 *  GPIO verification note:
 *    PCIe and GbE GPIO assignments are derived from ArmSoM CM5-IO schematics.
 *    Verify against Waveshare CM4-IO-BASE-A V1.x schematic before production use.
 *
 *  Copyright (c) 2021, Rockchip Limited.
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2025, Waveshare CM4-IO-BASE-A EDK2 Port
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
 * RK806 PMIC voltage init for ArmSoM CM5 (RK3576)
 *
 * Rail mapping from rk3576-rk806.dtsi regulators section
 * (same silicon as ROCK 4D; CM5 module uses the same RK806 schematic):
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
 * SD card slot on CM4-IO-BASE-A carrier board, same RK3576 pin mapping as CM5-IO.
 * From rk3576-pinctrl.dtsi (sdmmc0 groups, all function 1):
 *   sdmmc0_bus4:  GPIO2 PA0-PA3  (D0-D3)
 *   sdmmc0_cmd:   GPIO2 PA4      (CMD)
 *   sdmmc0_clk:   GPIO2 PA5      (CLK)
 *   sdmmc0_pwren: GPIO0 PB6      (power enable)
 *   sdmmc0_det:   GPIO0 PA7      (hardware DETN — function 1)
 */
VOID
EFIAPI
SdmmcIoMux (
  VOID
  )
{
  /* Ungate SDGMAC root clock */
  MmioWrite32 (CRU_CLKGATE_CON(42),
    (1U << (0 + 16)) | (0U << 0));

  /* Ungate SDMMC0 clocks */
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (1 + 16)) | (0U << 1));
  MmioWrite32 (CRU_CLKGATE_CON(43),
    (1U << (2 + 16)) | (0U << 2));

  /* sdmmc0_bus4: GPIO2 PA0-PA3 function 1 (data D0-D3) */
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

  DEBUG ((DEBUG_INFO, "CM5WaveshareA SdmmcIoMux: SDMMC0 clocks ungated + iomux set\n"));
}

/*
 * SdhciEmmcIoMux — eMMC iomux (sdhci: mmc@2a330000)
 *
 * Onboard eMMC on CM5 module — same RK3576 pin mapping as CM5-IO.
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
  /* Ungate NVM root clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (0 + 16)) | (0U << 0));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (1 + 16)) | (0U << 1));

  /* Ungate eMMC clocks */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (8 + 16)) | (0U << 8));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (9 + 16)) | (0U << 9));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (10 + 16)) | (0U << 10));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (11 + 16)) | (0U << 11));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (12 + 16)) | (0U << 12));

  /* emmc_bus8: GPIO1 PA0-PA7 function 1 */
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

  DEBUG ((DEBUG_INFO, "CM5WaveshareA SdhciEmmcIoMux: eMMC clocks ungated + iomux set\n"));
}

/*
 * Rk806SpiIomux — RK806 uses I2C on CM5 module (not SPI). No-op.
 */
VOID
EFIAPI
Rk806SpiIomux (
  VOID
  )
{
  /* CM5: RK806 is connected via I2C1. No SPI iomux needed. */
}

VOID
EFIAPI
Rk806Configure (
  VOID
  )
{
  UINTN  RegCfgIndex;

  RK806Init ();

  /* dvs1 → null (no DVS) */
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
 * Same SFC0 on RK3576. SPL-initialized iomux is retained.
 */
VOID
EFIAPI
NorFspiIomux (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "CM5WaveshareA NorFspiIomux: SFC0 @ 0x2A340000 (SPL iomux retained)\n"));
}

VOID
EFIAPI
NorFspiEnableClock (
  UINT32  *CruBase
  )
{
  /*
   * SFC0 (spi@2a340000) clock enable.
   * NVM domain: HCLK_NVM_ROOT (bit 0) + ACLK_NVM_ROOT (bit 1) in CON(33).
   * SFC0:  SCLK_FSPI_X2 (bit 6) + HCLK_FSPI (bit 7) in CON(33).
   */
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (0 + 16)) | (0U << 0));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (1 + 16)) | (0U << 1));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (6 + 16)) | (0U << 6));
  MmioWrite32 (CRU_CLKGATE_CON(33),
    (1U << (7 + 16)) | (0U << 7));

  DEBUG ((DEBUG_INFO, "CM5WaveshareA NorFspiEnableClock: CLKGATE_CON(33) bits 0,1,6,7 ungated\n"));
}

/*
 * GmacIomux — GMAC iomux for CM4-IO-BASE-A
 *
 * gmac0: RTL8211F in rgmii-rxid mode, eth0 M0 pins (same RK3576 pinout as CM5-IO).
 * From rk3576-pinctrl.dtsi eth0m0_* groups.
 * gmac1: not connected on CM5 carriers.
 */
VOID
EFIAPI
GmacIomux (
  IN UINT32  Id
  )
{
  if (Id != 0) {
    return;
  }

  /* eth0m0_miim */
  GpioPinSetFunction (3, GPIO_PIN_PA5, 3);
  GpioPinSetFunction (3, GPIO_PIN_PA6, 3);

  /* eth0m0_rx_bus2 */
  GpioPinSetFunction (3, GPIO_PIN_PA7, 3);
  GpioPinSetFunction (3, GPIO_PIN_PB2, 3);
  GpioPinSetFunction (3, GPIO_PIN_PB1, 3);

  /* eth0m0_tx_bus2 */
  GpioPinSetFunction (3, GPIO_PIN_PB3, 3);
  GpioPinSetFunction (3, GPIO_PIN_PB5, 3);
  GpioPinSetFunction (3, GPIO_PIN_PB4, 3);

  /* eth0m0_rgmii_clk */
  GpioPinSetFunction (3, GPIO_PIN_PD1, 3);
  GpioPinSetFunction (3, GPIO_PIN_PB6, 3);

  /* eth0m0_rgmii_bus */
  GpioPinSetFunction (3, GPIO_PIN_PD3, 3);
  GpioPinSetFunction (3, GPIO_PIN_PD2, 3);
  GpioPinSetFunction (3, GPIO_PIN_PC3, 3);
  GpioPinSetFunction (3, GPIO_PIN_PC2, 3);

  /* ethm0_clk0_25m_out */
  GpioPinSetFunction (3, GPIO_PIN_PA4, 3);
}

/*
 * GmacIoPhyReset — Assert/deassert ETH PHY reset GPIO.
 *
 * gmac0 PHY: GPIO2_PB3, GPIO_ACTIVE_LOW
 *   (same as CM5-IO; verify with CM4-IO-BASE-A schematic)
 *   Enable=TRUE  → assert reset   → drive LOW  (active-low)
 *   Enable=FALSE → deassert reset → drive HIGH
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

  GpioPinSetDirection (2, GPIO_PIN_PB3, GPIO_PIN_OUTPUT);
  GpioPinWrite (2, GPIO_PIN_PB3, !Enable);  /* active-low */
}

/*
 * I2cIomux — I2C pin configuration (same as CM5-IO)
 * From rk3576-pinctrl.dtsi:
 *   i2c1m0: GPIO0 PB2 (fn 11) = SCL, GPIO0 PB3 (fn 11) = SDA  (RK806 PMIC)
 *   i2c2m0: GPIO0 PB7 (fn  9) = SCL, GPIO0 PC0 (fn  9) = SDA  (HYM8563 RTC)
 */
VOID
EFIAPI
I2cIomux (
  UINT32  id
  )
{
  switch (id) {
    case 1:
      GpioPinSetFunction (0, GPIO_PIN_PB2, 11);
      GpioPinSetFunction (0, GPIO_PIN_PB3, 11);
      DEBUG ((DEBUG_INFO, "CM5WaveshareA I2cIomux: I2C1 (PMIC) GPIO0 PB2/PB3 fn11\n"));
      break;
    case 2:
      GpioPinSetFunction (0, GPIO_PIN_PB7, 9);
      GpioPinSetFunction (0, GPIO_PIN_PC0, 9);
      DEBUG ((DEBUG_INFO, "CM5WaveshareA I2cIomux: I2C2 (RTC) GPIO0 PB7/PC0 fn9\n"));
      break;
    default:
      break;
  }
}

/*
 * UsbPortPowerEnable — USB host port power enable
 *
 * CM4-IO-BASE-A: USB HOST 5V is always-on from the carrier board.
 * The FE1.1S USB hub is powered directly from the 5V rail.
 * No GPIO control is needed (vs CM5-IO which uses GPIO4_PB0).
 */
VOID
EFIAPI
UsbPortPowerEnable (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "CM5WaveshareA UsbPortPowerEnable: USB HOST 5V always-on (no GPIO)\n"));
  /* No action needed — carrier provides always-on USB 5V to FE1.1S hub */
}

/*
 * Usb2PhyResume — USB2 PHY wakeup + USB clock enable
 *
 * Same RK3576 USB2 PHY configuration as CM5-IO.
 * USB GRF, clock and reset register addresses are identical.
 * No OTG 5V GPIO needed on CM4-IO-BASE-A (no USB-C port; always-on).
 */
VOID
EFIAPI
Usb2PhyResume (
  VOID
  )
{
  /* Step 1: Ungate ALL USB clocks in CLKGATE_CON(47) */
  MmioWrite32 (CRU_CLKGATE_CON(47), 0xFFFF0000);

  /* Step 2: Deassert SIDDQ (bit 13 in GRF+0x0010) */
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x20000000);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x20000000);

  /* Step 3: First PHY CRU hard reset */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);
  MicroSecondDelay (150);

  /* Step 4: HS signal quality tuning */
  MmioWrite32 (USB2PHY0_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY1_BASE + 0x000C, 0x0F000900);
  MmioWrite32 (USB2PHY0_BASE + 0x0010, 0x00180010);
  MmioWrite32 (USB2PHY1_BASE + 0x0010, 0x00180010);

  /* Step 5: Clear phy_sus */
  MmioWrite32 (USB2PHY0_BASE + 0x0000, 0x01FF0000);
  MmioWrite32 (USB2PHY1_BASE + 0x0000, 0x01FF0000);

  /* Step 5b: Second PHY CRU hard reset */
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030003);
  MicroSecondDelay (10);
  MmioWrite32 (CRU_SOFTRST_CON(28), 0x00030000);
  MicroSecondDelay (150);

  /* Step 5c: Wait for UTMI clock stabilization */
  MicroSecondDelay (2000);

  /*
   * No USB OTG 5V GPIO needed: CM4-IO-BASE-A has no USB-C port.
   * The USB adapter connector (DRD0) is powered by an always-on carrier rail.
   */
  DEBUG ((DEBUG_INFO,
    "CM5WaveshareA Usb2PhyResume: SIDDQ off, 1st reset, HS tuning, phy_sus off, "
    "2nd reset+2ms; no OTG 5V GPIO (always-on carrier)\n"));
}

/*
 * PcieIoInit — PCIe GPIO direction setup
 *
 * M.2 slot on CM4-IO-BASE-A:
 *   power:  GPIO0 PC3 (vcc3v3_pcie, active-high) — verify with BASE-A schematic
 *   reset:  GPIO2 PB1 (active-high in DTS)       — verify with BASE-A schematic
 */
VOID
EFIAPI
PcieIoInit (
  UINT32  Segment
  )
{
  switch (Segment) {
    case 0:
      /* PCIe power enable: GPIO0 PC3 */
      GpioPinSetDirection (0, GPIO_PIN_PC3, GPIO_PIN_OUTPUT);
      /* PCIe PERST#: GPIO2 PB1 */
      GpioPinSetDirection (2, GPIO_PIN_PB1, GPIO_PIN_OUTPUT);
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
    case 0:  /* GPIO0 PC3 — vcc3v3_pcie (verify with BASE-A schematic) */
      GpioPinWrite (0, GPIO_PIN_PC3, Enable);
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
    case 0:  /* GPIO2 PB1 — PCIe reset active-high (verify with BASE-A schematic) */
      GpioPinWrite (2, GPIO_PIN_PB1, Enable);
      break;
    default:
      break;
  }
}

/*
 * HdmiTxIomux — HDMI TX pin mux & clock ungating
 *
 * CM4-IO-BASE-A: HDMI 5V is supplied from the carrier board (always-on regulator).
 * No GPIO needs to be driven for HDMI 5V.
 *
 * All CRU clock ungating and software reset deasserts are identical to
 * CM5-IO as they address the same RK3576 SoC CRU registers.
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
       * CM4-IO-BASE-A carrier board: HDMI 5V is always-on; no enable GPIO required.
       */
      DEBUG ((DEBUG_INFO,
        "CM5WaveshareA HdmiTxIomux: HDMI 5V always-on (no GPIO needed); continuing clock setup\n"));
      MicroSecondDelay (50 * 1000);

      /* CLKGATE_CON(61): VOP Root + VP clocks (ungate all) */
      MmioWrite32 (CRU_CLKGATE_CON(61), 0x3F0D0000 | 0x0000);

      /* CLKGATE_CON(62): DCLK_VP1, DCLK_VP2 */
      MmioWrite32 (CRU_CLKGATE_CON(62), 0x00030000 | 0x0000);

      /* CLKGATE_CON(63): VO0 root clocks */
      MmioWrite32 (CRU_CLKGATE_CON(63), 0x000B0000 | 0x0000);

      /* CLKGATE_CON(64): HDMI TX clocks */
      MmioWrite32 (CRU_CLKGATE_CON(64), 0x03800000 | 0x0000);

      /* PMU1CRU: PCLK_HDPTX_APB + CLK_HDMITXHDP + PCLK_PMUPHY_ROOT */
      MmioWrite32 (PMU1CRU_BASE + 0x800,       (BIT (1) << 16) | 0U);
      MmioWrite32 (PMU1CRU_BASE + 0x800 + 4,   (BIT (13) << 16) | 0U);
      MmioWrite32 (PMU1CRU_BASE + 0x800 + 20,  (BIT (0) << 16) | 0U);

      /* Enable HDPTX PHY analog bias + BGR */
      MmioWrite32 (0x26032000U,
        ((BIT (6) | BIT (5)) << 16) | (BIT (6) | BIT (5)));
      MicroSecondDelay (100);

      /* Deassert HDMI TX software resets */
      MmioWrite32 (CRU_SOFTRST_CON (22), (0x0040U << 16) | 0U);
      MmioWrite32 (CRU_SOFTRST_CON (28), (0x0020U << 16) | 0U);
      MmioWrite32 (CRU_SOFTRST_CON (25), (0x0020U << 16) | 0U);

      MicroSecondDelay (5 * 1000);
      DEBUG ((DEBUG_INFO,
        "CM5WaveshareA HdmiTxIomux: VOP, HDMI and HDPTX APB clocks ungated, resets deasserted\n"));
      break;
    default:
      break;
  }
}

/*
 * PwmFanIoSetup / PwmFanSetSpeed
 * CM4-IO-BASE-A has a PWM fan header.  Fan control is handled by the OS kernel.
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
 *
 * CM4-IO-BASE-A: Only the CM5 module work LED (GPIO0 PB4, active-high) is used.
 * The carrier board does not expose additional status LEDs on CM5-accessible GPIOs.
 */
VOID
EFIAPI
PlatformInitLeds (
  VOID
  )
{
  /* Module work LED: GPIO0 PB4 */
  GpioPinWrite (0, GPIO_PIN_PB4, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PB4, GPIO_PIN_OUTPUT);
}

VOID
EFIAPI
PlatformSetStatusLed (
  IN BOOLEAN  Enable
  )
{
  /* Module work LED: GPIO0 PB4 active-high */
  GpioPinWrite (0, GPIO_PIN_PB4, Enable);
}

/*
 * PlatformEarlyInit — Early board initialization
 * Called before driver dispatch.
 *
 * Power on the SYN43752 WiFi/BT combo on the CM5 module.
 * From rk3576-armsom-cm5.dtsi:
 *   WiFi reset GPIO: GPIO1 PC6 (active-low) → deassert = drive HIGH
 *   BT enable GPIO:  GPIO1 PC7 (active-high)
 */
VOID
EFIAPI
PlatformEarlyInit (
  VOID
  )
{
  /*
   * WiFi: deassert reset → GPIO1 PC6 HIGH.
   */
  GpioPinWrite (1, GPIO_PIN_PC6, TRUE);
  GpioPinSetDirection (1, GPIO_PIN_PC6, GPIO_PIN_OUTPUT);

  /*
   * BT: drive GPIO1 PC7 HIGH to release BT reset / enable BT subsystem.
   */
  GpioPinWrite (1, GPIO_PIN_PC7, TRUE);
  GpioPinSetDirection (1, GPIO_PIN_PC7, GPIO_PIN_OUTPUT);

  /* WiFi wake IRQ: GPIO0 PB0 configured as input */
  GpioPinSetDirection (0, GPIO_PIN_PB0, GPIO_PIN_INPUT);

  DEBUG ((DEBUG_INFO, "CM5WaveshareA PlatformEarlyInit: WiFi reset deasserted (GPIO1 PC6), "
    "BT enabled (GPIO1 PC7)\n"));
}

/*
 * PlatformGetDtbFileGuid — Return GUID for embedded DTB based on compat mode.
 * GUIDs must match FILE_GUID in DeviceTree/Vendor.inf and DeviceTree/Mainline.inf.
 */
CONST EFI_GUID *
EFIAPI
PlatformGetDtbFileGuid (
  IN UINT32  CompatMode
  )
{
  STATIC CONST EFI_GUID VendorDtbFileGuid = {
    /* DeviceTree/Vendor.inf FILE_GUID: 2a213934-0d7d-4008-bd58-76764fa7d4ee */
    0x2a213934, 0x0d7d, 0x4008, { 0xbd, 0x58, 0x76, 0x76, 0x4f, 0xa7, 0xd4, 0xee }
  };
  STATIC CONST EFI_GUID MainlineDtbFileGuid = {
    /* DeviceTree/Mainline.inf FILE_GUID: 737a5941-8ace-4557-bc8d-8b504ddaca6d */
    0x737a5941, 0x8ace, 0x4557, { 0xbc, 0x8d, 0x8b, 0x50, 0x4d, 0xda, 0xca, 0x6d }
  };

  switch (CompatMode) {
    case FDT_COMPAT_MODE_VENDOR:
      return &VendorDtbFileGuid;
    case FDT_COMPAT_MODE_MAINLINE:
      return &MainlineDtbFileGuid;
  }

  return NULL;
}
