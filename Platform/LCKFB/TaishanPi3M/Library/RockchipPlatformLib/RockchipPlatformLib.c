/** @file
 *
 *  RockchipPlatformLib.c -- board glue for the LCKFB TaishanPi 3M (RK3576)
 *
 *  Written from the ArmSoM CM5-IO library and the board's mainline device
 *  tree (arch/arm64/boot/dts/rockchip/rk3576-lckfb-tspi-3m.dts, Linux 7.3).
 *  Every pin below names the device tree line it was taken from.  First run
 *  on the board 2026-11-12: the PMIC, SD, eMMC, GMAC, USB and HDMI paths all
 *  executed and the firmware reached the front page.  PCIe, the LED and the
 *  USB-A power switches ran but had nothing attached to show they work.
 *
 *  Pins that differ from the CM5-IO:
 *    - USB-A 5V:      GPIO2 PB5 (USB 2.0 ports) and GPIO0 PC3 (USB 3.0 port)
 *    - USB-C VBUS:    GPIO0 PB6, left alone here (see Usb2PhyResume)
 *    - SD power:      none.  GPIO0 PB6 is the USB-C VBUS switch on this
 *                     board, not SDMMC0_PWREN, and must not be muxed to it.
 *    - mini-PCIe 3V3: GPIO1 PD5 active-high
 *    - PCIe PERST#:   GPIO2 PB1, as on the CM5-IO
 *    - GMAC0 PHY:     no 25 MHz reference from the SoC in the device tree
 *    - HDMI:          GPIO4 PC6 enables the level shifters
 *    - LED:           GPIO0 PA0 active-low
 *
 *  Copyright (c) 2021, Rockchip Limited.
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>
#include <Library/PcdLib.h>
#include <Library/RK806.h>
#include <Library/Rk3576PwmLib.h>
#include <Library/TimerLib.h>
#include <Soc.h>
#include <VarStoreData.h>

/*
 * RK806 PMIC voltage init for the TaishanPi 3M.
 *
 * Only the two CPU rails are written.  RK3576Dxe raises both clusters to
 * 1608 MHz over SCMI, and mainline's OPP tables in rk3576.dtsi want 750 mV
 * (A53) and 737.5 mV (A72) for that rate; 800 mV leaves 50 mV of margin and
 * is inside the 550-950 mV range the board's device tree allows on both.
 *
 * Every other rail stays at what the PMIC and the SPL left.  The CM5-IO table
 * this file started from also writes the logic, analogue and DDR-side rails.
 * Those values have not been checked against this board's schematic, and a
 * wrong one on a DDR rail is not something to find out by flashing.  Add
 * rails here one at a time, each with its device tree line.
 *
 *   DCDC1 -> vdd_cpu_big_s0  (A72, 550-950 mV)
 *   DCDC3 -> vdd_cpu_lit_s0  (A53, 550-950 mV)
 */
static struct regulator_init_data rk806_init_data[] = {
  RK8XX_VOLTAGE_INIT (MASTER_BUCK1, 800000),   /* vdd_cpu_big_s0 (A72) */
  RK8XX_VOLTAGE_INIT (MASTER_BUCK3, 800000),   /* vdd_cpu_lit_s0 (A53) */
};

/*
 * SdmmcIoMux — SD card iomux (sdmmc: mmc@2a310000)
 *
 * From rk3576-pinctrl.dtsi (sdmmc0 groups, all function 1):
 *   sdmmc0_bus4:  GPIO2 PA0-PA3  (D0-D3)
 *   sdmmc0_cmd:   GPIO2 PA4      (CMD)
 *   sdmmc0_clk:   GPIO2 PA5      (CLK)
 *   sdmmc0_det:   GPIO0 PA7      (hardware DETN, function 1)
 *
 * No sdmmc0_pwren.  The device tree says "GPIO0_B6/SDMMC0_PWREN is not
 * connected on this board" and uses GPIO0 PB6 for the USB-C VBUS switch, so
 * muxing it to the SD controller would hand that switch to the SD block.
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
  /* sdmmc0_det: GPIO0 PA7 function 1 */
  GpioPinSetFunction (0, GPIO_PIN_PA7, 1);

  DEBUG ((DEBUG_INFO, "TSPI-3M SdmmcIoMux: SDMMC0 clocks ungated + iomux set\n"));
}

/*
 * SdhciEmmcIoMux — eMMC iomux (sdhci: mmc@2a330000)
 *
 * Onboard eMMC.
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

  DEBUG ((DEBUG_INFO, "TSPI-3M SdhciEmmcIoMux: eMMC clocks ungated + iomux set\n"));
}

/*
 * Rk806SpiIomux -- the RK806 is on I2C1 here, not SPI.  No-op.
 */
VOID
EFIAPI
Rk806SpiIomux (
  VOID
  )
{
  /* RK806 is on I2C1.  No SPI iomux needed. */
}

VOID
EFIAPI
Rk806Configure (
  VOID
  )
{
  UINTN  RegCfgIndex;

  RK806Init ();

  /* dvs1 -> null, as rk806_dvs1_null in the device tree */
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
  DEBUG ((DEBUG_INFO, "TSPI-3M NorFspiIomux: SFC0 @ 0x2A340000 (SPL iomux retained)\n"));
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

  DEBUG ((DEBUG_INFO, "TSPI-3M NorFspiEnableClock: CLKGATE_CON(33) bits 0,1,6,7 ungated\n"));
}

/*
 * GmacIomux -- GMAC0 pins, eth0 M0 group.
 *
 * From the device tree's &gmac0 pinctrl-0 and rk3576-pinctrl.dtsi:
 *   eth0m0_miim:       GPIO3_PA5 (MDIO), GPIO3_PA6 (MDC)
 *   eth0m0_rx_bus2:    GPIO3_PA7 (RXCTL), GPIO3_PB2 (RXD0), GPIO3_PB1 (RXD1)
 *   eth0m0_tx_bus2:    GPIO3_PB3 (TXCTL), GPIO3_PB5 (TXD0), GPIO3_PB4 (TXD1)
 *   eth0m0_rgmii_clk:  GPIO3_PD1 (RXCLK), GPIO3_PB6 (TXCLK)
 *   eth0m0_rgmii_bus:  GPIO3_PD3 (RXD2), GPIO3_PD2 (RXD3), GPIO3_PC3 (TXD2), GPIO3_PC2 (TXD3)
 *   eth0m0_mclk:       GPIO3_PB0 (125 MHz in, clock_in_out = "input")
 *
 * The CM5-IO's YT8531C has no crystal and takes a 25 MHz reference from the
 * SoC on GPIO3_PA4.  This board's device tree does not reference
 * ethm0_clk0_25m_out, so that block is not carried over.  Check the schematic
 * for the PHY's crystal before relying on this.
 *
 * The PHY is a Maxio MAE0621A-Q3C, ID 7b74.4412, with its own 25 MHz crystal
 * (PCB v1.4).  GmacPlatformDxe's MaxioPhy.c recognises it and keeps its strap
 * defaults: RX delay on, TX delay off, address 1.  Its CLKOUT pin drives
 * eth0m0_mclk, but GmacSetToRgmii() takes the MAC TX clock from the CRU, as
 * LCKFB's vendor DTS does (clock_in_out = "output"); mainline's "input"
 * instead uses CLKOUT and needs it at 125 MHz.  Whether the link comes up in
 * UEFI like this is not known.
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

  /* eth0m0_mclk */
  GpioPinSetFunction (3, GPIO_PIN_PB0, 3);
}

/*
 * GmacIoPhyReset — Assert/deassert ETH PHY reset GPIO.
 *
 * gmac0 PHY: GPIO2_PB3, GPIO_ACTIVE_LOW
 *   (&mdio0 rgmii_phy0: reset-gpios = <&gpio2 RK_PB3 GPIO_ACTIVE_LOW>)
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
 * I2cIomux -- I2C pin configuration
 * From rk3576-pinctrl.dtsi:
 *   i2c1m0: GPIO0 PB2 (fn 11) = SCL, GPIO0 PB3 (fn 11) = SDA  (RK806 PMIC)
 *   i2c2m0: GPIO0 PB7 (fn  9) = SCL, GPIO0 PC0 (fn  9) = SDA  (HYM8563 RTC, FUSB302)
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
      DEBUG ((DEBUG_INFO, "TSPI-3M I2cIomux: I2C1 (PMIC) GPIO0 PB2/PB3 fn11\n"));
      break;
    case 2:
      GpioPinSetFunction (0, GPIO_PIN_PB7, 9);
      GpioPinSetFunction (0, GPIO_PIN_PC0, 9);
      DEBUG ((DEBUG_INFO, "TSPI-3M I2cIomux: I2C2 (RTC) GPIO0 PB7/PC0 fn9\n"));
      break;
    default:
      break;
  }
}

/*
 * UsbPortPowerEnable -- 5V to the USB-A ports.
 *
 *   vcc_5v0_usb2_host: gpio = <&gpio2 RK_PB5 GPIO_ACTIVE_HIGH>  (two USB 2.0 ports)
 *   vcc_5v0_usb3_host: gpio = <&gpio0 RK_PC3 GPIO_ACTIVE_HIGH>  (USB 3.0 port and one USB 2.0 port)
 *
 * All four ports sit behind the onboard hub on DRD1.
 */
VOID
EFIAPI
UsbPortPowerEnable (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "TSPI-3M UsbPortPowerEnable: GPIO2 PB5 and GPIO0 PC3 high (USB-A 5V)\n"));
  GpioPinWrite (2, GPIO_PIN_PB5, TRUE);
  GpioPinSetDirection (2, GPIO_PIN_PB5, GPIO_PIN_OUTPUT);
  GpioPinWrite (0, GPIO_PIN_PC3, TRUE);
  GpioPinSetDirection (0, GPIO_PIN_PC3, GPIO_PIN_OUTPUT);
}

/*
 * UsbTypeCGetOrientation -- which way round the USB-C plug is.
 *
 * This board's USB-C port (DRD0) has an FUSB302 at 0x22 on I2C2, as the
 * CM5-IO has on I2C0, but nothing here reads it: the port's VBUS switch
 * stays off (see Usb2PhyResume) and XHC0 is not in this board's DSDT, so no
 * OS driver uses the SuperSpeed lanes the answer would set up.  Returning
 * Unknown makes UsbHcd try the unflipped and then the flipped lane mapping,
 * as on the ROCK 4D.
 */
USB_TYPEC_ORIENTATION
EFIAPI
UsbTypeCGetOrientation (
  IN UINT32  Port
  )
{
  return UsbTypeCOrientationUnknown;
}

/*
 * Usb2PhyResume — USB2 PHY wakeup + USB clock enable
 *
 * Same RK3576 USB2 PHY sequence as the CM5-IO and the ROCK 4D.
 *
 * The USB-C VBUS switch (vcc_5v0_typec, GPIO0 PB6) is deliberately not
 * turned on.  That port is also the MaskROM and flashing port, so it is often
 * plugged into a PC, and Linux only sources VBUS there after the FUSB302 has
 * seen a sink attach.  The firmware has no Type-C port manager, so it has no
 * way to know what is on the other end.
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

  DEBUG ((DEBUG_INFO,
    "TSPI-3M Usb2PhyResume: SIDDQ off, 1st reset, HS tuning, phy_sus off, "
    "2nd reset+2ms, USB-C VBUS left off\n"));
}

/*
 * PcieIoInit — PCIe GPIO direction setup
 *
 * pcie0 is the mini-PCIe slot, shared with SATA and the EC20's USB:
 *   power:  GPIO1 PD5 (vcc_3v3_minipcie, active-high)
 *   reset:  GPIO2 PB1 (LCKFB's pcie include: reset-gpios = <&gpio2 RK_PB1 GPIO_ACTIVE_HIGH>)
 *
 * The mainline device tree leaves pcie0 disabled; the slot's mode is chosen
 * by an overlay in the OS.
 */
VOID
EFIAPI
PcieIoInit (
  UINT32  Segment
  )
{
  switch (Segment) {
    case 0:
      /* mini-PCIe 3V3: GPIO1 PD5 */
      GpioPinSetDirection (1, GPIO_PIN_PD5, GPIO_PIN_OUTPUT);
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
    case 0:  /* GPIO1 PD5 -- vcc_3v3_minipcie */
      GpioPinWrite (1, GPIO_PIN_PD5, Enable);
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
    case 0:  /* GPIO2 PB1 — pcie0 reset (active-high) */
      //
      // PERST# is active low: the pin is driven LOW to hold the endpoint in
      // reset and HIGH to release it, so Enable ("assert reset") maps to LOW.
      // This used to write Enable straight through, which inverted the signal:
      // "assert" released the device and "release" drove it back into reset.
      // Every other reset line in this file already uses !Enable.
      //
      // Mainline is unambiguous -- pcie-dw-rockchip requests the GPIO as
      // GPIOD_OUT_HIGH and rockchip_pcie_start_link() writes 0 under the
      // comment "Reset device", then 1 to release.
      //
      // Measured on the CM5-IO (same pin and polarity) 2026-09-17: with the inversion the link trained to
      // Recovery (LTSSM 0x0013000D) then collapsed to Polling.Compliance
      // (0x00000003) within 50 ms -- exactly when the code believed it was
      // releasing PERST# and was in fact asserting it.  The endpoint's config
      // space read 0xFFFFFFFF at the instant of link-up, so the link was never
      // usable.
      //
      GpioPinWrite (2, GPIO_PIN_PB1, !Enable);
      break;
    default:
      break;
  }
}

/*
 * HdmiTxIomux — HDMI TX pin mux & clock ungating
 *
 * GPIO4 PC6 enables the external level shifters.  The device tree has
 * "frl-enable-gpios = <&gpio4 RK_PC6 GPIO_ACTIVE_LOW>" with the comment "The
 * external level shifters must be on for TMDS operation", and LCKFB's vendor
 * tree has "enable-gpios = <&gpio4 RK_PC6 GPIO_ACTIVE_HIGH>".  Both mean the
 * same thing for TMDS: drive the pin high.
 *
 * The CRU clock ungating and reset deasserts are the SoC's and are the same
 * as on the CM5-IO and the ROCK 4D.
 */
VOID
EFIAPI
HdmiTxIomux (
  IN UINT32  Id
  )
{
  switch (Id) {
    case 0:
      GpioPinWrite (4, GPIO_PIN_PC6, TRUE);
      GpioPinSetDirection (4, GPIO_PIN_PC6, GPIO_PIN_OUTPUT);
      DEBUG ((DEBUG_INFO, "TSPI-3M HdmiTxIomux: GPIO4 PC6 high (level shifters on)\n"));
      MicroSecondDelay (50 * 1000);

      /* CLKGATE_CON(61): VOP Root + VP clocks (ungate all) */
      MmioWrite32 (CRU_CLKGATE_CON(61), 0x3F0D0000 | 0x0000);

      /* CLKGATE_CON(62): DCLK_VP1, DCLK_VP2 */
      MmioWrite32 (CRU_CLKGATE_CON(62), 0x00030000 | 0x0000);

      /*
       * CLKGATE_CON(63): VO0 root clocks, plus ACLK/HCLK/PCLK_HDCP0.
       *
       * The HDCP0 clocks (bits 12/13/14) are not here for HDCP -- they are in
       * PD_VO0's devicetree clock list (rk3576.dtsi:1247), and Rockchip's
       * pm-domains driver enables a domain's clocks across the power-up
       * handshake (pm-domains.c:666).  The HDCP0 bus interface sits inside
       * PD_VO0, so with those clocks gated the interconnect has nothing to
       * answer the NIU idle-exit handshake with.
       *
       * The vendor UEFI build that produces a stable picture on ROCK 4D
       * writes exactly this value here.
       */
      MmioWrite32 (CRU_CLKGATE_CON(63), 0x700B0000 | 0x0000);

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

      /*
       * Deassert the HDMI TX software resets.
       *
       * These three used to be CRU SOFTRST_CON22 bit 6, CON28 bit 5 and
       * CON25 bit 5, derived by taking mainline's reset *index* and applying
       * the vendor binding's ID/16 encoding to it -- two incompatible
       * numbering schemes (the same mistake the HDPTX PHY resets carried;
       * see the note in DwHdmiQpLib.h).  Per rst-rk3576.c those addresses are
       * DDR and NPU reset registers, and the resets meant here are:
       *
       *   SRST_HDMITX0_REF        CRU     SOFTRST_CON64 bit 9   (:476)
       *   SRST_LINKSYM_HDMITXPHY0 CRU     SOFTRST_CON75 bit 1   (:537)
       *   SRST_HDMITXHDP          PMU1CRU SOFTRST_CON01 bit 13  (:599)
       *
       * SRST_HDMITXHDP gates the hot-plug-detect block.  CLK_HDMITXHDP is
       * ungated above; the reset was never released, which is a candidate
       * for ROCK 4D reading HPD low while DDC -- a separate block -- works.
       */
      MmioWrite32 (CRU_SOFTRST_CON (64), (BIT (9) << 16) | 0U);
      MmioWrite32 (CRU_SOFTRST_CON (75), (BIT (1) << 16) | 0U);
      MmioWrite32 (PMU1CRU_BASE + 0xA04, (BIT (13) << 16) | 0U);

      MicroSecondDelay (5 * 1000);
      DEBUG ((DEBUG_INFO,
        "TSPI-3M HdmiTxIomux: VOP, HDMI and HDPTX APB clocks ungated, resets deasserted\n"));
      break;
    default:
      break;
  }
}

/*
 * PwmFanIoSetup / PwmFanSetSpeed -- the 2-pin fan on CN2.
 *
 * Wiring, from three sources that agree:
 *   - LCKFB's vendor tree, rk3576-lckfb-tspi-3m.dts:164-188:
 *       fan: pwm-fan { pwms = <&pwm2_8ch_3 0 500000 0>;
 *                      cooling-levels = <0 100 150 200 255>; }
 *       &pwm2_8ch_3 { pinctrl-0 = <&pwm2m3_ch3>; }
 *   - Armbian's mainline tree for this board,
 *     board-lckfb-tspi-3m-01-onboard-hardware.patch:58-64 and :238-242,
 *     the same pwms cell and pinctrl, tested on the board with 7.3.3.
 *   - The schematic (LCEDA project v1.4, page 31 "RTC和FAN") has net
 *     PWM2_CH3_M3, VCC5V0_SYS, an MMST3904 NPN (Q13), an SK2301 P-MOSFET
 *     (Q12), a 1N4148 (D60) and the 2-pin connector CN2: the usual NPN
 *     driving a high-side P-MOSFET, in which a high pin turns the fan on.
 *     The parts list was read from the project file; the wires between them
 *     were not traced, so the device trees are the authority on polarity.
 *
 * So: PWM2 channel 3 (pwm@2ade3000), polarity flag 0 = PWM_POLARITY_NORMAL,
 * i.e. the pin is high for the duty part of the period.  The device trees
 * use a 500000 ns period (2 kHz); the firmware's is PcdPwmFanPeriodNs, 25 kHz
 * on this board (TaishanPi3M.dsc says why and what the risk is).  Linux's
 * pwm-fan programs its own period when it probes.  The cooling levels make duty 0 the "off" level, so high = fan on.
 * This is not the CM5-IO, whose fan is inverted.
 *
 * pwm2m3_ch3 is GPIO3_D0 function 12 (rk3576-pinctrl.dtsi:2958-2961).  GPIO3
 * PD is IOC_GRF + 0x4078 (pinctrl-rockchip.c:6433), which GpioLib computes
 * as 0x4060 + (24 / 4) * 4.  The pin config there also asks for drive level
 * 2; GpioLib cannot set drive strength on RK3576, so the pad keeps its reset
 * drive.  It only feeds Q13's base resistor.
 */
#define FAN_PWM_CONTROLLER  2
#define FAN_PWM_CHANNEL     3
#define FAN_PWM_PERIOD_NS   FixedPcdGet32 (PcdPwmFanPeriodNs)
#define FAN_PWM_INVERTED    FALSE

VOID
EFIAPI
PwmFanIoSetup (
  VOID
  )
{
  /* pwm2m3_ch3: GPIO3 PD0 function 12 */
  GpioPinSetFunction (3, GPIO_PIN_PD0, 12);
}

VOID
EFIAPI
PwmFanSetSpeed (
  IN UINT32  Percentage
  )
{
  EFI_STATUS  Status;

  if (Percentage > 100) {
    Percentage = 100;
  }

  Status = Rk3576PwmStart (
             FAN_PWM_CONTROLLER,
             FAN_PWM_CHANNEL,
             FAN_PWM_PERIOD_NS,
             (UINT32)(((UINT64)FAN_PWM_PERIOD_NS * Percentage) / 100),
             FAN_PWM_INVERTED
             );
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
    "TSPI-3M PwmFanSetSpeed: %u%% on PWM2 ch3 (GPIO3 PD0): %r\n",
    Percentage,
    Status
    ));
}

/*
 * PlatformInitLeds / PlatformSetStatusLed
 *
 * One LED: led-0, gpios = <&gpio0 RK_PA0 GPIO_ACTIVE_LOW>.
 */
VOID
EFIAPI
PlatformInitLeds (
  VOID
  )
{
  GpioPinWrite (0, GPIO_PIN_PA0, TRUE);   /* off */
  GpioPinSetDirection (0, GPIO_PIN_PA0, GPIO_PIN_OUTPUT);
}

VOID
EFIAPI
PlatformSetStatusLed (
  IN BOOLEAN  Enable
  )
{
  GpioPinWrite (0, GPIO_PIN_PA0, !Enable);  /* active-low */
}

/*
 * PlatformEarlyInit -- nothing yet.
 *
 * The CM5-IO releases its WiFi and Bluetooth resets here.  On this board the
 * WiFi module also has a power switch (vcc_3v3_wl, GPIO0 PD1 active-low) and
 * a 32 kHz clock from the RTC, and Linux sequences all of it through
 * mmc-pwrseq.  The firmware has no use for WiFi, so it is left to the OS.
 */
VOID
EFIAPI
PlatformEarlyInit (
  VOID
  )
{
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
    /* No vendor DTB is built for this board yet; reserved. 5d1b2c4e-8a3f-4c6d-9e21-7b0f4a6c3d58 */
    0x5d1b2c4e, 0x8a3f, 0x4c6d, { 0x9e, 0x21, 0x7b, 0x0f, 0x4a, 0x6c, 0x3d, 0x58 }
  };
  STATIC CONST EFI_GUID MainlineDtbFileGuid = {
    /* DeviceTree/Mainline.inf FILE_GUID: a7c3e9f1-2b4d-4f86-b5a0-6d8e1c3f5b72 */
    0xa7c3e9f1, 0x2b4d, 0x4f86, { 0xb5, 0xa0, 0x6d, 0x8e, 0x1c, 0x3f, 0x5b, 0x72 }
  };

  switch (CompatMode) {
    case FDT_COMPAT_MODE_VENDOR:
      return &VendorDtbFileGuid;
    case FDT_COMPAT_MODE_MAINLINE:
      return &MainlineDtbFileGuid;
  }

  return NULL;
}
