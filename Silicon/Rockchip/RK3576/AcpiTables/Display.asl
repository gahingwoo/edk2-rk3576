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
 *  (GOP), so no resources are claimed here; the VOP2 registers stay
 *  untouched by Windows.
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
}
