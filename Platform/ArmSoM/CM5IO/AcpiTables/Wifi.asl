/** @file
 *
 *  [SSDT] The SDIO controller the CM5 module's Wi-Fi (BW3752-50B1, a
 *  Synaptics/Broadcom BCM43752) sits on, for the woa-rk3576 drivers
 *  (docs/dev/wifi.md there).
 *
 *  Installed by RK3576AcpiPlatformDxe only when the "SDIO Wi-Fi" setup
 *  option is on (PcdAcpiSdioWifi, off by default), the same option under
 *  which Cm5ioWifi.c sets the pins and clocks, cycles WL_REG_ON and probes
 *  the card.  Without the option the OS sees no SDIO controller at all, as
 *  before.
 *
 *  From rk3576.dtsi (sdio: mmc@2a320000, rockchip,rk3576-dw-mshc, 16 KiB,
 *  GIC_SPI 252 = GSIV 284, level high, fifo-depth 0x100),
 *  rk3576-armsom-cm5.dtsi (&sdio: bus-width 4, cap-sdio-irq, non-removable,
 *  no-sd, no-mmc, vqmmc vcc_1v8_s3) and the ArmSoM-CM5-1V1 schematic
 *  (page 17: U8 BW3752-50B1, SD_* to SDMMC1_*_M0, VDDIO = VCC_1V8_S3,
 *  WL_WAKE_HOST = WIFI_WAKE_HOST_H = GPIO0 PB0).  The TaishanPi 3M's Wi-Fi
 *  uses the same controller and the same pins; this table differs from
 *  its Wifi.asl only in the names.
 *
 *  The HID is RKCPFE2D, not the SD slot's RKCPFE2C, so a driver for the SD
 *  slot never binds this controller by accident: the woa-rk3576 dwcmshc
 *  driver programs the SD slot's CRU clock register unless it knows which
 *  controller it has, and only an INF that names RKCPFE2D opts in.
 *
 *  The properties are the ones dwcmshc reads, kept conservative for bring-up:
 *  max-frequency 50 MHz (high speed, no UHS: SDR104 needs tuning, not tried
 *  yet), no-1-8-v (the pins already run at 1.8 V, VCC_1V8_S3; no voltage
 *  switch is wanted).  "non-removable" and "cap-sdio-irq" are what the
 *  device tree says; dwcmshc uses the first to report the card present
 *  without a card-detect line.
 *
 *  HOST_WAKE is not declared yet: in-band SDIO interrupts (DAT1) come first.
 *  It goes in WLAN's _CRS as
 *    GpioInt (Level, ActiveHigh, Exclusive, PullDown, 0, "\\_SB.GPI0") { GPIO_PIN_PB0 }
 *  once the Wi-Fi driver uses out-of-band wake (the NVRAM's muxenab=0x10
 *  routes it).
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

DefinitionBlock ("Wifi.aml", "SSDT", 2, "RKCP  ", "RKWIFI  ", 1)
{
  Scope (\_SB_)
  {
    Device (SDIO) {
      Name (_HID, "RKCPFE2D")
      Name (_UID, 0x1)
      Name (_DDN, "RK3576 SDIO controller (CM5 BCM43752 Wi-Fi)")
      Name (_CCA, 0x0)
      Name (_S1D, 0x1)
      Name (_S2D, 0x1)
      Name (_S3D, 0x1)
      Name (_S4D, 0x1)

      Method (_CRS, 0x0, Serialized) {
        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x2a320000, 0x4000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 284 }
        })
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
        Package () {
          Package () { "compatible", Package () { "rockchip,rk3576-dw-mshc" } },
          Package () { "fifo-depth", 0x100 },
          Package () { "max-frequency", 50000000 },
          Package () { "bus-width", 4 },
          Package () { "cap-sd-highspeed", 1 },
          Package () { "cap-sdio-irq", 1 },
          Package () { "non-removable", 1 },
          Package () { "no-sd", 1 },
          Package () { "no-mmc", 1 },
          Package () { "no-1-8-v", 1 },
        }
      })

      Method (_STA) { Return (0xf) }

      //
      // The SDIO card in the slot, as Sdhc.asl's SDMM is the SD card.  The
      // Broadcom functions (1: backplane, 2: WLAN data) are enumerated by
      // the bus driver from the card's CIS, not described here.
      //
      Device (WLAN) {
        Name (_ADR, 0x0)
        Method (_RMV) { Return (0) }
      }
    }
  }
}
