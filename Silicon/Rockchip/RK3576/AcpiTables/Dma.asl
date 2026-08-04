/** @file
 *
 *  RK3576 DMA controllers (DMA0..DMA2, ARM PL330).
 *
 *  Addresses and interrupts verified against upstream rk3576.dtsi:
 *    DMA0  @ 0x2ab90000, GIC_SPI 32+33 → ACPI 64+65
 *    DMA1  @ 0x2abb0000, GIC_SPI 34+35 → ACPI 66+67
 *    DMA2  @ 0x2abd0000, GIC_SPI 36+37 → ACPI 68+69
 *
 *  Copyright (c) 2022, Rockchip Electronics Co. Ltd.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

//
// Description: DMA
//
  Device (DMA0)
  {
    Name (_HID, "ARMH0330")
    Name (_UID, 0)
    Method (_CRS, 0, Serialized)
    {
      Name (RBUF, ResourceTemplate ()
      {
        Memory32Fixed (ReadWrite,
          0x2ab90000,         // Address Base
          0x00004000,         // Address Length
        )
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          64,
        }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          65,
        }
      })
      Return (RBUF)
    }
    Name (_DSD, Package() {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) {"ctlrName", "DMA0"}
      }
    })
  }

  Device (DMA1)
  {
    Name (_HID, "ARMH0330")
    Name (_UID, 1)
    Method (_CRS, 0, Serialized)
    {
      Name (RBUF, ResourceTemplate ()
      {
        Memory32Fixed (ReadWrite,
          0x2abb0000,         // Address Base
          0x00004000,         // Address Length
        )
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          66,
        }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          67,
        }
      })
      Return (RBUF)
    }
    Name (_DSD, Package() {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) {"ctlrName", "DMA1"}
      }
    })
  }

  Device (DMA2)
  {
    Name (_HID, "ARMH0330")
    Name (_UID, 2)
    Method (_CRS, 0, Serialized)
    {
      Name (RBUF, ResourceTemplate ()
      {
        Memory32Fixed (ReadWrite,
          0x2abd0000,         // Address Base
          0x00004000,         // Address Length
        )
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          68,
        }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
        {
          69,
        }
      })
      Return (RBUF)
    }
    Name (_DSD, Package() {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package (2) {"ctlrName", "DMA2"}
      }
    })
  }
