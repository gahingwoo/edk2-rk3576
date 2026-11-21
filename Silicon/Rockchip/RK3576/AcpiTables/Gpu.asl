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

#ifdef GPU0_BACKLIGHT_PWM_BASE
  //
  // The backlight of a MIPI DSI panel, for the display driver on a board
  // that has one (the board's DSDT defines GPU0_BACKLIGHT_PWM_BASE, the
  // PWM v4 channel's 4 KiB register block, before it includes this file).
  //
  // BLPR is 0 here and set to 1 by RK3576AcpiPlatformDxe at ReadyToBoot
  // when the panel is the screen the OS gets and the firmware lit its
  // backlight.  Only then does _CRS carry the channel's registers, as a
  // second memory resource after the GPU's; the driver takes a second memory
  // resource as "this screen is an internal panel, and here is its
  // backlight", and the absence of one as HDMI.  It must stay
  // "Name (BLPR, 0)", ahead of _CRS: the patch looks for the first NameOp
  // followed by "BLPR" and turns its ZeroOp into a OneOp.
  //
  // The firmware leaves the channel running in continuous mode at the
  // brightness set in setup; the driver changes only DUTY and commits it.
  //
  Name (BLPR, 0)
#endif

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate() {
      Memory32Fixed (ReadWrite, 0x27800000, 0x20000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 379 }  // job
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 380 }  // MMU
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 381 }  // GPU
    })
#ifdef GPU0_BACKLIGHT_PWM_BASE
    If (BLPR) {
      Name (BBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, GPU0_BACKLIGHT_PWM_BASE, 0x1000)       // backlight PWM
      })
      ConcatenateResTemplate (RBUF, BBUF, Local0)
      Return (Local0)
    }
#endif
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
