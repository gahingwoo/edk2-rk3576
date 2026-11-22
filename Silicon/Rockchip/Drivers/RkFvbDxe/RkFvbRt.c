/** @file
 *
 *  Variable writes an OS makes after ExitBootServices, on SD and eMMC.
 *
 *  The store on these media is a DRAM buffer that only the boot-time NV dump
 *  writes back, and DRAM does not come through a reset on RK3576: TPL
 *  retrains it and it reads back as zeros (measured on the TaishanPi 3M with
 *  the canary grid in RkFvbDxe.c).  So two other ways out:
 *
 *  1. BootNext through a PMU1 GRF OS register.  The register is only cleared
 *     by NPOR, so it survives the CRU global reset TF-A uses for
 *     SYSTEM_RESET.  SetVariable("BootNext") from the OS leaves a tagged copy
 *     there, and the next boot puts BootNext back before BDS runs.  That is
 *     what "reboot into the other OS" needs (bcdedit bootsequence,
 *     efibootmgr -n).
 *
 *  2. On eMMC, an optional write of the store to the medium from the
 *     ResetSystem() an OS makes, by a small polled driver for the SDHCI that
 *     resets the controller and the card and starts from CMD0, since the OS
 *     may have put the card to sleep or sent it a power-off notification.
 *     Only sectors that changed are written, highest first, each one read
 *     back, and only if every change is one flash could make (bits going
 *     from 1 to 0).  That is all the variable driver does at runtime: it
 *     appends at the end of the log and clears state bits further down, it
 *     never reclaims.  Writing the appended sectors before the state bits
 *     means a power cut in the middle leaves either the old or the new value
 *     of a variable, never neither, and the FV and variable store headers,
 *     which a runtime write never touches, are never rewritten.  Off unless
 *     the "Save OS Variable Changes to eMMC" setup option is on.
 *
 *  Everything reached after ExitBootServices follows the runtime rules: no
 *  boot services, no DEBUG(), only converted pointers.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/VariableWrite.h>

#include "RkFvbDxe.h"

//
// RK3576 registers (TRM V1.2 Part 1).
//
#define RT_PMU1_GRF_BASE   0x26026000ULL
#define RT_PMU1_GRF_SIZE   SIZE_4KB
#define RT_PMU1_OS_REG(n)  (0x200 + (n) * 4)   // "with npor reset"

#define RT_CRU_BASE        0x27200000ULL
#define RT_CRU_SIZE        SIZE_64KB
#define RT_CRU_CLKSEL89    (0x300 + 89 * 4)    // [15:14] cclk_src_emmc mux, [13:8] div
#define RT_CRU_CLKGATE33   (0x800 + 33 * 4)    // [12:8] cclk/hclk/aclk/bclk/tclk_emmc

#define RT_PMU_BASE        0x27380000ULL
#define RT_PMU_SIZE        SIZE_4KB
#define RT_PMU_IDLE_ST     0x128               // PD_NVM bus idle: bit 18
#define RT_PMU_REPAIR_ST   0x570               // PD_NVM powered: bit 6
#define RT_PD_NVM_IDLE     BIT18
#define RT_PD_NVM_ON       BIT6

#define RT_SDHCI_SIZE      SIZE_64KB

//
// Which OS registers.  PMU1 GRF OS_REG0 is the BootROM request (TF-A), 2..5
// the DRAM description the SPL/ddrbin leave for TF-A, U-Boot, Linux and
// SdramLib, 6 the DRAM dynamic info (vendor rockchip-dfi).  Nothing in TF-A,
// U-Boot, mainline or vendor Linux, or this firmware uses 14 or 15.  The
// boot log prints all sixteen so a board can confirm it.
//
#define RT_OS_REG_STATUS    14
#define RT_OS_REG_BOOTNEXT  15

#define RT_BOOTNEXT_TAG     0xB0070000U   // | Boot#### option number
#define RT_BOOTNEXT_MASK    0xFFFF0000U
#define RT_STATUS_TAG       0xE3C00000U   // | step << 8 | detail
#define RT_STATUS_MASK      0xFFF00000U

#define RT_ST_PD_OFF          0x01
#define RT_ST_NOT_MONOTONIC   0x02
#define RT_ST_OUTSIDE_VARS    0x03
#define RT_ST_CTRL_RESET      0x04
#define RT_ST_CARD_IDLE       0x05
#define RT_ST_CARD_OCR        0x06
#define RT_ST_CARD_SELECT     0x07
#define RT_ST_EXT_CSD         0x08
#define RT_ST_PARTITION       0x09
#define RT_ST_WRITE           0x0A
#define RT_ST_VERIFY          0x0B
#define RT_ST_DONE            0x0C
#define RT_ST_NOTHING         0x0D
#define RT_ST_BUSY            0x0E

//
// SDHCI (SD Host Controller spec 4.x) and the DWC MSHC vendor area, as
// DwcSdhciDxe uses them.
//
#define SDHC_BLK_SIZE     0x04
#define SDHC_BLK_COUNT    0x06
#define SDHC_ARG          0x08
#define SDHC_XFER_MODE    0x0C
#define SDHC_CMD          0x0E
#define SDHC_RESP0        0x10
#define SDHC_BUF_DATA     0x20
#define SDHC_PRESENT      0x24
#define SDHC_HOST_CTRL1   0x28
#define SDHC_PWR_CTRL     0x29
#define SDHC_CLK_CTRL     0x2C
#define SDHC_TIMEOUT      0x2E
#define SDHC_SW_RST       0x2F
#define SDHC_NOR_INT_STS  0x30
#define SDHC_ERR_INT_STS  0x32
#define SDHC_NOR_INT_EN   0x34
#define SDHC_ERR_INT_EN   0x36
#define SDHC_NOR_INT_SIG  0x38
#define SDHC_ERR_INT_SIG  0x3A
#define SDHC_HOST_CTRL2   0x3E
#define SDHC_CAP          0x40

#define EMMC_HOST_CTRL3   0x508
#define EMMC_EMMC_CTRL    0x52C
#define EMMC_DLL_CTRL     0x800
#define EMMC_DLL_RXCLK    0x804
#define EMMC_DLL_TXCLK    0x808
#define EMMC_DLL_STRBIN   0x80C
#define EMMC_MISC_CON     0x81C

#define PRESENT_CMD_INHIBIT  BIT0
#define PRESENT_DAT_INHIBIT  BIT1

#define INT_CMD_COMPLETE   BIT0
#define INT_XFER_COMPLETE  BIT1
#define INT_BUF_WR_READY   BIT4
#define INT_BUF_RD_READY   BIT5
#define INT_ERROR          BIT15

#define CMD_RESP_NONE   0x00
#define CMD_RESP_R2     (0x01 | BIT3)             // 136 bit, CRC
#define CMD_RESP_R3     0x02                      // 48 bit, no CRC, no index
#define CMD_RESP_R1     (0x02 | BIT3 | BIT4)      // 48 bit, CRC, index
#define CMD_RESP_R1B    (0x03 | BIT3 | BIT4)      // 48 bit busy
#define CMD_DATA        BIT5

#define XFER_READ       BIT4

#define EMMC_OCR_REQ    0x40FF8080U               // sector mode, 2.7-3.6 V and 1.7-1.95 V
#define EMMC_RCA        0x00010000U
#define EMMC_STATE_TRAN 4

//
// State.  Pointers are converted at SetVirtualAddressMap.
//
STATIC UINTN             mRtPmu1Grf;
STATIC UINTN             mRtCru;
STATIC UINTN             mRtPmu;
STATIC UINTN             mRtSdhci;
STATIC FVB_DEVICE        *mRtDev;
STATIC UINT8             *mRtShadow;      // the store as it is on the medium
STATIC UINT64            mRtEmmcLba;      // first sector of nvdata on the eMMC, 0 = none
STATIC BOOLEAN           mRtEmmcEnabled;
STATIC BOOLEAN           mRtDirty;
STATIC BOOLEAN           mRtCommitting;
STATIC BOOLEAN           mRtHighCapacity;
STATIC EFI_SET_VARIABLE  mRtOrigSetVariable;
STATIC EFI_RESET_SYSTEM  mRtOrigResetSystem;
STATIC UINT32            mRtSector[128];  // one 512 byte block

/**
  Describe an MMIO range to the OS as needed by runtime services.
**/
STATIC
EFI_STATUS
RkFvbRtRegisterMmio (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Size
  )
{
  EFI_STATUS                       Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;

  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  Base,
                  Size,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (Status == EFI_ACCESS_DENIED) {
    //
    // Some other driver added it already, maybe without EFI_MEMORY_RUNTIME
    // in the capabilities, which SetMemorySpaceAttributes() then refuses.
    //
    Status = gDS->GetMemorySpaceDescriptor (Base, &Desc);
    if (!EFI_ERROR (Status) && ((Desc.Capabilities & EFI_MEMORY_RUNTIME) == 0)) {
      Status = gDS->SetMemorySpaceCapabilities (
                      Base,
                      Size,
                      Desc.Capabilities | EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                      );
    }
  }

  if (!EFI_ERROR (Status)) {
    Status = gDS->SetMemorySpaceAttributes (Base, Size, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RkFvbRt: cannot map 0x%lx+0x%lx for runtime: %r\n", Base, Size, Status));
  }

  return Status;
}

STATIC
VOID
RkFvbRtOsRegWrite (
  IN UINTN   Reg,
  IN UINT32  Value
  )
{
  if (mRtPmu1Grf != 0) {
    MmioWrite32 (mRtPmu1Grf + RT_PMU1_OS_REG (Reg), Value);
  }
}

STATIC
VOID
RkFvbRtStatus (
  IN UINT32  Step,
  IN UINT32  Detail
  )
{
  RkFvbRtOsRegWrite (RT_OS_REG_STATUS, RT_STATUS_TAG | ((Step & 0xFF) << 8) | (Detail & 0xFF));
}

// ---------------------------------------------------------------------------
// Polled SDHCI.  Runtime only.
// ---------------------------------------------------------------------------

STATIC
BOOLEAN
RtWait16 (
  IN UINTN   Reg,
  IN UINT16  Mask,
  IN UINT16  Value,
  IN UINTN   TimeoutUs
  )
{
  while ((MmioRead16 (mRtSdhci + Reg) & Mask) != Value) {
    if (TimeoutUs < 10) {
      return FALSE;
    }

    MicroSecondDelay (10);
    TimeoutUs -= 10;
  }

  return TRUE;
}

/**
  Wait for one of Bits in the normal interrupt status, failing on an error.
**/
STATIC
BOOLEAN
RtWaitInt (
  IN UINT16  Bits,
  IN UINTN   TimeoutUs
  )
{
  UINT16  Sts;

  for ( ; ; ) {
    Sts = MmioRead16 (mRtSdhci + SDHC_NOR_INT_STS);
    if ((Sts & INT_ERROR) != 0) {
      return FALSE;
    }

    if ((Sts & Bits) != 0) {
      MmioWrite16 (mRtSdhci + SDHC_NOR_INT_STS, Sts & Bits);
      return TRUE;
    }

    if (TimeoutUs < 10) {
      return FALSE;
    }

    MicroSecondDelay (10);
    TimeoutUs -= 10;
  }
}

STATIC
VOID
RtResetLines (
  VOID
  )
{
  // CMD and DAT line resets, after an error.
  MmioWrite8 (mRtSdhci + SDHC_SW_RST, BIT1 | BIT2);
  RtWait16 (SDHC_SW_RST & ~1U, 0x0600, 0, 100000);
  MmioWrite16 (mRtSdhci + SDHC_NOR_INT_STS, 0xFFFF);
  MmioWrite16 (mRtSdhci + SDHC_ERR_INT_STS, 0xFFFF);
}

/**
  Send one command; for a data command also move one 512 byte block
  between mRtSector and the card.
**/
STATIC
BOOLEAN
RtCmd (
  IN  UINT8    Index,
  IN  UINT32   Arg,
  IN  UINT16   Flags,
  IN  BOOLEAN  Read,
  OUT UINT32   *Resp OPTIONAL
  )
{
  UINT32   Inhibit;
  UINTN    Count;
  UINTN    Word;
  BOOLEAN  Data;

  Data    = (Flags & CMD_DATA) != 0;
  Inhibit = PRESENT_CMD_INHIBIT;
  if (Data || ((Flags & 0x03) == 0x03)) {
    Inhibit |= PRESENT_DAT_INHIBIT;
  }

  for (Count = 0; (MmioRead32 (mRtSdhci + SDHC_PRESENT) & Inhibit) != 0; Count++) {
    if (Count > 50000) {
      RtResetLines ();
      return FALSE;
    }

    MicroSecondDelay (10);
  }

  MmioWrite16 (mRtSdhci + SDHC_NOR_INT_STS, 0xFFFF);
  MmioWrite16 (mRtSdhci + SDHC_ERR_INT_STS, 0xFFFF);

  if (Data) {
    MmioWrite16 (mRtSdhci + SDHC_BLK_SIZE, 512);
    MmioWrite16 (mRtSdhci + SDHC_BLK_COUNT, 1);
    MmioWrite16 (mRtSdhci + SDHC_XFER_MODE, Read ? XFER_READ : 0);
  } else {
    MmioWrite16 (mRtSdhci + SDHC_XFER_MODE, 0);
  }

  MmioWrite32 (mRtSdhci + SDHC_ARG, Arg);
  MmioWrite16 (mRtSdhci + SDHC_CMD, (UINT16)((Index << 8) | Flags));

  if (!RtWaitInt (INT_CMD_COMPLETE, 20000)) {
    RtResetLines ();
    return FALSE;
  }

  if (Resp != NULL) {
    *Resp = MmioRead32 (mRtSdhci + SDHC_RESP0);
  }

  if (Data) {
    if (!RtWaitInt (Read ? INT_BUF_RD_READY : INT_BUF_WR_READY, 500000)) {
      RtResetLines ();
      return FALSE;
    }

    for (Word = 0; Word < ARRAY_SIZE (mRtSector); Word++) {
      if (Read) {
        mRtSector[Word] = MmioRead32 (mRtSdhci + SDHC_BUF_DATA);
      } else {
        MmioWrite32 (mRtSdhci + SDHC_BUF_DATA, mRtSector[Word]);
      }
    }
  }

  if (Data || ((Flags & 0x03) == 0x03)) {
    // Data done, or the busy of an R1b response released.
    if (!RtWaitInt (INT_XFER_COMPLETE, 1000000)) {
      RtResetLines ();
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Wait until the card is back in the transfer state and ready for data.
**/
STATIC
BOOLEAN
RtWaitTran (
  VOID
  )
{
  UINT32  Status;
  UINTN   Count;

  for (Count = 0; Count < 400; Count++) {
    if (RtCmd (13, EMMC_RCA, CMD_RESP_R1, FALSE, &Status) &&
        ((Status & BIT8) != 0) && (((Status >> 9) & 0xF) == EMMC_STATE_TRAN))
    {
      return TRUE;
    }

    MicroSecondDelay (250);
  }

  return FALSE;
}

/**
  The card clock comes from the CRU on RK3576 (the SDHCI divider does
  nothing, see DwcSdhciDxe.c).  The oscillator divided by Div, with the card
  clock stopped around the change.
**/
STATIC
BOOLEAN
RtSetCardClock (
  IN UINT32  Div
  )
{
  MmioWrite16 (mRtSdhci + SDHC_CLK_CTRL, 0);
  MmioWrite32 (
    mRtCru + RT_CRU_CLKSEL89,
    (((0x3U << 14) | (0x3FU << 8)) << 16) | (2U << 14) | ((Div - 1) << 8)
    );
  MicroSecondDelay (10);

  MmioWrite16 (mRtSdhci + SDHC_CLK_CTRL, BIT0);
  if (!RtWait16 (SDHC_CLK_CTRL, BIT1, BIT1, 20000)) {
    return FALSE;
  }

  MmioWrite16 (mRtSdhci + SDHC_CLK_CTRL, BIT0 | BIT2);
  MicroSecondDelay (100);
  return TRUE;
}

/**
  Bring the controller and the card up from whatever the OS left: clocks on,
  controller reset, card hardware reset, CMD0, identification, select, user
  area.  Returns 0 or the RT_ST_* step that failed.
**/
STATIC
UINT32
RtEmmcInit (
  VOID
  )
{
  UINT32  Cap;
  UINT32  Ocr;
  UINT32  Resp;
  UINT8   Pwr;
  UINT8   PartConfig;
  UINTN   Count;

  // Clock gates for cclk/hclk/aclk/bclk/tclk_emmc (1 = gated).
  MmioWrite32 (mRtCru + RT_CRU_CLKGATE33, 0x1F00U << 16);

  MmioWrite8 (mRtSdhci + SDHC_SW_RST, BIT0);
  if (!RtWait16 (SDHC_SW_RST & ~1U, 0x0100, 0, 100000)) {
    return RT_ST_CTRL_RESET;
  }

  // What the reset clears and DwcSdhciDxe sets up.
  MmioOr32 (mRtSdhci + EMMC_MISC_CON, BIT1);       // internal clock
  MmioWrite32 (mRtSdhci + EMMC_HOST_CTRL3, 0);
  MmioWrite32 (mRtSdhci + EMMC_DLL_CTRL, BIT24 | BIT0);   // DLL bypass, start
  MmioWrite32 (mRtSdhci + EMMC_DLL_RXCLK, BIT31);
  MmioWrite32 (mRtSdhci + EMMC_DLL_TXCLK, 0);
  MmioWrite32 (mRtSdhci + EMMC_DLL_STRBIN, 0);

  //
  // Card hardware reset: a no-op unless RST_n is enabled in EXT_CSD, the
  // most thorough reset when it is.  CMD0 below does the rest either way.
  //
  MmioAnd32 (mRtSdhci + EMMC_EMMC_CTRL, ~(UINT32)BIT2);
  MicroSecondDelay (20);
  MmioOr32 (mRtSdhci + EMMC_EMMC_CTRL, BIT0 | BIT2);
  MicroSecondDelay (300);

  Cap = MmioRead32 (mRtSdhci + SDHC_CAP);
  if ((Cap & BIT24) != 0) {
    Pwr = 0x0F;        // 3.3 V
  } else if ((Cap & BIT25) != 0) {
    Pwr = 0x0D;        // 3.0 V
  } else {
    Pwr = 0x0B;        // 1.8 V
  }

  MmioWrite8 (mRtSdhci + SDHC_PWR_CTRL, Pwr);
  MmioWrite8 (mRtSdhci + SDHC_HOST_CTRL1, 0);      // 1 bit, default speed, no DMA
  MmioWrite16 (mRtSdhci + SDHC_HOST_CTRL2, 0);
  MmioWrite8 (mRtSdhci + SDHC_TIMEOUT, 0x0E);
  MmioWrite16 (mRtSdhci + SDHC_NOR_INT_EN, 0xFFFF);
  MmioWrite16 (mRtSdhci + SDHC_ERR_INT_EN, 0xFFFF);
  MmioWrite16 (mRtSdhci + SDHC_NOR_INT_SIG, 0);  // never interrupt the OS
  MmioWrite16 (mRtSdhci + SDHC_ERR_INT_SIG, 0);

  // 375 kHz for identification.
  if (!RtSetCardClock (64)) {
    return RT_ST_CTRL_RESET;
  }

  MicroSecondDelay (1000);

  //
  // CMD0 from any state, including Sleep (Linux sends CMD5 at shutdown) and
  // after a power-off notification (Windows, or Linux on hosts that can
  // power the card off).
  //
  if (!RtCmd (0, 0, CMD_RESP_NONE, FALSE, NULL)) {
    return RT_ST_CARD_IDLE;
  }

  MicroSecondDelay (1000);

  Ocr = 0;
  for (Count = 0; Count < 1000; Count++) {
    if (RtCmd (1, EMMC_OCR_REQ, CMD_RESP_R3, FALSE, &Ocr) && ((Ocr & BIT31) != 0)) {
      break;
    }

    MicroSecondDelay (1000);
  }

  if ((Ocr & BIT31) == 0) {
    return RT_ST_CARD_OCR;
  }

  mRtHighCapacity = (Ocr & BIT30) != 0;

  if (!RtCmd (2, 0, CMD_RESP_R2, FALSE, NULL) ||
      !RtCmd (3, EMMC_RCA, CMD_RESP_R1, FALSE, NULL) ||
      !RtCmd (7, EMMC_RCA, CMD_RESP_R1B, FALSE, NULL) ||
      !RtWaitTran ())
  {
    return RT_ST_CARD_SELECT;
  }

  // 12 MHz: default speed, 1 bit.
  if (!RtSetCardClock (2)) {
    return RT_ST_CARD_SELECT;
  }

  // EXT_CSD, for the partition the card is on.
  if (!RtCmd (8, 0, CMD_RESP_R1 | CMD_DATA, TRUE, &Resp)) {
    return RT_ST_EXT_CSD;
  }

  PartConfig = ((UINT8 *)mRtSector)[179];
  if ((PartConfig & 0x7) != 0) {
    //
    // Back to the user area, keeping the boot configuration bits.
    // SWITCH, access "write byte", index 179.
    //
    if (!RtCmd (6, (3U << 24) | (179U << 16) | ((UINT32)(PartConfig & ~0x7) << 8), CMD_RESP_R1B, FALSE, NULL) ||
        !RtWaitTran ())
    {
      return RT_ST_PARTITION;
    }
  }

  return 0;
}

STATIC
BOOLEAN
RtWriteSector (
  IN UINT64  Lba,
  IN UINT8   *Data
  )
{
  UINT32  Arg;
  UINTN   Try;

  Arg = (UINT32)(mRtHighCapacity ? Lba : Lba * 512);

  for (Try = 0; Try < 2; Try++) {
    CopyMem (mRtSector, Data, 512);
    if (!RtCmd (24, Arg, CMD_RESP_R1 | CMD_DATA, FALSE, NULL) || !RtWaitTran ()) {
      continue;
    }

    if (!RtCmd (17, Arg, CMD_RESP_R1 | CMD_DATA, TRUE, NULL) || !RtWaitTran ()) {
      continue;
    }

    if (CompareMem (mRtSector, Data, 512) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Write the sectors of the store that differ from the medium.  Called from
  ResetSystem() after ExitBootServices.
**/
STATIC
VOID
RkFvbRtEmmcCommit (
  VOID
  )
{
  UINT8   *Store;
  UINTN   Size;
  UINTN   VarSize;
  UINTN   Sectors;
  UINTN   Index;
  UINTN   Byte;
  UINTN   Changed;
  UINTN   Written;
  UINT32  Step;

  Store   = (UINT8 *)mRtDev->RegionBaseAddress;
  Size    = mRtDev->FvbSize;
  VarSize = FixedPcdGet32 (PcdFlashNvStorageVariableSize);
  Sectors = Size / 512;

  //
  // Only changes the variable driver makes at runtime: inside the variable
  // area, and only bits going from 1 to 0.  Anything else (a reclaim made
  // between the last boot-time dump and ExitBootServices) is not safe to
  // write in place and stays unwritten.
  //
  Changed = 0;
  for (Index = 0; Index < Sectors; Index++) {
    if (CompareMem (Store + Index * 512, mRtShadow + Index * 512, 512) == 0) {
      continue;
    }

    Changed++;
    if ((Index + 1) * 512 > VarSize) {
      RkFvbRtStatus (RT_ST_OUTSIDE_VARS, (UINT32)Index);
      return;
    }

    for (Byte = Index * 512; Byte < (Index + 1) * 512; Byte++) {
      if ((Store[Byte] & ~mRtShadow[Byte]) != 0) {
        RkFvbRtStatus (RT_ST_NOT_MONOTONIC, (UINT32)Index);
        return;
      }
    }
  }

  if (Changed == 0) {
    RkFvbRtStatus (RT_ST_NOTHING, 0);
    return;
  }

  if (((MmioRead32 (mRtPmu + RT_PMU_REPAIR_ST) & RT_PD_NVM_ON) == 0) ||
      ((MmioRead32 (mRtPmu + RT_PMU_IDLE_ST) & RT_PD_NVM_IDLE) != 0))
  {
    // Touching the controller with its power domain off would hang the bus.
    RkFvbRtStatus (RT_ST_PD_OFF, 0);
    return;
  }

  RkFvbRtStatus (RT_ST_BUSY, 0);
  Step = RtEmmcInit ();
  if (Step != 0) {
    RkFvbRtStatus (Step, 0);
    return;
  }

  //
  // Highest sector first: the appended variables before the state bits of
  // the ones they replace.
  //
  Written = 0;
  for (Index = Sectors; Index-- > 0;) {
    if (CompareMem (Store + Index * 512, mRtShadow + Index * 512, 512) == 0) {
      continue;
    }

    if (!RtWriteSector (mRtEmmcLba + Index, Store + Index * 512)) {
      RkFvbRtStatus (RT_ST_WRITE, (UINT32)Index);
      return;
    }

    CopyMem (mRtShadow + Index * 512, Store + Index * 512, 512);
    Written++;
  }

  RkFvbRtStatus (RT_ST_DONE, (UINT32)MIN (Written, 0xFF));
}

// ---------------------------------------------------------------------------
// Runtime service hooks
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
EFIAPI
RkFvbRtSetVariable (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *VendorGuid,
  IN UINT32    Attributes,
  IN UINTN     DataSize,
  IN VOID      *Data
  )
{
  EFI_STATUS  Status;

  Status = mRtOrigSetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);
  if (  !EFI_ERROR (Status)
     && (VariableName != NULL) && (VendorGuid != NULL)
     && CompareGuid (VendorGuid, &gEfiGlobalVariableGuid)
     && (StrCmp (VariableName, EFI_BOOT_NEXT_VARIABLE_NAME) == 0))
  {
    if ((DataSize == sizeof (UINT16)) && (Data != NULL) &&
        ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0))
    {
      RkFvbRtOsRegWrite (RT_OS_REG_BOOTNEXT, RT_BOOTNEXT_TAG | ReadUnaligned16 ((UINT16 *)Data));
    } else {
      RkFvbRtOsRegWrite (RT_OS_REG_BOOTNEXT, 0);
    }
  }

  return Status;
}

STATIC
VOID
EFIAPI
RkFvbRtResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  if (mRtEmmcEnabled && (mRtEmmcLba != 0) && mRtDirty && !mRtCommitting) {
    mRtCommitting = TRUE;
    RkFvbRtEmmcCommit ();
  }

  mRtOrigResetSystem (ResetType, ResetStatus, DataSize, ResetData);
}

// ---------------------------------------------------------------------------
// Boot time
// ---------------------------------------------------------------------------

STATIC
VOID
EFIAPI
RkFvbRtVariableWriteReady (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  VOID        *Protocol;
  UINT32      Regs[16];
  UINT32      Value;
  UINT16      Option;
  UINTN       Index;
  EFI_STATUS  Status;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiVariableWriteArchProtocolGuid, NULL, &Protocol))) {
    return;
  }

  gBS->CloseEvent (Event);

  for (Index = 0; Index < ARRAY_SIZE (Regs); Index++) {
    Regs[Index] = MmioRead32 (RT_PMU1_GRF_BASE + RT_PMU1_OS_REG (Index));
  }

  DEBUG ((
    DEBUG_INFO,
    "RkFvbRt: PMU1 GRF OS_REG0-15: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
    Regs[0], Regs[1], Regs[2], Regs[3], Regs[4], Regs[5], Regs[6], Regs[7],
    Regs[8], Regs[9], Regs[10], Regs[11], Regs[12], Regs[13], Regs[14], Regs[15]
    ));
  DEBUG ((
    DEBUG_INFO,
    "RkFvbRt: PMU0 GRF OS_REG16-23: %08x %08x %08x %08x %08x %08x %08x %08x\n",
    MmioRead32 (0x26024040), MmioRead32 (0x26024044), MmioRead32 (0x26024048), MmioRead32 (0x2602404C),
    MmioRead32 (0x26024050), MmioRead32 (0x26024054), MmioRead32 (0x26024058), MmioRead32 (0x2602405C)
    ));

  Value = Regs[RT_OS_REG_STATUS];
  if ((Value & RT_STATUS_MASK) == RT_STATUS_TAG) {
    DEBUG ((
      DEBUG_INFO,
      "RkFvbRt: last reset's eMMC commit: step 0x%02x detail %u (0x0c done, 0x0d nothing to write, 0x01 power domain off, 0x02/0x03 not an in-place change, 0x04-0x09 controller or card bring-up, 0x0a write/verify failed, 0x0e hung)\n",
      (Value >> 8) & 0xFF,
      Value & 0xFF
      ));
    MmioWrite32 (RT_PMU1_GRF_BASE + RT_PMU1_OS_REG (RT_OS_REG_STATUS), 0);
  }

  Value = Regs[RT_OS_REG_BOOTNEXT];
  if ((Value & RT_BOOTNEXT_MASK) == RT_BOOTNEXT_TAG) {
    Option = (UINT16)(Value & 0xFFFF);
    MmioWrite32 (RT_PMU1_GRF_BASE + RT_PMU1_OS_REG (RT_OS_REG_BOOTNEXT), 0);
    Status = gRT->SetVariable (
                    EFI_BOOT_NEXT_VARIABLE_NAME,
                    &gEfiGlobalVariableGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                    sizeof (Option),
                    &Option
                    );
    DEBUG ((DEBUG_INFO, "RkFvbRt: BootNext Boot%04x set by the OS last boot, restored: %r\n", Option, Status));
  } else if (Value != 0) {
    DEBUG ((DEBUG_WARN, "RkFvbRt: OS_REG%u holds 0x%08x, not ours; left alone\n", RT_OS_REG_BOOTNEXT, Value));
  }
}

/**
  SD/eMMC store, at driver start.  Map the OS register for runtime and put
  BootNext back once variables can be written.
**/
VOID
RkFvbRtInit (
  IN FVB_DEVICE  *FlashInstance
  )
{
  EFI_EVENT  Event;
  VOID       *Registration;

  mRtDev = FlashInstance;

  //
  // The store as it is on the medium, so that the runtime commit writes
  // only what changed.  The SPL loaded it unchanged into the buffer.
  //
  mRtShadow = AllocateRuntimeCopyPool (FlashInstance->FvbSize, (VOID *)FlashInstance->RegionBaseAddress);

  if (!EFI_ERROR (RkFvbRtRegisterMmio (RT_PMU1_GRF_BASE, RT_PMU1_GRF_SIZE))) {
    mRtPmu1Grf = (UINTN)RT_PMU1_GRF_BASE;
  }

  Event = EfiCreateProtocolNotifyEvent (
            &gEfiVariableWriteArchProtocolGuid,
            TPL_CALLBACK,
            RkFvbRtVariableWriteReady,
            NULL,
            &Registration
            );
  if (Event == NULL) {
    DEBUG ((DEBUG_ERROR, "RkFvbRt: cannot wait for variable writes; BootNext from the OS will not be restored\n"));
  }
}

/**
  The store has just been written to the medium in full.
**/
VOID
RkFvbRtMediumUpdated (
  VOID
  )
{
  if ((mRtShadow != NULL) && (mRtDev != NULL)) {
    CopyMem (mRtShadow, (VOID *)mRtDev->RegionBaseAddress, mRtDev->FvbSize);
  }
}

/**
  EndOfDxe: the boot medium is the eMMC and nvdata starts at Lba.
**/
VOID
RkFvbRtSetEmmcTarget (
  IN UINT64  Lba
  )
{
  EFI_STATUS  Status;
  UINT8       Enabled;
  UINTN       Size;

  Size   = sizeof (Enabled);
  Status = gRT->GetVariable (RT_EMMC_COMMIT_VARIABLE, &gRK3576DxeFormSetGuid, NULL, &Size, &Enabled);
  if (EFI_ERROR (Status) || (Size != sizeof (Enabled))) {
    Enabled = 0;
  }

  if ((Enabled == 0) || (mRtShadow == NULL)) {
    DEBUG ((DEBUG_INFO, "RkFvbRt: saving OS variable changes to the eMMC at reset is off\n"));
    return;
  }

  if (  EFI_ERROR (RkFvbRtRegisterMmio (FixedPcdGet32 (PcdDwcSdhciBaseAddress), RT_SDHCI_SIZE))
     || EFI_ERROR (RkFvbRtRegisterMmio (RT_CRU_BASE, RT_CRU_SIZE))
     || EFI_ERROR (RkFvbRtRegisterMmio (RT_PMU_BASE, RT_PMU_SIZE))
     || (mRtPmu1Grf == 0))
  {
    DEBUG ((DEBUG_ERROR, "RkFvbRt: eMMC registers not mapped for runtime; OS variable changes will not be saved\n"));
    return;
  }

  mRtSdhci       = FixedPcdGet32 (PcdDwcSdhciBaseAddress);
  mRtCru         = (UINTN)RT_CRU_BASE;
  mRtPmu         = (UINTN)RT_PMU_BASE;
  mRtEmmcLba     = Lba;
  mRtEmmcEnabled = TRUE;
  DEBUG ((DEBUG_INFO, "RkFvbRt: OS variable changes go to eMMC sectors %lu.. at reset\n", Lba));
}

VOID
RkFvbRtMarkDirty (
  VOID
  )
{
  mRtDirty = TRUE;
}

/**
  ExitBootServices: hook SetVariable (BootNext) and ResetSystem (eMMC).
**/
VOID
RkFvbRtExitBootServices (
  VOID
  )
{
  if (mRtPmu1Grf != 0) {
    mRtOrigSetVariable = gRT->SetVariable;
    gRT->SetVariable   = RkFvbRtSetVariable;
  }

  if (mRtEmmcEnabled) {
    mRtOrigResetSystem = gRT->ResetSystem;
    gRT->ResetSystem   = RkFvbRtResetSystem;
  }

  gRT->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &gRT->Hdr.CRC32);
}

VOID
RkFvbRtVirtualAddressChange (
  VOID
  )
{
  EfiConvertPointer (0x0, (VOID **)&mRtOrigSetVariable);
  EfiConvertPointer (0x0, (VOID **)&mRtOrigResetSystem);
  EfiConvertPointer (0x0, (VOID **)&mRtShadow);
  EfiConvertPointer (0x0, (VOID **)&mRtDev);
  if (mRtPmu1Grf != 0) {
    EfiConvertPointer (0x0, (VOID **)&mRtPmu1Grf);
  }

  if (mRtEmmcEnabled) {
    EfiConvertPointer (0x0, (VOID **)&mRtSdhci);
    EfiConvertPointer (0x0, (VOID **)&mRtCru);
    EfiConvertPointer (0x0, (VOID **)&mRtPmu);
  }
}
