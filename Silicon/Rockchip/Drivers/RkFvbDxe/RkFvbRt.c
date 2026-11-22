/** @file
 *
 *  Variable writes an OS makes after ExitBootServices, on SD and eMMC.
 *
 *  The store on these media is a DRAM buffer that only the boot-time NV dump
 *  writes back.  Nothing on RK3576 that firmware can reach keeps a value
 *  through the reset TF-A makes for SYSTEM_RESET: DRAM reads back as zeros
 *  (canary grid in RkFvbDxe.c), and the PMU1 GRF OS registers are cleared
 *  too, whatever the TRM says (Shell mm + reset on the TaishanPi 3M).  So
 *  the medium is the only place, and the one moment the firmware may drive
 *  it is the ResetSystem() an OS makes after it stopped using it.
 *
 *  On eMMC, optionally, a small polled driver for the SDHCI resets the
 *  controller and the card there and starts from CMD0, since the OS may
 *  have put the card to sleep or sent it a power-off notification.  Only
 *  sectors that changed are written, highest first, each one read back, and
 *  only if every change is one flash could make (bits going from 1 to 0).
 *  That is all the variable driver does at runtime: it appends at the end
 *  of the log and clears state bits further down, it never reclaims.
 *  Writing the appended sectors before the state bits means a power cut in
 *  the middle leaves either the old or the new value of a variable, never
 *  neither, and the FV and variable store headers, which a runtime write
 *  never touches, are never rewritten.  The last sector of the slot (end of
 *  the FTW spare area) carries a marker of what happened, read back on the
 *  next boot.  Off unless the "Save OS Variable Changes to eMMC" setup
 *  option is on.
 *
 *  Windows resets through PSCI and never calls ResetSystem() (measured on the
 *  TaishanPi 3M: no marker after shutdown /r), so for Windows BootNext alone
 *  is kept, in the RTC's timer registers, at the SetVariable() that sets it
 *  (see RTC_REG_TMR_CTL below), and put back before BDS on the next boot.
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
#include <Library/PrintLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/ExitBootServicesOsNotify.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipI2cMasterProtocol.h>
#include <Protocol/VariableWrite.h>

#include "RkFvbDxe.h"

//
// RK3576 registers (TRM V1.2 Part 1).
//
#define RT_PMU1_GRF_BASE   0x26026000ULL
#define RT_PMU1_OS_REG(n)  (0x200 + (n) * 4)   // printed at boot, not used: the reset clears them

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
// The marker the commit leaves in the last sector of the nvdata slot.
//
#define RT_EMMC_MARKER_SIGNATURE  SIGNATURE_64 ('R', 'K', 'F', 'V', 'B', 'M', 'K', '1')

typedef struct {
  UINT64    Signature;
  UINT32    Calls;       // ResetSystem() calls after ExitBootServices
  UINT32    Step;        // RT_ST_*
  UINT32    Detail;      // sectors written, or the sector a step failed at
  UINT32    ResetType;   // EFI_RESET_TYPE the OS asked for
  UINT32    Crc;         // CRC32 of the fields above
} RT_EMMC_MARKER;

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
STATIC UINT32            mRtCalls;        // ResetSystem() calls after ExitBootServices
STATIC UINT32            mRtResetType;
STATIC EFI_RESET_SYSTEM  mRtOrigResetSystem;

//
// BootNext for Windows, which resets through PSCI and never calls
// ResetSystem(): kept in the 8563 RTC's timer registers.
//
//   0x0E timer control: bit 7 TE (timer enable), bits 1:0 TD (frequency).
//                       Power-on value 0x03: TE 0, TD 1/60 Hz.
//   0x0F timer value:   8 bits.
//
// Neither Linux's rtc-hym8563, nor Hym8563RealTimeClockLib, nor anything
// else here writes them.  With TE kept 0 the timer does not run and its
// value is plain storage, and nothing else in the chip depends on TD.
// TD carries the tag: 3 (the power-on value) = nothing stored, 1 = BootNext
// is Boot00xx with xx in 0x0F, 2 = Boot01xx.  TD 0 is never written here,
// so a chip found with TD 0, or with TE set, belongs to someone else and is
// left alone.  BootNext above Boot01FF cannot be stored and is not.
//
// VL (seconds bit 7, the oscillator stopped) is not looked at: it says the
// time cannot be trusted, nothing about the timer registers, which a reboot
// keeps as long as the board stays powered (the TaishanPi 3M has no RTC
// battery; a power cut clears them to 0x03, i.e. nothing stored).  The chip
// is used only when control 1 is readable and not in test mode, as in
// Hym8563RealTimeClockLib.
//
#define RTC_I2C_ADDRESS   0x51
#define RTC_REG_CTL1      0x00
#define RTC_CTL1_TEST     BIT7
#define RTC_REG_TMR_CTL   0x0E
#define RTC_REG_TMR_CNT   0x0F
#define RTC_TMR_TE        BIT7
#define RTC_TMR_TD_MASK   0x03
#define RTC_TMR_TD_IDLE   0x03
#define RTC_TMR_TD_LOW    0x01
#define RTC_TMR_TD_HIGH   0x02

typedef struct {
  UINTN                OperationCount;
  EFI_I2C_OPERATION    Operation[2];
} RT_I2C_PACKET;

STATIC EFI_I2C_MASTER_PROTOCOL     *mRtI2c;        // set once the chip passed its checks
STATIC UINT8                       mRtTmrCtl;      // 0x0E as found, TD forced to idle
STATIC EXIT_BOOT_SERVICES_OS_TYPE  mRtOsType = ExitBootServicesOsUnknown;
STATIC BOOLEAN                     mRtVarWriteReady;
STATIC BOOLEAN                     mRtBootNextChecked;
STATIC VOID                        *mRtI2cRegistration;
STATIC EFI_SET_VARIABLE            mRtOrigSetVariable;
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
  Leave the marker: the last sector of the slot, which is the end of the FTW
  spare area.  Nothing reads the spare unless an FTW write was interrupted,
  so the sector is free; the next boot reads the marker from the store the
  SPL loaded and erases it there (RkFvbRtInit).  It is the only record of
  what ResetSystem() did that survives the reset: DRAM and the PMU1 GRF
  OS registers do not.  Only after a card init that worked.
**/
STATIC
VOID
RtWriteMarker (
  IN UINT32  Step,
  IN UINT32  Detail
  )
{
  RT_EMMC_MARKER  *Marker;
  UINT8           Sector[512];

  SetMem (Sector, sizeof (Sector), 0xFF);
  Marker            = (RT_EMMC_MARKER *)Sector;
  Marker->Signature = RT_EMMC_MARKER_SIGNATURE;
  Marker->Calls     = mRtCalls;
  Marker->Step      = Step;
  Marker->Detail    = Detail;
  Marker->ResetType = mRtResetType;
  Marker->Crc       = CalculateCrc32 (Marker, OFFSET_OF (RT_EMMC_MARKER, Crc));

  RtWriteSector (mRtEmmcLba + mRtDev->FvbSize / 512 - 1, Sector);
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
  UINTN   Written;
  UINT32  Refused;
  UINT32  RefusedAt;
  UINT32  Step;

  Store   = (UINT8 *)mRtDev->RegionBaseAddress;
  Size    = mRtDev->FvbSize;
  VarSize = FixedPcdGet32 (PcdFlashNvStorageVariableSize);
  Sectors = Size / 512 - 1;     // the last one holds the marker

  //
  // Only changes the variable driver makes at runtime: inside the variable
  // area, and only bits going from 1 to 0.  Anything else (a reclaim made
  // between the last boot-time dump and ExitBootServices) is not safe to
  // write in place and stays unwritten.
  //
  Refused   = 0;
  RefusedAt = 0;
  for (Index = 0; (Index < Sectors) && (Refused == 0); Index++) {
    if (CompareMem (Store + Index * 512, mRtShadow + Index * 512, 512) == 0) {
      continue;
    }

    if ((Index + 1) * 512 > VarSize) {
      Refused   = RT_ST_OUTSIDE_VARS;
      RefusedAt = (UINT32)Index;
      break;
    }

    for (Byte = Index * 512; Byte < (Index + 1) * 512; Byte++) {
      if ((Store[Byte] & ~mRtShadow[Byte]) != 0) {
        Refused   = RT_ST_NOT_MONOTONIC;
        RefusedAt = (UINT32)Index;
        break;
      }
    }
  }

  if (((MmioRead32 (mRtPmu + RT_PMU_REPAIR_ST) & RT_PD_NVM_ON) == 0) ||
      ((MmioRead32 (mRtPmu + RT_PMU_IDLE_ST) & RT_PD_NVM_IDLE) != 0))
  {
    // Touching the controller with its power domain off would hang the bus.
    return;
  }

  //
  // The card is brought up even with nothing to write, so that the marker
  // says ResetSystem() ran (diagnostic for the board bring-up).
  //
  Step = RtEmmcInit ();
  if (Step != 0) {
    return;
  }

  if (Refused != 0) {
    RtWriteMarker (Refused, RefusedAt);
    return;
  }

  RtWriteMarker (RT_ST_BUSY, 0);

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
      RtWriteMarker (RT_ST_WRITE, (UINT32)Index);
      return;
    }

    CopyMem (mRtShadow + Index * 512, Store + Index * 512, 512);
    Written++;
  }

  RtWriteMarker ((Written == 0) ? RT_ST_NOTHING : RT_ST_DONE, (UINT32)Written);
}

// ---------------------------------------------------------------------------
// Runtime service hooks
// ---------------------------------------------------------------------------

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
  mRtCalls++;
  mRtResetType = (UINT32)ResetType;
  if (mRtEmmcEnabled && (mRtEmmcLba != 0) && !mRtCommitting) {
    mRtCommitting = TRUE;
    RkFvbRtEmmcCommit ();
  }

  mRtOrigResetSystem (ResetType, ResetStatus, DataSize, ResetData);
}

// ---------------------------------------------------------------------------
// RTC timer registers.  Boot time and runtime.
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
RtcRead (
  IN  EFI_I2C_MASTER_PROTOCOL  *Master,
  IN  UINT8                    Reg,
  OUT UINT8                    *Value
  )
{
  EFI_STATUS     Status;
  RT_I2C_PACKET  Packet;

  Packet.OperationCount             = 2;
  Packet.Operation[0].Flags         = 0;
  Packet.Operation[0].LengthInBytes = sizeof (Reg);
  Packet.Operation[0].Buffer        = &Reg;
  Packet.Operation[1].Flags         = I2C_FLAG_READ;
  Packet.Operation[1].LengthInBytes = sizeof (*Value);
  Packet.Operation[1].Buffer        = Value;

  Status = Master->StartRequest (Master, RTC_I2C_ADDRESS, (EFI_I2C_REQUEST_PACKET *)&Packet, NULL, NULL);
  if (!EFI_ERROR (Status) && (Packet.Operation[1].LengthInBytes != sizeof (*Value))) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

STATIC
EFI_STATUS
RtcWrite (
  IN EFI_I2C_MASTER_PROTOCOL  *Master,
  IN UINT8                    Reg,
  IN UINT8                    Value
  )
{
  RT_I2C_PACKET  Packet;
  UINT8          Buffer[2];

  Buffer[0] = Reg;
  Buffer[1] = Value;

  Packet.OperationCount             = 1;
  Packet.Operation[0].Flags         = 0;
  Packet.Operation[0].LengthInBytes = sizeof (Buffer);
  Packet.Operation[0].Buffer        = Buffer;

  return Master->StartRequest (Master, RTC_I2C_ADDRESS, (EFI_I2C_REQUEST_PACKET *)&Packet, NULL, NULL);
}

/**
  Store BootNext in the timer registers, or clear them.  The value first,
  then the tag, so an interrupted write leaves "nothing stored".
**/
STATIC
VOID
RtcStoreBootNext (
  IN BOOLEAN  Set,
  IN UINT16   Option
  )
{
  if (Set && (Option <= 0x1FF)) {
    if (!EFI_ERROR (RtcWrite (mRtI2c, RTC_REG_TMR_CNT, (UINT8)Option))) {
      RtcWrite (mRtI2c, RTC_REG_TMR_CTL, (UINT8)((mRtTmrCtl & ~RTC_TMR_TD_MASK) | ((Option >= 0x100) ? RTC_TMR_TD_HIGH : RTC_TMR_TD_LOW)));
    }
  } else {
    RtcWrite (mRtI2c, RTC_REG_TMR_CTL, mRtTmrCtl);
  }
}

/**
  SetVariable after ExitBootServices.  Under Windows only: Linux drives the
  RTC's bus itself, and Linux calls ResetSystem(), where the eMMC commit
  saves BootNext with everything else.
**/
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
     && (mRtI2c != NULL)
     && (mRtOsType == ExitBootServicesOsWindows)
     && (VariableName != NULL) && (VendorGuid != NULL)
     && CompareGuid (VendorGuid, &gEfiGlobalVariableGuid)
     && (StrCmp (VariableName, EFI_BOOT_NEXT_VARIABLE_NAME) == 0))
  {
    RtcStoreBootNext (
      (DataSize == sizeof (UINT16)) && (Data != NULL) && ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0),
      ((Data != NULL) && (DataSize == sizeof (UINT16))) ? ReadUnaligned16 ((UINT16 *)Data) : 0
      );
  }

  return Status;
}

/**
  Put back a BootNext Windows left in the RTC, once the chip has been found
  and variables can be written.  Before BDS: both happen during DXE dispatch.
**/
STATIC
VOID
RtcRestoreBootNext (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Ctl;
  UINT8       Cnt;
  UINT16      Option;
  CHAR16      Name[sizeof ("Boot####")];
  UINTN       Size;

  if ((mRtI2c == NULL) || !mRtVarWriteReady || mRtBootNextChecked) {
    return;
  }

  mRtBootNextChecked = TRUE;

  if (EFI_ERROR (RtcRead (mRtI2c, RTC_REG_TMR_CTL, &Ctl)) ||
      EFI_ERROR (RtcRead (mRtI2c, RTC_REG_TMR_CNT, &Cnt)))
  {
    DEBUG ((DEBUG_WARN, "RkFvbRt: RTC timer registers unreadable; no BootNext from the RTC\n"));
    return;
  }

  if ((Ctl & RTC_TMR_TD_MASK) == RTC_TMR_TD_IDLE) {
    DEBUG ((DEBUG_INFO, "RkFvbRt: no BootNext in the RTC (timer 0x%02x 0x%02x)\n", Ctl, Cnt));
    return;
  }

  Option = (UINT16)(Cnt + (((Ctl & RTC_TMR_TD_MASK) == RTC_TMR_TD_HIGH) ? 0x100 : 0));

  // Cleared before use, so it acts once whatever happens next.
  RtcWrite (mRtI2c, RTC_REG_TMR_CTL, mRtTmrCtl);

  UnicodeSPrint (Name, sizeof (Name), L"Boot%04X", Option);
  Size   = 0;
  Status = gRT->GetVariable (Name, &gEfiGlobalVariableGuid, NULL, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_WARN, "RkFvbRt: the RTC held BootNext %s, which does not exist; dropped\n", Name));
    return;
  }

  Status = gRT->SetVariable (
                  EFI_BOOT_NEXT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Option),
                  &Option
                  );
  DEBUG ((DEBUG_INFO, "RkFvbRt: BootNext %s from the RTC (set by Windows last boot): %r\n", Name, Status));
}

STATIC
VOID
EFIAPI
RtcI2cReady (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    Handle;
  UINTN                         BufferSize;
  ROCKCHIP_I2C_MASTER_PROTOCOL  *RockchipI2c;
  EFI_I2C_MASTER_PROTOCOL       *Master;
  UINT8                         Ctl1;
  UINT8                         TmrCtl;

  for ( ; ; ) {
    BufferSize = sizeof (Handle);
    Status     = gBS->LocateHandle (ByRegisterNotify, &gRockchipI2cMasterProtocolGuid, mRtI2cRegistration, &BufferSize, &Handle);
    if (EFI_ERROR (Status)) {
      return;
    }

    Status = gBS->HandleProtocol (Handle, &gRockchipI2cMasterProtocolGuid, (VOID **)&RockchipI2c);
    if (EFI_ERROR (Status) || (RockchipI2c->Bus != FixedPcdGet8 (PcdRtc8563Bus))) {
      continue;
    }

    Status = gBS->HandleProtocol (Handle, &gEfiI2cMasterProtocolGuid, (VOID **)&Master);
    if (EFI_ERROR (Status)) {
      continue;
    }

    gBS->CloseEvent (Event);

    if (  EFI_ERROR (RtcRead (Master, RTC_REG_CTL1, &Ctl1))
       || ((Ctl1 & RTC_CTL1_TEST) != 0)
       || EFI_ERROR (RtcRead (Master, RTC_REG_TMR_CTL, &TmrCtl)))
    {
      DEBUG ((DEBUG_WARN, "RkFvbRt: no usable RTC on I2C%u; BootNext from Windows will not survive a reboot\n", FixedPcdGet8 (PcdRtc8563Bus)));
      return;
    }

    if (((TmrCtl & RTC_TMR_TE) != 0) || ((TmrCtl & RTC_TMR_TD_MASK) == 0)) {
      DEBUG ((DEBUG_WARN, "RkFvbRt: RTC timer control 0x%02x is in use by something else; left alone\n", TmrCtl));
      return;
    }

    mRtTmrCtl = (UINT8)((TmrCtl & ~RTC_TMR_TE) | RTC_TMR_TD_IDLE);
    mRtI2c    = Master;
    RtcRestoreBootNext ();
    return;
  }
}

STATIC
VOID
EFIAPI
RtVariableWriteReady (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  VOID  *Protocol;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiVariableWriteArchProtocolGuid, NULL, &Protocol))) {
    return;
  }

  gBS->CloseEvent (Event);
  mRtVarWriteReady = TRUE;
  RtcRestoreBootNext ();
}

STATIC
VOID
EFIAPI
RtOsHandler (
  IN EXIT_BOOT_SERVICES_OS_CONTEXT  *Context
  )
{
  mRtOsType = Context->OsType;
}

STATIC
VOID
EFIAPI
RtOsNotifyReady (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EXIT_BOOT_SERVICES_OS_NOTIFY_PROTOCOL  *Notify;

  if (EFI_ERROR (gBS->LocateProtocol (&gExitBootServicesOsNotifyProtocolGuid, NULL, (VOID **)&Notify))) {
    return;
  }

  gBS->CloseEvent (Event);
  Notify->RegisterHandler (Notify, RtOsHandler);
}

/**
  BootNext through the RTC, for Windows.  Only on a board that names the
  RTC's bus (PcdRtc8563Bus, 0 = none).
**/
STATIC
VOID
RtcBootNextInit (
  VOID
  )
{
  VOID  *Registration;

  if (FixedPcdGet8 (PcdRtc8563Bus) == 0) {
    return;
  }

  EfiCreateProtocolNotifyEvent (&gRockchipI2cMasterProtocolGuid, TPL_CALLBACK, RtcI2cReady, NULL, &mRtI2cRegistration);
  EfiCreateProtocolNotifyEvent (&gEfiVariableWriteArchProtocolGuid, TPL_CALLBACK, RtVariableWriteReady, NULL, &Registration);
  EfiCreateProtocolNotifyEvent (&gExitBootServicesOsNotifyProtocolGuid, TPL_CALLBACK, RtOsNotifyReady, NULL, &Registration);
}

// ---------------------------------------------------------------------------
// Boot time
// ---------------------------------------------------------------------------

/**
  SD/eMMC store, at driver start, before anything changes the buffer the SPL
  loaded.  Report and erase the marker the last ResetSystem() left, then
  keep a copy of the store as it is on the medium.
**/
VOID
RkFvbRtInit (
  IN FVB_DEVICE  *FlashInstance
  )
{
  RT_EMMC_MARKER  *Marker;
  UINT8           *Last;
  UINTN           Index;
  UINT32          Pmu1[16];

  mRtDev = FlashInstance;

  for (Index = 0; Index < ARRAY_SIZE (Pmu1); Index++) {
    Pmu1[Index] = MmioRead32 (RT_PMU1_GRF_BASE + RT_PMU1_OS_REG (Index));
  }

  DEBUG ((
    DEBUG_INFO,
    "RkFvbRt: PMU1 GRF OS_REG0-15: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
    Pmu1[0], Pmu1[1], Pmu1[2], Pmu1[3], Pmu1[4], Pmu1[5], Pmu1[6], Pmu1[7],
    Pmu1[8], Pmu1[9], Pmu1[10], Pmu1[11], Pmu1[12], Pmu1[13], Pmu1[14], Pmu1[15]
    ));
  DEBUG ((
    DEBUG_INFO,
    "RkFvbRt: PMU0 GRF OS_REG16-23: %08x %08x %08x %08x %08x %08x %08x %08x\n",
    MmioRead32 (0x26024040), MmioRead32 (0x26024044), MmioRead32 (0x26024048), MmioRead32 (0x2602404C),
    MmioRead32 (0x26024050), MmioRead32 (0x26024054), MmioRead32 (0x26024058), MmioRead32 (0x2602405C)
    ));

  Last   = (UINT8 *)FlashInstance->RegionBaseAddress + FlashInstance->FvbSize - 512;
  Marker = (RT_EMMC_MARKER *)Last;
  if (  (Marker->Signature == RT_EMMC_MARKER_SIGNATURE)
     && (Marker->Crc == CalculateCrc32 (Marker, OFFSET_OF (RT_EMMC_MARKER, Crc))))
  {
    DEBUG ((
      DEBUG_INFO,
      "RkFvbRt: the last OS called ResetSystem() (call %u, reset type %u); eMMC commit step 0x%02x, detail %u"
      " (0x0c written, 0x0d nothing to write, 0x02/0x03 refused: not an in-place change at sector detail, 0x0a write or verify failed at sector detail, 0x0e stopped midway)\n",
      Marker->Calls,
      Marker->ResetType,
      Marker->Step,
      Marker->Detail
      ));
    // Back to erased, here and in what the next dump writes.
    SetMem (Last, 512, 0xFF);
  } else {
    DEBUG ((DEBUG_INFO, "RkFvbRt: no eMMC commit marker from the last reset\n"));
  }

  //
  // The store as it is on the medium (but for the marker sector, which the
  // commit never compares), so that the commit writes only what changed.
  //
  mRtShadow = AllocateRuntimeCopyPool (FlashInstance->FvbSize, (VOID *)FlashInstance->RegionBaseAddress);

  RtcBootNextInit ();
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
     || EFI_ERROR (RkFvbRtRegisterMmio (RT_PMU_BASE, RT_PMU_SIZE)))
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
  ExitBootServices: hook SetVariable (BootNext to the RTC, under Windows)
  and ResetSystem (eMMC commit).
**/
VOID
RkFvbRtExitBootServices (
  VOID
  )
{
  if (mRtI2c != NULL) {
    mRtOrigSetVariable = gRT->SetVariable;
    gRT->SetVariable   = RkFvbRtSetVariable;
  }

  if (mRtEmmcEnabled) {
    mRtOrigResetSystem = gRT->ResetSystem;
    gRT->ResetSystem   = RkFvbRtResetSystem;
  }

  gRT->Hdr.CRC32     = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &gRT->Hdr.CRC32);
}

VOID
RkFvbRtVirtualAddressChange (
  VOID
  )
{
  EfiConvertPointer (0x0, (VOID **)&mRtOrigResetSystem);
  if (mRtI2c != NULL) {
    EfiConvertPointer (0x0, (VOID **)&mRtOrigSetVariable);
    EfiConvertPointer (0x0, (VOID **)&mRtI2c);
  }
  EfiConvertPointer (0x0, (VOID **)&mRtShadow);
  EfiConvertPointer (0x0, (VOID **)&mRtDev);
  if (mRtEmmcEnabled) {
    EfiConvertPointer (0x0, (VOID **)&mRtSdhci);
    EfiConvertPointer (0x0, (VOID **)&mRtCru);
    EfiConvertPointer (0x0, (VOID **)&mRtPmu);
  }
}
