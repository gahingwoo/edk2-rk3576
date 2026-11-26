/** @file
 *
 *  Secondary System Description Table (SSDT): the RK3576 video decoder
 *  (RKVDEC, VDPU383) for the Windows driver in woa-rk3576
 *  (drivers/video/vdpu383).
 *
 *  Installed only when "Video Decoder" is enabled in
 *  setup (PcdAcpiVdec, default off): RK3576AcpiPlatformDxe skips any SSDT with
 *  OEM table ID "RKVDEC" otherwise.  RK3576Dxe powers PD_VDEC and sets the
 *  decoder clocks under the same condition (Vdec.c); nothing here changes
 *  either.
 *
 *  From mainline rk3576.dtsi:
 *
 *    vdec: video-codec@27b00000 {
 *      reg = <0x0 0x27b00000 0x0 0x100>,      "link"
 *            <0x0 0x27b00100 0x0 0x500>,      "function"
 *            <0x0 0x27b00600 0x0 0x100>;      "cache"
 *      interrupts = <GIC_SPI 308 IRQ_TYPE_LEVEL_HIGH>;       GSIV 340
 *      iommus = <&vdec_mmu>;
 *      power-domains = <&power RK3576_PD_VDEC>;
 *      sram = <&rkvdec_sram>;
 *    };
 *    vdec_mmu: iommu@27b00800 {
 *      reg = <0x0 0x27b00800 0x0 0x40>, <0x0 0x27b00900 0x0 0x40>;
 *      interrupts = <GIC_SPI 309 IRQ_TYPE_LEVEL_HIGH>;       GSIV 341
 *    };
 *    sram: sram@3ff88000 { reg = <0x0 0x3ff88000 0x0 0x78000>; ... };
 *    rkvdec_sram: rkvdec-sram@0 { reg = <0x0 0x78000>; };
 *
 *  Resources, in the order the driver reads them:
 *    memory 0     0x27B00000, 4 KiB: link, function and cache registers and
 *                 both IOMMU instances (one 4 KiB page; the decoder and its
 *                 IOMMU share a power domain and a driver)
 *    interrupt 0  340, the decoder
 *    interrupt 1  341, the IOMMU
 *    memory 1     0x3FF88000, 480 KiB: the system SRAM mainline gives the
 *                 decoder for its row buffers.  Optional; the driver works
 *                 without it.
 *
 *  _STA reports the device only while PD_VDEC is on (PMU_REPAIR0 bit 20, the
 *  bit mainline's rockchip_pmu_domain_is_on() reads) and the decoder clocks
 *  are ungated (CRU_CLKGATE_CON45 bits 0-3, 8, 9 all 0).  A register access
 *  to a powered-down domain is an SError; this keeps a driver from ever
 *  being started against one.
 *
 *  _CCA 0: the decoder is not cache coherent.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("Vdec.aml", "SSDT", 2, "RKCP  ", "RKVDEC  ", 1)
{
  Scope (\_SB) {
    Device (VDEC) {
      Name (_HID, "RKCP0383")
      Name (_UID, 0)
      Name (_CCA, 0)

      // PMU_REPAIR0, 0x27380000 + 0x570.  Read only.
      OperationRegion (PMUR, SystemMemory, 0x27380570, 0x4)
      Field (PMUR, DWordAcc, NoLock, Preserve) {
        REP0, 32
      }
      // CRU_CLKGATE_CON45, 0x27200000 + 0x800 + 45 * 4.  Read only.
      OperationRegion (CRUG, SystemMemory, 0x272008B4, 0x4)
      Field (CRUG, DWordAcc, NoLock, Preserve) {
        GT45, 32
      }

      Method (_STA, 0, Serialized) {
        If (((REP0 & 0x100000) != 0) && ((GT45 & 0x30F) == 0)) {
          Return (0xF)
        }
        Return (0x0)
      }

      Method (_CRS, 0x0, Serialized) {
        Return (\_SB.VDRS)
      }
    }

    //
    // The decoder's resources as one named template, outside the device,
    // so that another device can take them over: the plan is for the
    // display driver (wddm-mali-g52, GPU0 in Gpu.asl) to own the decoder as
    // a second, VIDEO_DECODE, node.  GPU0's _CRS would then append them
    // after its own,
    //
    //   If (CondRefOf (\_SB.VDRS)) {
    //     ConcatenateResTemplate (RBUF, \_SB.VDRS, Local0)
    //   }
    //
    // and this table would stop declaring VDEC: Windows gives a memory
    // resource to one device only, so the two cannot both claim it.
    // CondRefOf keeps GPU0 valid on a boot where this SSDT is not installed.
    //
    Name (VDRS, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x27B00000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 340 }
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 341 }
      Memory32Fixed (ReadWrite, 0x3FF88000, 0x78000)
    })
  }
}
