/** @file
 *
 *  Secondary System Description Table (SSDT): the RK3576 NPU (RKNN, two
 *  cores) for the Windows driver in woa-rk3576-npu.
 *
 *  Installed only when "NPU (experimental)" is enabled in setup (PcdAcpiNpu,
 *  default off) on a board that can set the NPU rail (PcdRk806I2cBase):
 *  RK3576AcpiPlatformDxe skips any SSDT with OEM table ID "RKNPU" otherwise.
 *  RK3576Dxe sets the rail, powers the NPU domains and sets its clock under
 *  the same condition (Npu.c); nothing here changes any of that.
 *
 *  From rk3576.dtsi as the RK3576 NPU series adds it (the nodes are not in
 *  mainline yet; v17 patch 11/12, linux-rk3576-npu rfc-send-v17/npu):
 *
 *    rknn_core_0: npu@27700000 {
 *      reg = <0x0 0x27700000 0x0 0x1000>,     "pc"
 *            <0x0 0x27701000 0x0 0x1000>,     "cna"
 *            <0x0 0x27703000 0x0 0x1000>;     "core"
 *      interrupts = <GIC_SPI 247 IRQ_TYPE_LEVEL_HIGH>;       GSIV 279
 *      resets = <&cru SRST_A_RKNN0>;
 *      power-domains = <&power RK3576_PD_NPU0>;
 *    };
 *    rknn_mmu_0: iommu@27702000 {
 *      reg = <0x0 0x27702000 0x0 0x100>, <0x0 0x27702100 0x0 0x100>;
 *      interrupts = <GIC_SPI 247 IRQ_TYPE_LEVEL_HIGH>;       shared with core 0
 *    };
 *    rknn_core_1: npu@27708000 { ... 0x27708000, 0x27709000, 0x2770b000;
 *      GIC_SPI 248 (GSIV 280), SRST_A_RKNN1, RK3576_PD_NPU1 };
 *    rknn_mmu_1: iommu@2770a000 { 0x2770a000, 0x2770a100; GIC_SPI 248 };
 *
 *  Resources, in the order the driver reads them:
 *    memory 0     0x27700000, 64 KiB: both cores (core 0 at +0x0000, core 1
 *                 at +0x8000), each with pc, cna, core and its two IOMMU
 *                 instances at +0x2000 and +0x2100
 *    interrupt 0  279, core 0 and its IOMMU
 *    interrupt 1  280, core 1 and its IOMMU
 *  One device owns both cores: the two share the CBUF and the clock, and
 *  each core's interrupt line is shared with its IOMMU.
 *
 *  _STA reports the device only while PD_NPU is on (PMU_STATUS0 bit 0 clear),
 *  NPUTOP, NPU0 and NPU1 are on (PMU_REPAIR0 bits 22, 23 and 24, the bits
 *  mainline's rockchip_pmu_domain_is_on() reads), and the NPU clocks are
 *  ungated (CLKGATE_CON28 bit 9, CON29 bit 0, CON31 bits 4, 5, 8, CON32 bits
 *  0, 5, 12).  A register access to a powered-down domain is an SError; this
 *  keeps a driver from ever being started against one.
 *
 *  _DSM, UUID 8599ea0d-52b6-41f1-993a-ce314872566c, revision 0:
 *    function 0  supported functions: 0x3
 *    function 1  soft-reset one core: Arg3 = Package () { core }, core 0 or
 *                1.  Asserts SRST_A_RKNN0 (SOFTRST_CON28 bit 9) or
 *                SRST_A_RKNN1 (SOFTRST_CON29 bit 0) for 10 us, then releases
 *                it: rocket_core_reset()'s sequence (rst-rk3576.c).  Returns
 *                0, or 1 for a bad core number.  The CRU is shared by every
 *                block on the SoC, so it is not in _CRS; this keeps every CRU
 *                write in firmware-described code.  The written bits are
 *                masked (the upper half of each register is a write enable),
 *                so no other reset moves.
 *
 *  _CCA 0: the NPU is not cache coherent.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("Npu.aml", "SSDT", 2, "RKCP  ", "RKNPU   ", 1)
{
  Scope (\_SB) {
    Device (NPU0) {
      Name (_HID, "RKCP3576")
      Name (_UID, 0)
      Name (_CCA, 0)

      // PMU_STATUS0, 0x27380000 + 0x230.  Read only.
      OperationRegion (PMUS, SystemMemory, 0x27380230, 0x4)
      Field (PMUS, DWordAcc, NoLock, Preserve) {
        STA0, 32
      }
      // PMU_REPAIR0, 0x27380000 + 0x570.  Read only.
      OperationRegion (PMUR, SystemMemory, 0x27380570, 0x4)
      Field (PMUR, DWordAcc, NoLock, Preserve) {
        REP0, 32
      }
      // CRU_CLKGATE_CON28..32, 0x27200000 + 0x800 + 28 * 4.  Read only.
      OperationRegion (CRUG, SystemMemory, 0x27200870, 0x14)
      Field (CRUG, DWordAcc, NoLock, Preserve) {
        GT28, 32,
        GT29, 32,
        GT30, 32,
        GT31, 32,
        GT32, 32
      }
      // CRU_SOFTRST_CON28..29, 0x27200000 + 0xA00 + 28 * 4.  Masked writes.
      OperationRegion (CRUR, SystemMemory, 0x27200A70, 0x8)
      Field (CRUR, DWordAcc, NoLock, WriteAsZeros) {
        SR28, 32,
        SR29, 32
      }

      Method (_STA, 0, Serialized) {
        If (((STA0 & 0x1) == 0) &&
            ((REP0 & 0x01C00000) == 0x01C00000) &&
            ((GT28 & 0x200) == 0) && ((GT29 & 0x1) == 0) &&
            ((GT31 & 0x130) == 0) && ((GT32 & 0x1021) == 0)) {
          Return (0xF)
        }
        Return (0x0)
      }

      Name (_CRS, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x27700000, 0x10000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 279 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 280 }
      })

      Method (_DSM, 4, Serialized) {
        If (Arg0 == ToUUID ("8599ea0d-52b6-41f1-993a-ce314872566c")) {
          If (Arg2 == 0) {
            Return (Buffer () { 0x03 })
          }
          If (Arg2 == 1) {
            Local0 = DerefOf (Arg3[0])
            If (Local0 == 0) {
              SR28 = 0x02000200
              Stall (10)
              SR28 = 0x02000000
              Return (0)
            }
            If (Local0 == 1) {
              SR29 = 0x00010001
              Stall (10)
              SR29 = 0x00010000
              Return (0)
            }
            Return (1)
          }
        }
        Return (Buffer () { 0x00 })
      }
    }
  }
}
