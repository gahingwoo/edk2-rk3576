/** @file
 *
 *  RK3576 SoC definitions for EDK2 UEFI firmware
 *
 *  Addresses verified from:
 *    linux/arch/arm64/boot/dts/rockchip/rk3576.dtsi (mainline)
 *    u-boot/dts/upstream/src/arm64/rockchip/rk3576.dtsi
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#ifndef __SOC_H__
#define __SOC_H__

#ifdef __cplusplus
extern "C" {
#endif

#define uint8_t   UINT8
#define uint16_t  UINT16
#define uint32_t  UINT32
#define uint64_t  UINT64
#define HAL_ASSERT      ASSERT
#define HAL_CPUDelayUs  MicroSecondDelay
#define __WEAK
#define HAL_DivU64  DivU64x32

#ifdef __cplusplus
#define   __I  volatile
#else
#define   __I  volatile const
#endif
#define     __O   volatile
#define     __IO  volatile

#define WRITE_REG(REG, VAL)  ((*(volatile uint32_t *)&(REG)) = (VAL))
#define READ_REG(REG)        ((*(volatile const uint32_t *)&(REG)))
#define DIV_ROUND_UP(x, y)   (((x) + (y) - 1) / (y))

/* ---------------------------------------------------------------
 * Reuse Rockchip IP block register layouts (struct FSPI_REG,
 * struct UART_REG, struct CRU_REG, FSPI_*_SHIFT/MASK, ...).
 * The IP blocks themselves are shared across RK3568/RK3576/RK3588;
 * only the base addresses differ.  We pull in the RK3588 SoC header
 * for the typedefs / register-bit macros, then `#undef` the few
 * RK3588-specific base addresses that clash with RK3576's own map
 * before re-defining them below.
 * --------------------------------------------------------------- */
#include "../../RK3588/Include/RK3588.h"

/* RK3588 base macros that collide with RK3576's memory map.
 * They are `#undef`'d here and re-defined further down with the
 * RK3576 addresses, so the instance macros (FSPI, UART0, CRU, ...)
 * declared in RK3588.h end up pointing at the right MMIO window.
 */
#undef CRU_BASE
#undef UART0_BASE
#undef UART2_BASE
#undef FSPI_BASE
#undef PMU1CRU_BASE
/*
 * RK3576 PMU1CRU is a sub-domain inside the unified CRU at 0x27200000,
 * located at +0x20000.  Confirmed by mainline Linux:
 *   drivers/clk/rockchip/rst-rk3576.c:
 *     #define RK3576_PMU1CRU_RESET_OFFSET(...) [id] = (0x20000*4 + ...)
 *     ("0x27220000 + 0x0A00" comment in driver)
 */
#define PMU1CRU_BASE  0x27220000UL

/* ============================================================
 *  RK3576 Peripheral Memory Map
 *  All addresses from rk3576.dtsi (verified)
 * ============================================================ */

/* Debug UART — stdout-path = "serial0:1500000n8" in board DTS */
#define UART0_BASE    0x2AD40000UL
#define UART2_BASE    0x2AD50000UL
#define UART_CLOCK    24000000UL      /* 24 MHz OSC */
#define UART_BAUD     1500000UL

/* eMMC — sdhci: mmc@2a330000 */
#define EMMC_BASE     0x2A330000UL

/* SD card — sdmmc: mmc@2a310000 */
#define SDMMC_BASE    0x2A310000UL

/* SPI NOR Flash Controller — sfc0: spi@2a340000 */
#define SFC0_BASE     0x2A340000UL
#define SFC1_BASE     0x2A300000UL

/* Alias used by RK3588.h's `FSPI` instance macro — point it at
 * RK3576's SFC0 so the shared FspiLib HAL works unmodified. */
#define FSPI_BASE     SFC0_BASE

/* PCIe — pcie0: pcie@22000000 (M.2 on ROCK 4D) */
#define PCIE0_BASE    0x22000000UL
#define PCIE1_BASE    0x22400000UL

/* GPIO — gpio0..4 */
#define GPIO0_BASE    0x27320000UL
#define GPIO1_BASE    0x2AE10000UL
#define GPIO2_BASE    0x2AE20000UL
#define GPIO3_BASE    0x2AE30000UL
#define GPIO4_BASE    0x2AE40000UL

/* I2C — i2c0..9 */
#define I2C0_BASE     0x27300000UL   /* PMU domain */
#define I2C1_BASE     0x2AC40000UL   /* PMIC RK806 */
#define I2C2_BASE     0x2AC50000UL   /* HYM8563 RTC */
#define I2C3_BASE     0x2AC60000UL
#define I2C4_BASE     0x2AC70000UL
#define I2C5_BASE     0x2AC80000UL
#define I2C6_BASE     0x2AC90000UL
#define I2C7_BASE     0x2ACA0000UL
#define I2C8_BASE     0x2ACB0000UL
#define I2C_COUNT     9

#define I2C_BASE(id)  ((id == 0) ? I2C0_BASE :                          \
                       (id <= 8) ? (I2C1_BASE + ((id - 1) * 0x10000)) : \
                       0)

/* CRU — cru: clock-controller@27200000 */
#define CRU_BASE      0x27200000UL

/*
 * CRU register offset macros (from drivers/clk/rockchip/clk-rk3576.c)
 * CLKGATE_CON: clock gate control, offset 0x800, 4 bytes per register
 * CLKSEL_CON: clock select/divider, offset 0x300
 * SOFTRST_CON: soft reset control, offset 0xA00
 *
 * Rockchip write-enable convention:
 *   bits[31:16] = write-enable mask (1 = allow write to corresponding bit)
 *   bits[15:0]  = value (1 = gate clock / assert reset)
 */
#define CRU_CLKGATE_CON(n)  (CRU_BASE + 0x800 + (n) * 4)
#define CRU_CLKSEL_CON(n)   (CRU_BASE + 0x300 + (n) * 4)
#define CRU_SOFTRST_CON(n)  (CRU_BASE + 0xA00 + (n) * 4)

/* PHP CRU domain (PCIe/SATA combo PHY clocks) — offset 0x8000 from CRU_BASE */
#define PHP_CRU_BASE             (CRU_BASE + 0x8000)
#define PHP_CRU_CLKGATE_CON(n)   (PHP_CRU_BASE + 0x800 + (n) * 4)
#define PHP_CRU_CLKSEL_CON(n)   (PHP_CRU_BASE + 0x300 + (n) * 4)
#define PHP_CRU_SOFTRST_CON(n)   (PHP_CRU_BASE + 0xA00 + (n) * 4)

/*
 * Naneng ComboPHY MMIO
 *   combphy0_ps  @ 0x2b050000  (PCIe0 / SATA0)
 *   combphy1_psu @ 0x2b060000  (PCIe1 / SATA1 / USB3 for DRD1)
 */
#define COMBPHY0_BASE   0x2B050000UL
#define COMBPHY1_BASE   0x2B060000UL

/*
 * Pipe-PHY GRF (per-combphy PHY mode / clock select registers)
 *   pipe_phy0_grf: syscon@26028000   for combphy0
 *   pipe_phy1_grf: syscon@2602a000   for combphy1
 */
#define PIPE_PHY0_GRF_BASE  0x26028000UL
#define PIPE_PHY1_GRF_BASE  0x2602A000UL

/*
 * PHP GRF (u3otg port-enable, pipe mux for PCIe/SATA/USB3)
 *   php_grf: syscon@26020000
 */
#define PHP_GRF_BASE  0x26020000UL

/* System GRF */
#define SYS_GRF_BASE  0x2600A000UL

/* PMU GRF */
#define PMU0_GRF_BASE 0x26024000UL
#define PMU1_GRF_BASE 0x26026000UL

/* USB DWC3 — usb_drd0: usb@23000000, usb_drd1: usb@23400000 */
#define USB_DRD0_BASE 0x23000000UL
#define USB_DRD1_BASE 0x23400000UL

/* USB2 PHY — accessed via usb2phy_grf syscon, NOT standalone MMIO!
 * Per mainline rk3576.dtsi:
 *   usb2phy_grf: syscon@2602e000 { reg = <0x2602e000 0x4000>; }
 *   u2phy0: usb2-phy@0    { reg = <0x0    0x10>; }  (offset within grf)
 *   u2phy1: usb2-phy@2000 { reg = <0x2000 0x10>; }  (offset within grf)
 *
 * WARNING: Previous values 0x2B000000/0x2B010000 were WRONG.
 */
#define USB2PHY_GRF_BASE  0x2602E000UL
#define USB2PHY0_OFFSET   0x0000
#define USB2PHY1_OFFSET   0x2000
#define USB2PHY0_BASE     (USB2PHY_GRF_BASE + USB2PHY0_OFFSET)
#define USB2PHY1_BASE     (USB2PHY_GRF_BASE + USB2PHY1_OFFSET)

/* GMAC — gmac0: ethernet@2a220000 (per mainline rk3576.dtsi)
 * WARNING: Previous value 0x2BD40000 was WRONG.
 */
#define GMAC0_BASE    0x2A220000UL

/* VOP2 — display controller (Video Output Processor v2)
 * vop@27d00000, compatible = "rockchip,rk3576-vop"
 * 3 Video Ports (VP0, VP1, VP2), 4 Cluster + 4 Esmart layers
 */
#define VOP_BASE      0x27D00000UL

/* HDMI TX — DW-HDMI QP (same IP as RK3588!)
 * hdmi@27da0000, compatible = "rockchip,rk3576-dw-hdmi-qp"
 */
#define HDMI_BASE     0x27DA0000UL

/* HDPTX PHY — Samsung HDPTX (compatible with RK3588)
 * hdmiphy@2b000000, compatible = "rockchip,rk3576-hdptx-phy", "rockchip,rk3588-hdptx-phy"
 *
 * NOTE: 0x2B000000 was previously INCORRECTLY used as USB2PHY0_BASE.
 * This address is actually the HDPTX PHY for HDMI output.
 */
#define HDPTX_PHY_BASE     0x2B000000UL

/* VO0 GRF — Video Output 0 General Register File
 * vo0_grf: syscon@2601a000, HDMI output control & sync polarity
 */
#define VO0_GRF_BASE       0x2601A000UL

/* HDPTX PHY GRF — configuration registers for HDPTX PHY
 * hdptxphy_grf: syscon@26032000
 */
#define HDPTXPHY_GRF_BASE  0x26032000UL

/* Peripheral region for EDK2 memory map */
#define RK3576_PERIPH_BASE  0x20000000UL
#define RK3576_PERIPH_SZ    0x10000000UL

/* PLL input */
#define PLL_INPUT_OSC_RATE  (24 * 1000 * 1000)

/* I2C slave addresses on ROCK 4D */
#define PMIC_I2C_BUS   1          /* i2c1 */
#define PMIC_I2C_ADDR  0x23       /* RK806 */
#define RTC_I2C_BUS    2          /* i2c2 */
#define RTC_I2C_ADDR   0x51       /* HYM8563 */

/* GPIO pins for ROCK 4D (from rk3576-rock-4d.dts) */
/* Power LED:  GPIO0 PB4 = bank 0, pin 12 */
#define LED_POWER_GPIO_BANK  0
#define LED_POWER_GPIO_PIN   GPIO_PIN_PB4   /* active high, green */

/* User LED:   GPIO0 PC4 = bank 0, pin 20 */
#define LED_USER_GPIO_BANK   0
#define LED_USER_GPIO_PIN    GPIO_PIN_PC4   /* active low, blue */

/* USB HOST power enable: GPIO0 PD3 = bank 0, pin 27 */
#define USB_HOST_PWREN_BANK  0
#define USB_HOST_PWREN_PIN   GPIO_PIN_PD3   /* active high */

/* PCIe power enable: GPIO2 PD3 = bank 2, pin 27 */
#define PCIE_PWREN_BANK   2
#define PCIE_PWREN_PIN    GPIO_PIN_PD3

/* PCIe reset: GPIO2 PB4 = bank 2, pin 12 */
#define PCIE_RESET_BANK   2
#define PCIE_RESET_PIN    GPIO_PIN_PB4

/* WiFi enable: GPIO2 PD1 = bank 2, pin 25 */
#define WIFI_EN_BANK      2
#define WIFI_EN_PIN       GPIO_PIN_PD1

/* RTC interrupt: GPIO0 PA0 = bank 0, pin 0 */
#define RTC_INT_BANK      0
#define RTC_INT_PIN       GPIO_PIN_PA0

#ifdef __cplusplus
}
#endif
#endif /* __SOC_H__ */
