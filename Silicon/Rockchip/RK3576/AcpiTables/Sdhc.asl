/** @file
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

//
// The UHS modes need 1.8 V signalling, and on RK3576 nothing the OS can reach
// switches vqmmc (vccio_sd_s0, an RK806 LDO): RK3588 does it through a
// Rockchip SiP call into BL31, and the BL31 these boards ship does not
// implement that service. So no UHS, and "no-1-8-v" below, until it does.
//
#ifndef SDMMC_CAP_DDR50
#define SDMMC_CAP_DDR50 0
#endif
#ifndef SDMMC_CAP_SDR50
#define SDMMC_CAP_SDR50 0
#endif
#ifndef SDMMC_CAP_SDR104
#define SDMMC_CAP_SDR104 0
#endif

Scope (\_SB_) {
  Name (SDRM, 1) // SD slot is removable

  Device (SDHC) {
    Name (_HID, "RKCPFE2C")
    Name (_UID, 0x0)
    Name (_CCA, 0x0)
    Name (_S1D, 0x1)
    Name (_S2D, 0x1)
    Name (_S3D, 0x1)
    Name (_S4D, 0x1)
    Name (_STA, 0xf)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x2a310000, 0x4000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 283 }

        // SDMMC_DET
        GpioIO (Shared, PullUp, 0, 0, IoRestrictionNone, "\\_SB.GPI0") { GPIO_PIN_PA7 }
        GpioInt (Edge, ActiveBoth, Shared, PullUp, 0, "\\_SB.GPI0") { GPIO_PIN_PA7 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", Package () { "rockchip,rk3576-dw-mshc", "rockchip,rk3288-dw-mshc" } },
        Package () { "fifo-depth", 0x100 },
        Package () { "max-frequency", 200000000 },
        Package () { "bus-width", 4 },
        Package () { "cap-sd-highspeed", 1 },
        Package () { "sd-uhs-ddr50", SDMMC_CAP_DDR50 },
        Package () { "sd-uhs-sdr50", SDMMC_CAP_SDR50 },
        Package () { "sd-uhs-sdr104", SDMMC_CAP_SDR104 },
        Package () { "no-1-8-v", 1 },
        Package () { "broken-cd", FixedPcdGetBool (PcdRkSdmmcCardDetectBroken) },
      }
    })

    //
    // A child device that represents the SD card.
    //
    Device (SDMM) {
      Name (_ADR, 0x0)

      Method (_RMV) {
        Return (SDRM)
      }
    }
  }
}
