/** @file
 *
 *  RK3576 PCIe hardware mapping
 *
 *  Copyright (c) 2025, ROCK 4D RK3576 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_PCIE_H
#define RK3576_PCIE_H

//
// RK3576 has 2 PCIe Gen2 1-lane controllers (PCIe 2.0).
// pce0 is connected through combphy0, pce1 through combphy1.
//
#define NUM_PCIE_CONTROLLER  2

#define PCIE_SEGMENT_PCIE0  0
#define PCIE_SEGMENT_PCIE1  1

//
// DBI (per-controller, 32-bit address space)
//
#define PCIE0_DBI_BASE  0x22000000UL
#define PCIE1_DBI_BASE  0x22400000UL

//
// APB slave base (per-controller PCIe2 client registers)
//
#define PCIE0_APB_BASE  0x2a200000UL
#define PCIE1_APB_BASE  0x2a210000UL

//
// ECAM / CFG space base (each segment maps its bus window here)
//
#define PCIE0_CFG_BASE  0x20000000UL
#define PCIE1_CFG_BASE  0x21000000UL

//
// I/O window (CPU address, identity-mapped to bus address)
//
#define PCIE0_IO_BASE  0x20100000UL
#define PCIE1_IO_BASE  0x21100000UL

//
// 32-bit MEM window
//
#define PCIE0_MEM32_BASE  0x20200000UL
#define PCIE1_MEM32_BASE  0x21200000UL

//
// 64-bit MEM window
//
#define PCIE0_MEM64_BASE  0x900000000ULL
#define PCIE1_MEM64_BASE  0x980000000ULL

//
// Fixed sizes
//
#define PCIE_APB_SIZE    SIZE_64KB
#define PCIE_DBI_SIZE    SIZE_4MB
#define PCIE_CFG_SIZE    SIZE_1MB
#define PCIE_IO_SIZE     SIZE_1MB
#define PCIE_MEM32_SIZE  0x00E00000UL   // 14 MB
#define PCIE_MEM64_SIZE  0x80000000ULL  // 2 GB

//
// Per-segment address selectors (identity-mapped: CPU addr == bus addr)
//
#define PCIE_DBI_BASE(Segment)    ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_DBI_BASE    : PCIE1_DBI_BASE)
#define PCIE_APB_BASE(Segment)    ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_APB_BASE    : PCIE1_APB_BASE)
#define PCIE_CFG_BASE(Segment)    ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_CFG_BASE    : PCIE1_CFG_BASE)
#define PCIE_IO_BASE(Segment)     ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_IO_BASE     : PCIE1_IO_BASE)
#define PCIE_MEM32_BASE(Segment)  ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_MEM32_BASE  : PCIE1_MEM32_BASE)
#define PCIE_MEM64_BASE(Segment)  ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_MEM64_BASE  : PCIE1_MEM64_BASE)

//
// Bus-address base for the 32-bit MEM window (fixed/shared value for ACPI _CRS).
// Pcie.asl uses PCIE_MEM32_BUS_BASE as the bus address base for segment 0.
// Both segments share non-overlapping 32-bit windows, each with identity mapping.
//
#define PCIE_MEM32_BUS_BASE              PCIE0_MEM32_BASE
#define PCIE_MEM32_TRANSLATION(Segment)  0    // identity-mapped
#define PCIE_IO_BUS_BASE                 0x00000000UL
#define PCIE_IO_TRANSLATION(Segment)     0    // identity-mapped

//
// Bus number allocation. Each segment gets 16 buses (0-15, 32-47).
//
#define PCIE0_BUS_BASE  0
#define PCIE1_BUS_BASE  32
#define PCIE_BUS_COUNT  16

#define PCIE_BUS_BASE(Segment)         ((Segment) == PCIE_SEGMENT_PCIE0 ? PCIE0_BUS_BASE : PCIE1_BUS_BASE)
#define PCIE_BUS_LIMIT(Segment)        (PCIE_BUS_BASE (Segment) + PCIE_BUS_COUNT - 1)
#define PCIE_BUS_BASE_OFFSET(Segment)  (PCIE_BUS_BASE (Segment) * SIZE_1MB)

#endif // RK3576_PCIE_H
