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

  //
  // The monitor's EDID, for the display driver (DxgkCbEvalAcpiMethod on the
  // adapter, DISPLAY_ADAPTER_HW_ID), which has no DDC of its own and would
  // otherwise report a monitor with no name, size or modes.
  //
  // RK3576AcpiPlatformDxe copies in what LcdGraphicsOutputDxe read over DDC
  // (EFI_EDID_ACTIVE_PROTOCOL) at ReadyToBoot: the base block and its
  // extensions, zero after them. All zeros means no EDID: no monitor, or a
  // read that failed and fell back to the built-in mode. A reader checks
  // the header and checksum and uses byte 126 for the block count.
  //
  // 512 bytes, the most _DDC may return (ACPI 6.4, Appendix B, _DDC). The
  // initialiser is written out so iasl emits every byte and the firmware
  // can fill them in place; Buffer (512) {} would leave no room for them.
  //
  // A private object on the adapter, not _DOD with an output device that
  // has _DDC: those are the standard ACPI video objects, and Windows would
  // see them whatever driver is bound to GPU0 and expect the driver's child
  // IDs to match the _ADR values. This one is read only by a driver that
  // asks for it.
  //
#define EDID_ZERO16  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#define EDID_ZERO128 EDID_ZERO16, EDID_ZERO16, EDID_ZERO16, EDID_ZERO16, EDID_ZERO16, EDID_ZERO16, EDID_ZERO16, EDID_ZERO16
  Name (EDBF, Buffer (512) {
    EDID_ZERO128, EDID_ZERO128, EDID_ZERO128, EDID_ZERO128
  })

  Method (EDID, 0, Serialized) {
    Return (EDBF)
  }
}
