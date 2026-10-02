/** @file
 *
 *  The display: the VOP2 and HDMI output the firmware has already set up.
 *  Driver: malidod, the display-only driver in github.com/gahingwoo/wddm-mali-g52.
 *
 *  A device of its own, apart from GPU0, because Windows pairs a
 *  display-only driver with a render-only one: the Mali's WDDM driver
 *  (maliwddm) is render-only, and a render-only adapter cannot be the only
 *  graphics device -- with GPU0 alone it failed with code 43 and Windows
 *  had no display at all. malidod drives the framebuffer the firmware left
 *  (GOP) and does not touch the VOP2; the VOP2's registers and its system
 *  interrupt are described here because they are this device's, and a
 *  display driver that programs the VOP2 will need them.
 *
 *  DSP0 comes before GPU0 in the DSDT, and has resources: with neither,
 *  Windows (WinPE 22621) treated GPU0 as the POST device -- the display-only
 *  driver on DSP0 got an empty framebuffer from
 *  DxgkCbAcquirePostDisplayOwnership and failed to start, and the
 *  render-only driver on GPU0 was refused as a POST device
 *  (STATUS_GRAPHICS_INVALID_DRIVER_MODEL). Which of the two matters is not
 *  known.
 *
 *  Registers and interrupt from mainline rk3576.dtsi (vop@27d00000; "sys"
 *  is GIC SPI 342, GSIV 374).
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (DSP0) {
  Name (_HID, "RKCP7403")
  Name (_UID, 0)
  Name (_CCA, 0)
  Name (_STA, 0xF)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x27D00000, 0x3000)   // vop
      Memory32Fixed (ReadWrite, 0x27D05000, 0x1000)   // gamma-lut
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 374 }  // sys
    })
    Return (RBUF)
  }
}
