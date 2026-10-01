/** @file
 *
 *  Arm Mali-G52 (Bifrost, job manager). Driver: github.com/gahingwoo/wddm-mali-g52.
 *
 *  RK3576Dxe powers the GPU domain and sets its clock before boot; nothing
 *  here or in the driver changes either. Interrupts are GIC SPI 347, 348 and
 *  349 (job, MMU, GPU) from rk3576.dtsi, plus 32.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (GPU0) {
  Name (_HID, "RKCP7402")
  Name (_UID, 0)
  Name (_CCA, 0)
  Name (_STA, 0xF)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x27800000, 0x20000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 379 }  // job
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 380 }  // MMU
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 381 }  // GPU
    })
    Return (RBUF)
  }
}
