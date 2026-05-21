/** @file
 *
 *  RK3576 I2C controllers (I2C0..I2C9).
 *
 *  Addresses and interrupts verified against upstream rk3576.dtsi:
 *    I2C0  @ 0x27300000, GIC_SPI 88  → ACPI 120
 *    I2C1  @ 0x2ac40000, GIC_SPI 89  → ACPI 121
 *    I2C2  @ 0x2ac50000, GIC_SPI 90  → ACPI 122
 *    I2C3  @ 0x2ac60000, GIC_SPI 91  → ACPI 123
 *    I2C4  @ 0x2ac70000, GIC_SPI 92  → ACPI 124
 *    I2C5  @ 0x2ac80000, GIC_SPI 93  → ACPI 125
 *    I2C6  @ 0x2ac90000, GIC_SPI 94  → ACPI 126
 *    I2C7  @ 0x2aca0000, GIC_SPI 95  → ACPI 127
 *    I2C8  @ 0x2acb0000, GIC_SPI 96  → ACPI 128
 *    I2C9  @ 0x2ae80000, GIC_SPI 97  → ACPI 129
 *
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

  // I2C0 — SoC internal bus (DP/AUX or debug; usually disabled on carrier boards)
  Device (I2C0) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x27300000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 120 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C1) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 1)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac40000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 121 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C2) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 2)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac50000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 122 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C3) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 3)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac60000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 123 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C4) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 4)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac70000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 124 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C5) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 5)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac80000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 125 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C6) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 6)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ac90000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 126 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C7) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 7)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2aca0000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 127 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  Device (I2C8) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 8)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2acb0000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 128 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }

  // I2C9 — additional I2C bus in GPIO4 domain
  Device (I2C9) {
    Name (_HID, "RKCP3001")
    Name (_CID, "PRP0001")
    Name (_UID, 9)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2ae80000, 0x1000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 129 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) { "compatible", Package () { "rockchip,rk3576-i2c", "rockchip,rk3399-i2c" } },
        Package (2) { "clock-frequency", 400000 },
        Package (2) { "#address-cells", 1 },
        Package (2) { "#size-cells", 0 },
      }
    })
    Method (_STA) { Return (0xf) }
  }
