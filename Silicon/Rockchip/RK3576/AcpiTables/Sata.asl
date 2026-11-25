/** @file
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

/*
 * RK3576 has two AHCI SATA controllers, one port each, from mainline
 * rk3576.dtsi:
 *
 *   sata0: sata@2a240000, GIC_SPI 262 (GSIV 294), PHY combphy0_ps
 *          (phy@2b050000), shared with PCIe0 (pcie@22000000)
 *   sata1: sata@2a250000, GIC_SPI 263 (GSIV 295), PHY combphy1_psu
 *          (phy@2b060000), shared with PCIe1 (pcie@22400000) and USB3
 *
 * Both are "rockchip,rk3576-dwc-ahci", in PD_SUBPHP, with ports-implemented
 * = 1, and dma-coherent (hence _CCA 1). A controller only works when its
 * combo PHY is in SATA mode, which nothing sets: no supported board routes
 * SATA, and the PHYs are in PCIe or USB mode. Both devices therefore stay
 * disabled. Enabling one needs the PHY in SATA mode, PD_SUBPHP on and the
 * SATA clocks first.
 *
 * The _HID and compatible are the RK3588 ones, kept from the file this was
 * copied from. RK3588 has a third controller; RK3576 does not.
 */
Scope (\_SB_) {
  Device (ATA0) {
    Name (_HID, "RKCP0161")
    Name (_UID, 0)
    Name (_CLS, Package() { 0x01, 0x06, 0x01 })
    Name (_CCA, 1)
    Name (_STA, Zero)  // disabled: combo PHY 0 is not in SATA mode on any board

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2a240000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 294 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", "rockchip,rk-ahci" },
      }
    })
  }

  Device (ATA1) {
    Name (_HID, "RKCP0161")
    Name (_UID, 1)
    Name (_CLS, Package() { 0x01, 0x06, 0x01 })
    Name (_CCA, 1)
    Name (_STA, Zero)  // disabled: combo PHY 1 is not in SATA mode on any board

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2a250000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 295 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", "rockchip,rk-ahci" },
      }
    })
  }
}
