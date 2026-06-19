/** @file
 *
 *  RK3576 SPI controllers (SPI0..SPI4) and FSPI (SFC0).
 *
 *  Addresses and interrupts verified against upstream rk3576.dtsi:
 *    SPI0  @ 0x2acf0000, GIC_SPI 116 → ACPI 148
 *    SPI1  @ 0x2ad00000, GIC_SPI 117 → ACPI 149
 *    SPI2  @ 0x2ad10000, GIC_SPI 118 → ACPI 150
 *    SPI3  @ 0x2ad20000, GIC_SPI 119 → ACPI 151
 *    SPI4  @ 0x2ad30000, GIC_SPI 120 → ACPI 152
 *    SFC0  @ 0x2a340000, GIC_SPI 254 → ACPI 286 (FSPI0 — disabled, used by UEFI)
 *
 *  Copyright (c) 2022 ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

Device (SPI0)
{
  Name (_HID, "RKCP3003")   // Windows binds ACPI\RKCP3003; _CID keeps Linux (PRP0001+compatible)
  Name (_CID, "PRP0001")
  Name (_UID, 0)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2acf0000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 148 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-spi", "rockchip,rk3066-spi" } },
      Package () { "clock-frequency", 200000000 },
      Package () { "num-cs", 2 },
    }
  })
  Method (_STA) { Return (0xf) }
}

Device (SPI1)
{
  Name (_HID, "RKCP3003")
  Name (_CID, "PRP0001")
  Name (_UID, 1)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2ad00000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 149 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-spi", "rockchip,rk3066-spi" } },
      Package () { "clock-frequency", 200000000 },
      Package () { "num-cs", 2 },
    }
  })
  Method (_STA) { Return (0xf) }
}

Device (SPI2)
{
  Name (_HID, "RKCP3003")
  Name (_CID, "PRP0001")
  Name (_UID, 2)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2ad10000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 150 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-spi", "rockchip,rk3066-spi" } },
      Package () { "clock-frequency", 200000000 },
      Package () { "num-cs", 2 },
    }
  })
  Method (_STA) { Return (0xf) }
}

Device (SPI3)
{
  Name (_HID, "RKCP3003")
  Name (_CID, "PRP0001")
  Name (_UID, 3)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2ad20000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 151 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-spi", "rockchip,rk3066-spi" } },
      Package () { "clock-frequency", 200000000 },
      Package () { "num-cs", 2 },
    }
  })
  Method (_STA) { Return (0xf) }
}

Device (SPI4)
{
  Name (_HID, "RKCP3003")
  Name (_CID, "PRP0001")
  Name (_UID, 4)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2ad30000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 152 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-spi", "rockchip,rk3066-spi" } },
      Package () { "clock-frequency", 200000000 },
      Package () { "num-cs", 2 },
    }
  })
  Method (_STA) { Return (0xf) }
}

Device (SFC0)
{
  Name (_HID, "PRP0001")
  Name (_CID, "PRP0001")
  Name (_UID, 10)
  Name (_CCA, 0)

  /*
   * FSPI0 (SPI-NOR/NAND flash controller) at 0x2a340000.
   * Kept disabled: UEFI uses NorFlashDxe to access SPI-NOR directly;
   * letting Linux probe the same flash controller can corrupt the UEFI
   * variable store.  Enable only if the OS is explicitly managing the flash.
   */
  Name (_STA, 0x0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x2a340000, 0x4000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 286 }
    })
    Return (RBUF)
  }
  Name (_DSD, Package () {
    ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "compatible", Package () { "rockchip,rk3576-sfc", "rockchip,sfc" } },
      Package () { "clock-frequency", 100000000 },
    }
  })
}
