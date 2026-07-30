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
    // RK3576 physical DRAM starts at 0x40000000, NOT at 0.
    //
    // Authority: TF-A plat/rockchip/rk3576/rk3576_def.h
    //     #define RK_DRAM_BASE  0x40000000
    //     #define TZRAM_BASE    RK_DRAM_BASE
    //     #define TZRAM_SIZE    SZ_1M
    //     #define BL31_BASE     (TZRAM_BASE + 0x40000)   -> 0x40040000
    // and BL31_BASE matches the atf-2 load address in our own FIT. With 4 GB
    // detected, DRAM is [0x40000000 .. 0x140000000), which is exactly what
    // vendor U-Boot hands to Linux ("Adding bank: 0x40200000 - 0x100000000"
    // plus "0x100000000 - 0x140000000").
    //
    // Everything below 0x40000000 is SoC address space, not DRAM. The map
    // used to declare three chunks of it as System RAM (inherited from the
    // RK3588 layout, where DRAM really does start at 0):
    //     [0x00200000 .. 0x08400000)   [0x09400000 .. 0x20000000)
    //     [0x30000000 .. 0x40000000)
    // Any access to those takes a synchronous external abort. Confirmed on
    // hardware: `dmem 0x30000000` in the UEFI Shell gives
    //     ESR=0x96000010 (DFSC 0b010000, sync external abort) FAR=0x30000000
    // and the Windows boot manager died the same way, because its preferred
    // ImageBase is 0x10000000 -- inside the bogus "mid" region -- so
    // PeCoffLoaderLoadImage memcpy'd .text straight into nothing.
    //
    // MMIO at [0x20000000 .. 0x30000000) is correct and stays (UART0
    // 0x2AD40000, GIC 0x2A701000, USB 0x23000000/0x23400000, GRF 0x26040000,
    // VOP2 0x27D00000, HDMI 0x27DA0000).
    //
    // Layout produced below:
    //   [0x20000000 .. 0x30000000)   RK3576 MMIO                   (DEVICE)
    //   [0x40200000 .. LowTop)       DRAM                          (WB)
    //
    // Deliberately left out, each for its own reason:
    //   [0x40000000 .. 0x40200000)   TF-A: TZRAM (BL31) + DDR_SHARE_MEM.
    //   [0xF0000000 .. 0x100000000)  Real DRAM on this SoC, but the previous
    //                                author observed an abort here. That was
    //                                most likely the same low-memory bug
    //                                misattributed, yet it is unverified, so
    //                                it stays excluded until someone probes
    //                                it with `dmem`.
    //   [0x100000000 .. 0x140000000) Real DRAM, 1 GB, never mapped because the
    //                                old `> 0x100000000` test compared a
    //                                *size* against an *address*. Adding it
    //                                needs the >32-bit MMU/GCD span checked
    //                                first; separate change.
    //
    #define RK3576_DRAM_BASE      0x40000000ULL
    #define RK3576_DRAM_SAFE_BASE 0x40200000ULL  /* above TZRAM + share mem */
    #define RK3576_LOW_DRAM_TOP   0xF0000000ULL

    /*
     * Reclaiming the last 1.25 GB.
     *
     * With DRAM at [0x40000000 .. 0x140000000) the region
     * [0xF0000000 .. 0x140000000) is ordinary DRAM — 256 MB that the old code
     * carved out as a "TZASC firewall hole" plus the 1 GB the dead
     * `> 0x100000000` test never reached. Both exclusions were reasoned from
     * the RK3588 layout, where 0xF0000000 really is an MMIO hole.
     *
     * This is left OFF by default because it has not been probed on hardware
     * yet, and because turning it on changes the memory map — which must not
     * be bundled with any other display/boot experiment. To test:
     *
     *   1. `dmem 0xF0000000 0x20` and `dmem 0x100000000 0x20` from the UEFI
     *      Shell. A synchronous abort (EC 0x25) with a matching FAR means the
     *      region really is unusable; clean output means it is fine.
     *   2. Flip this to 1, rebuild, confirm `memmap` shows ~4 GB and the board
     *      still boots.
     *
     * Keep the top 256 MB reserved either way: Rockchip BL31 carves out a
     * secure pool (OP-TEE TA memory, SCMI mailbox) at the top of DRAM.
     */
    #define RK3576_MAP_FULL_DRAM      0
    #define RK3576_SECURE_TOP_RESERVE 0x10000000ULL  /* top 256 MB, BL31 */

    // RK3576 MMIO aperture (UART, GIC, SDHCI, SFC, CRU, USB, ...)
    VirtualMemoryTable[Index].PhysicalBase = 0x20000000;
    VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
    VirtualMemoryTable[Index].Length       = 0x10000000;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
    VirtualMemoryInfo[Index].Type          = RK3588_MEM_UNMAPPED_REGION;
    VirtualMemoryInfo[Index++].Name        = L"RK3576 MMIO";

    // The one and only DRAM region. Note mSystemMemorySize is a *size*, so the
    // top has to be BASE + SIZE; the old code compared it against an address
    // directly, which only worked because RK3588's DRAM base is 0.
    {
 #if RK3576_MAP_FULL_DRAM
      UINT64  DramTop = (RK3576_DRAM_BASE + mSystemMemorySize) -
                        RK3576_SECURE_TOP_RESERVE;
 #else
      UINT64  DramTop = MIN (RK3576_DRAM_BASE + mSystemMemorySize,
                             RK3576_LOW_DRAM_TOP);
 #endif

      ASSERT (DramTop > RK3576_DRAM_SAFE_BASE);

      VirtualMemoryTable[Index].PhysicalBase = RK3576_DRAM_SAFE_BASE;
      VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
      VirtualMemoryTable[Index].Length       = DramTop - RK3576_DRAM_SAFE_BASE;
      VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
      VirtualMemoryInfo[Index].Type          = RK3588_MEM_BASIC_REGION;
      VirtualMemoryInfo[Index++].Name        = L"System RAM";
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
