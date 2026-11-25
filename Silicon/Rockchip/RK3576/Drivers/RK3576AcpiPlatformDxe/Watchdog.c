/** @file
 *
 *  The hardware watchdog (WDT_NS) for an ACPI OS: prepares the block and
 *  fills in the WDAT (AcpiTables/Wdat.aslc) before it is installed.
 *
 *  Only with "Hardware watchdog" on in setup (PcdHwWatchdog, default off).
 *  With it off the table is not installed and nothing here touches the
 *  hardware.
 *
 *  What the OS needs and nobody else does for it:
 *    - the block clocked (CRU_GATE_CON16 bits 7/8 open; their reset state)
 *      and stopped: a pulse on its two resets, so it starts in the state the
 *      WDAT calls "enabled\stopped" whatever ran before;
 *    - its reset output allowed to reset the chip: SYS_GRF_SOC_CON4 bit 8
 *      and CRU_GLB_RST_CON bit 6.  TF-A (system_reset_init) sets both; they
 *      are checked, and set only if found clear;
 *    - a record of whether this boot was a watchdog reset, from
 *      CRU_GLBRST_ST_NCLR bit 12, which no reset clears: it is read, the
 *      bit is written back as 0, and the answer is kept for the OS in the
 *      scratch page.
 *
 *  The UEFI boot-services watchdog (gBS->SetWatchdogTimer) is
 *  MdeModulePkg WatchdogTimerDxe, a timer event that calls ResetSystem; it
 *  never touches this block, and it is gone after ExitBootServices.  TF-A
 *  uses WDT_S (0x2A4C0000), not this one.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/WatchdogActionTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <RK3576Wdat.h>

#include "Watchdog.h"

STATIC volatile UINT32  *mWdatScratch;

/**
  Get the WDT_NS ready and record whether the last reset was its doing.

  @return  TRUE if the last reset was a WDT_NS reset.
**/
STATIC
BOOLEAN
WatchdogPrepareHardware (
  VOID
  )
{
  UINT32   Nclr;
  UINT32   NclrAfter;
  UINT32   GlbRstCon;
  UINT32   GrfCon4;
  BOOLEAN  Fired;

  Nclr  = MmioRead32 (RK3576_CRU_GLBRST_ST_NCLR);
  Fired = (Nclr & RK3576_GLBRST_NCLR_WDTNS) != 0;
  if (Fired) {
    MmioWrite32 (RK3576_CRU_GLBRST_ST_NCLR, Nclr & ~RK3576_GLBRST_NCLR_WDTNS);
  }

  NclrAfter = MmioRead32 (RK3576_CRU_GLBRST_ST_NCLR);

  //
  // Clocks open, then the block through its reset: EN = 0, TORR = 0.
  //
  MmioWrite32 (RK3576_CRU_GATE_CON16, RK3576_WDT0_CLK_BITS << 16);
  MmioWrite32 (RK3576_CRU_SOFTRST_CON16, RK3576_WDT0_SRST_ASSERT);
  MmioWrite32 (RK3576_CRU_SOFTRST_CON16, RK3576_WDT0_SRST_ASSERT);
  MmioWrite32 (RK3576_CRU_SOFTRST_CON16, RK3576_WDT0_SRST_DEASSERT);

  GrfCon4 = MmioRead32 (RK3576_SYS_GRF_SOC_CON4);
  if ((GrfCon4 & RK3576_SYS_GRF_WDTNS_GLB_EN) == 0) {
    MmioWrite32 (
      RK3576_SYS_GRF_SOC_CON4,
      (RK3576_SYS_GRF_WDTNS_GLB_EN << 16) | RK3576_SYS_GRF_WDTNS_GLB_EN
      );
    DEBUG ((DEBUG_WARN, "AcpiPlatform: watchdog: SYS_GRF_SOC_CON4 had WDT_NS reset off (0x%08x), set\n", GrfCon4));
  }

  GlbRstCon = MmioRead32 (RK3576_CRU_GLB_RST_CON);
  if ((GlbRstCon & RK3576_GLB_RST_CON_WDT_EN) == 0) {
    //
    // As TF-A would have set it: watchdog resets enabled, as a first
    // global reset (the same kind PSCI SYSTEM_RESET does).
    //
    MmioWrite32 (
      RK3576_CRU_GLB_RST_CON,
      GlbRstCon | RK3576_GLB_RST_CON_WDT_EN | RK3576_GLB_RST_CON_WDT_FIRST
      );
    DEBUG ((DEBUG_WARN, "AcpiPlatform: watchdog: CRU_GLB_RST_CON had watchdog resets off (0x%08x), set\n", GlbRstCon));
  }

  DEBUG ((
    DEBUG_ERROR,
    "AcpiPlatform: watchdog: WDT_NS ready, CR 0x%x; last reset %a the watchdog "
    "(GLBRST_ST_NCLR 0x%04x -> 0x%04x); GLB_RST_CON 0x%04x, SYS_GRF_SOC_CON4 0x%04x\n",
    MmioRead32 (RK3576_WDT_NS_BASE + RK3576_WDT_CR),
    Fired ? "WAS" : "was not",
    Nclr & 0xFFFF,
    NclrAfter & 0xFFFF,
    MmioRead32 (RK3576_CRU_GLB_RST_CON) & 0xFFFF,
    MmioRead32 (RK3576_SYS_GRF_SOC_CON4) & 0xFFFF
    ));

  return Fired;
}

BOOLEAN
WatchdogPrepareWdat (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *AcpiHeader
  )
{
  EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE                              *Wdat;
  EFI_ACPI_WATCHDOG_ACTION_1_0_WATCHDOG_ACTION_INSTRUCTION_ENTRY  *Entry;
  EFI_PHYSICAL_ADDRESS                                            Page;
  EFI_STATUS                                                      Status;
  UINT32                                                          Index;
  UINT32                                                          Patched;
  BOOLEAN                                                         Fired;

  Wdat = (EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE *)AcpiHeader;
  if ((AcpiHeader->Length < sizeof (*Wdat)) ||
      (AcpiHeader->Length < sizeof (*Wdat) + Wdat->NumberWatchdogInstructionEntries * sizeof (*Entry)))
  {
    DEBUG ((DEBUG_ERROR, "AcpiPlatform: watchdog: WDAT malformed, not installed\n"));
    return FALSE;
  }

  if (mWdatScratch == NULL) {
    //
    // ACPI NVS: kept out of the OS's RAM and reported to it as firmware's,
    // so the OS can map it the way it maps the WDT registers.
    //
    Status = gBS->AllocatePages (AllocateAnyPages, EfiACPIMemoryNVS, 1, &Page);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "AcpiPlatform: watchdog: no page for the WDAT (%r), not installed\n", Status));
      return FALSE;
    }

    mWdatScratch = (volatile UINT32 *)(UINTN)Page;
    ZeroMem ((VOID *)mWdatScratch, EFI_PAGE_SIZE);
  }

  Fired = WatchdogPrepareHardware ();

  mWdatScratch[RK3576_WDAT_SCRATCH_STATUS / 4]    = Fired ? 1 : 0;
  mWdatScratch[RK3576_WDAT_SCRATCH_COUNTDOWN / 4] = RK3576_WDAT_MAX_COUNT;
  mWdatScratch[RK3576_WDAT_SCRATCH_REBOOT / 4]    = 1;

  Entry   = (EFI_ACPI_WATCHDOG_ACTION_1_0_WATCHDOG_ACTION_INSTRUCTION_ENTRY *)(Wdat + 1);
  Patched = 0;
  for (Index = 0; Index < Wdat->NumberWatchdogInstructionEntries; Index++) {
    if ((Entry[Index].RegisterRegion.Address & RK3576_WDAT_SCRATCH_TAG_MASK) == RK3576_WDAT_SCRATCH_TAG) {
      Entry[Index].RegisterRegion.Address = (UINTN)mWdatScratch +
                                            (Entry[Index].RegisterRegion.Address & ~RK3576_WDAT_SCRATCH_TAG_MASK);
      Patched++;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "AcpiPlatform: hardware watchdog on, installing the WDAT (%u actions, scratch at 0x%lx, %u entries patched)\n",
    Wdat->NumberWatchdogInstructionEntries,
    (UINT64)(UINTN)mWdatScratch,
    Patched
    ));

  return TRUE;
}
