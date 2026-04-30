/** @file
 *
 *  Copyright (c) 2021, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2017-2021, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2019, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2014, Linaro Limited. All rights reserved.
 *  Copyright (c) 2013-2018, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/Rk3588Mem.h>
#include <Library/SdramLib.h>
#include <Library/SerialPortLib.h>

// ROCK4D-DEBUG
#define CHKPT(c) do { UINT8 _ck[3] = { '[', (c), ']' }; SerialPortWrite (_ck, 3); } while (0)

UINT64         mSystemMemoryBase = FixedPcdGet64 (PcdSystemMemoryBase);
STATIC UINT64  mSystemMemorySize = FixedPcdGet64 (PcdSystemMemorySize);

// The total number of descriptors, including the final "end-of-table" descriptor.
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS  16

STATIC BOOLEAN                    VirtualMemoryInfoInitialized = FALSE;
STATIC RK3588_MEMORY_REGION_INFO  VirtualMemoryInfo[MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS];

#define VariablesBase  FixedPcdGet64(PcdFlashNvStorageVariableBase64)

#define VariablesSize  (FixedPcdGet32(PcdFlashNvStorageVariableSize)   +\
                       FixedPcdGet32(PcdFlashNvStorageFtwWorkingSize) + \
                       FixedPcdGet32(PcdFlashNvStorageFtwSpareSize))

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU
  on your platform.

  @param[out]   VirtualMemoryMap    Array of ARM_MEMORY_REGION_DESCRIPTOR
                                    describing a Physical-to-Virtual Memory
                                    mapping. This array must be ended by a
                                    zero-filled entry

**/
VOID
ArmPlatformGetVirtualMemoryMap (
  IN ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  UINTN                         Index = 0;
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  // ROCK4D-DEBUG: static fallback table to bypass AllocatePages entirely
  // while we instrument the early HOB heap. AllocatePages hung on RK3576
  // ROCK 4D between SdramGetMemorySize and the table allocation; using a
  // BSS-resident table eliminates the HOB allocator from the critical path.
  STATIC ARM_MEMORY_REGION_DESCRIPTOR  mStaticVmt[MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS];

  CHKPT ('1');
  mSystemMemorySize = SdramGetMemorySize ();
  CHKPT ('2');
  // ROCK4D-DEBUG: skip DEBUG() between [2] and [3] entirely to rule out
  // any possibility of the print itself stalling (the macro could expand
  // even in RELEASE under certain Pcd configurations).
  // DEBUG ((DEBUG_INFO, "RAM: 0x%ll08X (Size 0x%ll08X)\n", mSystemMemoryBase, mSystemMemorySize));

  VirtualMemoryTable = mStaticVmt;
  CHKPT ('3');

  //
  // TF-A Region
  // Must be unmapped for the shared memory to retain its attributes.
  //
  VirtualMemoryTable[Index].PhysicalBase = 0x00000000;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = 0x200000;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_UNMAPPED_REGION;
  VirtualMemoryInfo[Index++].Name        = L"TF-A + Shared Memory";

  //
  // SoC layout dispatch.
  //
  // RK3588: FV/NV in low DRAM (FvBase=0x00200000, NV ~0x007Cxxxx), MMIO at
  // 0xF0000000-0xFFFFFFFF.
  //
  // RK3576 (e.g. ROCK 4D): FV is loaded by TF-A as BL33 directly into
  // mid-DRAM (FvBase=0x40800000), so VariablesBase lands above OP-TEE.
  // RK3576 MMIO is at 0x20000000-0x2FFFFFFF (UART0=0x2AD40000,
  // GIC=0x2A701000, USB=0x23000000, CRU=0x27200000, ...). The 0xF0000000
  // region used by RK3588 is *DRAM* on RK3576 and must NOT be mapped as
  // DEVICE.
  //
  // We discriminate on the FV/NV layout (cleanly avoids needing a separate
  // SoC ifdef inside this generic mem map).
  //
  if ((VariablesBase + VariablesSize) > 0x08400000) {
    //
    // ===== RK3576-style layout (BL33 loaded into mid-DRAM) =====
    //
    // RK3576 physical DRAM map (per Rockchip TRM + observed crashes):
    //   [0x00000000 .. 0xF0000000)  — usable DRAM (NS-EL2 accessible)
    //   [0xF0000000 .. 0x100000000) — RESERVED (alias / TZASC firewall)
    //                                  faults with EC=0x25 ext-abort
    //   [0x100000000 .. PhysTop - 256MB) — usable DRAM (>=4GB)
    //   [PhysTop-256MB .. PhysTop)   — RESERVED (BL31 secure carve-out:
    //                                  OP-TEE TA pool, SCMI mailbox, etc.)
    //
    // The previous external abort at PC=0xFF634C3C, FAR=0x10F004 was caused
    // by DxeCore allocating a driver image into [0xFF000000..0x100000000)
    // which is not real DRAM on this SoC.
    //
    #define RK3576_LOW_DRAM_TOP       0xF0000000ULL
    #define RK3576_TZASC_RESERVE_TOP  0x10000000ULL  /* top 256MB */
    if (mSystemMemorySize > RK3576_TZASC_RESERVE_TOP) {
      mSystemMemorySize -= RK3576_TZASC_RESERVE_TOP;
    }
    // [0x00200000 .. 0x08400000)   DRAM (WB)            — pre-OP-TEE region
    // [0x08400000 .. 0x09400000)   OP-TEE              (WB, reserved)
    // [0x09400000 .. 0x20000000)   DRAM (WB)
    // [0x20000000 .. 0x30000000)   RK3576 MMIO         (DEVICE)
    // [0x30000000 .. 0x100000000)  DRAM (WB)
    // [0x100000000 .. SysMemEnd)   DRAM (WB)            — only if >4GB
    // FV (0x40800000) and NV-Variable (0x40DCxxxx) fall inside the WB DRAM
    // ranges with the correct attribute; runtime semantics are tracked by
    // the GCD descriptor type, not the MMU attribute.
    //

    // Pre-OP-TEE DRAM
    VirtualMemoryTable[Index].PhysicalBase = 0x00200000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x08400000 - 0x00200000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
    VirtualMemoryInfo[Index++].Name        = L"System RAM (< OP-TEE)";

    // OP-TEE
    VirtualMemoryTable[Index].PhysicalBase = 0x08400000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x01000000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_RESERVED_REGION;
    VirtualMemoryInfo[Index++].Name        = L"OP-TEE";

    // DRAM between OP-TEE end and RK3576 MMIO start
    VirtualMemoryTable[Index].PhysicalBase = 0x09400000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x20000000 - 0x09400000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
    VirtualMemoryInfo[Index++].Name        = L"System RAM (mid)";

    // RK3576 MMIO aperture (UART, GIC, SDHCI, SFC, CRU, USB, ...)
    VirtualMemoryTable[Index].PhysicalBase = 0x20000000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x10000000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_UNMAPPED_REGION;
    VirtualMemoryInfo[Index++].Name        = L"RK3576 MMIO";

    // DRAM from end of RK3576 MMIO up to the low-DRAM safe top (0xF0000000).
    // The [0xF0000000..0x100000000) hole is left UNMAPPED to avoid DxeCore
    // allocating into it.
    VirtualMemoryTable[Index].PhysicalBase = 0x30000000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = MIN (mSystemMemorySize, RK3576_LOW_DRAM_TOP) - 0x30000000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
    VirtualMemoryInfo[Index++].Name        = L"System RAM (< 4GB)";

    if (mSystemMemorySize > 0x100000000UL) {
      // DRAM >= 4GB (already capped at PhysTop - 256MB above)
      VirtualMemoryTable[Index].PhysicalBase = 0x100000000UL;
      VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
      VirtualMemoryTable[Index].Length       = mSystemMemorySize - 0x100000000UL;
      VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
      VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
      VirtualMemoryInfo[Index++].Name        = L"System RAM >= 4GB";
    }

    // End of Table
    VirtualMemoryTable[Index].PhysicalBase = 0;
    VirtualMemoryTable[Index].VirtualBase  = 0;
    VirtualMemoryTable[Index].Length       = 0;
    VirtualMemoryTable[Index++].Attributes = (ARM_MEMORY_REGION_ATTRIBUTES)0;

    ASSERT (Index <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

    *VirtualMemoryMap            = VirtualMemoryTable;
    VirtualMemoryInfoInitialized = TRUE;
    return;
  }

  //
  // ===== RK3588-style layout (FV/NV in low DRAM) =====
  //

  // Firmware Volume
  VirtualMemoryTable[Index].PhysicalBase = FixedPcdGet64 (PcdFvBaseAddress);
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = FixedPcdGet32 (PcdFvSize);
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_RESERVED_REGION;
  VirtualMemoryInfo[Index++].Name        = L"UEFI FV";

  // Variable Volume
  VirtualMemoryTable[Index].PhysicalBase = VariablesBase;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = VariablesSize;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_RUNTIME_REGION;
  VirtualMemoryInfo[Index++].Name        = L"Variable Store";

  // Base System RAM (< OP-TEE)
  VirtualMemoryTable[Index].PhysicalBase = VariablesBase + VariablesSize;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = MIN (mSystemMemorySize, 0x08400000 - VirtualMemoryTable[Index].PhysicalBase);
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
  VirtualMemoryInfo[Index++].Name        = L"System RAM (< OP-TEE)";

  // OP-TEE Region
  VirtualMemoryTable[Index].PhysicalBase = 0x08400000;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = 0x1000000;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_RESERVED_REGION;
  VirtualMemoryInfo[Index++].Name        = L"OP-TEE";

  // Base System RAM (< 4GB)
  VirtualMemoryTable[Index].PhysicalBase = 0x08400000 + 0x1000000;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = MIN (mSystemMemorySize, 0xF0000000 - VirtualMemoryTable[Index].PhysicalBase);
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
  VirtualMemoryInfo[Index++].Name        = L"System RAM (< 4GB)";

  // MMIO
  VirtualMemoryTable[Index].PhysicalBase = 0xF0000000;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = 0x10000000;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_UNMAPPED_REGION;
  VirtualMemoryInfo[Index++].Name        = L"MMIO";

  if (mSystemMemorySize > 0x100000000UL) {
    // Base System RAM >= 4GB
    VirtualMemoryTable[Index].PhysicalBase = 0x100000000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = mSystemMemorySize - 0x100000000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
    VirtualMemoryInfo[Index++].Name        = L"System RAM >= 4GB";
  }

  // MMIO > 32GB
  VirtualMemoryTable[Index].PhysicalBase = 0x0000000900000000UL;
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = 0x0000000141400000UL;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  VirtualMemoryInfo[Index].Type          = RK3588_MEM_UNMAPPED_REGION;
  VirtualMemoryInfo[Index++].Name        = L"MMIO > 32GB";

  if (mSystemMemoryBase + mSystemMemorySize > 0x3fc000000UL) {
    // Bad memory range 1
    VirtualMemoryTable[Index].PhysicalBase = 0x3fc000000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x500000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_RESERVED_REGION;
    VirtualMemoryInfo[Index++].Name        = L"BAD1";

    // Bad memory range 2
    VirtualMemoryTable[Index].PhysicalBase = 0x3fff00000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x100000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_RESERVED_REGION;
    VirtualMemoryInfo[Index++].Name        = L"BAD2";
  }

  // End of Table
  VirtualMemoryTable[Index].PhysicalBase = 0;
  VirtualMemoryTable[Index].VirtualBase  = 0;
  VirtualMemoryTable[Index].Length       = 0;
  VirtualMemoryTable[Index++].Attributes = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT (Index <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  *VirtualMemoryMap            = VirtualMemoryTable;
  VirtualMemoryInfoInitialized = TRUE;
}

/**
  Return additional memory info not populated by the above call.

  This call should follow the one to ArmPlatformGetVirtualMemoryMap ().

**/
VOID
Rk3588PlatformGetVirtualMemoryInfo (
  IN RK3588_MEMORY_REGION_INFO  **MemoryInfo
  )
{
  ASSERT (VirtualMemoryInfo != NULL);

  if (!VirtualMemoryInfoInitialized) {
    DEBUG ((
      DEBUG_ERROR,
      "ArmPlatformGetVirtualMemoryMap must be called before Rk3588PlatformGetVirtualMemoryInfo.\n"
      ));
    return;
  }

  *MemoryInfo = VirtualMemoryInfo;
}
