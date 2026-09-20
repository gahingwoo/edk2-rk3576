/** @file
 *
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"


  Device (SDC3) {
    Name (_HID, "RKCP0D40")
    Name (_CID, "PNP0D40")   // SDA-compatible: lets the Windows inbox SDHCI driver bind
    Name (_UID, 3)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0x2a330000, 0x10000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 285 }
      })
      Return (RBUF)
    }
    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        //
        // A _DSD property is a two-element package {name, value}; a list of
        // strings has to be nested, the way Sdhc.asl does it.  This had three
        // elements and was not a valid property.
        //
        Package () { "compatible", Package () { "rockchip,rk3576-dwcmshc", "rockchip,rk3588-dwcmshc" } },
        Package () { "max-frequency", 200000000 },
        Package () { "bus-width", 8 },
        Package () { "no-sd", 0x1 },
        Package () { "no-sdio", 0x1  },
        Package () { "mmc-hs400-1_8v", 0x1  },
        Package () { "mmc-hs400-enhanced-strobe", 0x1  },
        Package () { "non-removable", 0x1  },
      }
    })

    //
    // RK3576 CRU_CLKSEL_CON(89): CCLK_SRC_EMMC.
    //
    //   bits [15:14]  mux   00 = gpll_400m, 01 = cpll_400m, 10 = xin_24m
    //   bits [13:8]   divider - 1
    //   bits [31:16]  write mask for the bits being changed
    //
    // Both mux settings used below are confirmed from the firmware's own
    // clock calls on hardware: mux 0 with parent 400000000 and mux 2 with
    // parent 24000000.
    //
    OperationRegion(EMMC, SystemMemory, 0x27200464, 0x4)
      Field(EMMC, DWordAcc, NoLock, WriteAsZeros) {
      PLLE, 32,
    }

    Method (_DSM, 4) {
      If (LEqual (Arg0, ToUUID("434addb0-8ff3-49d5-a724-95844b79ad1f"))) {
        Switch (ToInteger (Arg2)) {
          Case (0) {
            Return (0x3)
          }
          Case (1) {
            //
            // This table was RK3588's, where the parent is 1200 MHz, so its
            // dividers read 1200/6, /8, /12 and /24 for 200, 150, 100 and
            // 50 MHz.  On RK3576 the parent is 400 MHz: the same dividers
            // give 66.7, 50, 33.3 and 16.7 MHz while this method reported
            // the RK3588 numbers back to the caller.  Recomputed for 400 MHz,
            // and each Return is the rate actually programmed.
            //
            Local0 = DerefOf (Arg3 [0])
            If (Local0 >= 200000000) {
              Store (0xFF000100, PLLE)      // gpll_400m / 2
              Return (200000000)
            }
            If (Local0 >= 133333333) {
              Store (0xFF000200, PLLE)      // gpll_400m / 3
              Return (133333333)
            }
            If (Local0 >= 100000000) {
              Store (0xFF000300, PLLE)      // gpll_400m / 4
              Return (100000000)
            }
            If (Local0 >= 50000000) {
              Store (0xFF000700, PLLE)      // gpll_400m / 8
              Return (50000000)
            }
            If (Local0 >= 24000000) {
              Store (0xFF008000, PLLE)      // xin_24m / 1
              Return (24000000)
            }
            if (Local0 >= 375000) {
              Store (0xFF00BF00, PLLE)      // xin_24m / 64
              Return (375000)
            }
            Return (0)
          }
        }
      }
      Return (0)
    }

    // Used by downstream Linux driver.
    Method(SCLK, 1, Serialized) {
      If (Arg0 <= 400000)
      {
        Store (0xFF00BF00, PLLE)
      }
      ElseIF (Arg0 <= 50000000)
      {
        Store (0xFF008000, PLLE)
      }
      Else
      {
        Store (0xFF000700, PLLE)      // gpll_400m / 8 = 50 MHz
      }
    }

    /* TODO:
    Method(_PS3) {

    }

    Method(_PS2) {
      Store (0xFF00BF00, PLLE)
    }

    Method(_PS1) {
      Store (0xFF008000, PLLE)
    }

    Method(_PS0) {
      Store (0xFF000600, PLLE)
    }

    Method(_PSC) {
      Return(0x01)
    }
    */

    //
    // A child device that represents the non-removable eMMC.
    //
    Device (SDMM)
    {
      Method (_ADR)
      {
        Return (0)
      }
      Method (_RMV) // Is removable
      {
        Return (0) // 0 - fixed
      }
    }
  }