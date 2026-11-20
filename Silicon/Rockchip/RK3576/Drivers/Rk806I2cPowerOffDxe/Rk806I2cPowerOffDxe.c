/** @file
 *
 *  Power the board off through an RK806 PMIC on I2C.
 *
 *  A shutdown ends in PSCI SYSTEM_OFF.  TF-A answers it by driving
 *  GPIO0_A3 (PWR_CTRL1) high, which powers the board off only if the RK806
 *  was told beforehand that its PWRCTRL1 pin means "power off".  Linux's
 *  RK806 driver does that, or sets the PMIC's DEV_OFF bit itself; Windows
 *  has no PMIC driver, and on a board whose RK806 sits on I2C the firmware
 *  never talked to it.  So a shutdown from Windows or the UEFI Shell
 *  stopped the system and left it powered, with the last frame on screen.
 *
 *  Telling the PMIC at boot is not an option: GPIO0_A3 comes out of reset
 *  pulled up and the PMIC keeps its settings across a SoC reset, so the
 *  next reboot would power the board off.  This driver instead wraps
 *  gRT->ResetSystem and, for a shutdown only, sets DEV_OFF (SYS_CFG3 bit 0)
 *  over I2C at that moment, the way mainline Linux powers an RK806 off.  It
 *  runs at boot time (UEFI Shell "reset -s") and at runtime (an OS calling
 *  ResetSystem); the I2C bus is declared runtime in I2cDxe, which maps its
 *  registers for the OS and programs its clock divider and pins at boot.
 *  If the write fails, the original ResetSystem runs as before.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

//
// Rockchip I2C controller (the same block I2cDxe drives).
//
#define I2C_CON         0x000
#define I2C_CON_EN      BIT0
#define I2C_CON_MOD(m)  ((m) << 1)
#define I2C_CON_START   BIT3
#define I2C_CON_STOP    BIT4
#define I2C_CON_LASTACK BIT5
#define I2C_CON_CFG     0xFF00      // SDA/start/stop timing set by I2cDxe
#define I2C_MODE_TX     0
#define I2C_MODE_TRX    1
#define I2C_MRXADDR     0x008
#define I2C_MRXRADDR    0x00C
#define I2C_MTXCNT      0x010
#define I2C_MRXCNT      0x014
#define I2C_IEN         0x018
#define I2C_IPD         0x01C
#define I2C_IPD_MBTF    BIT2
#define I2C_IPD_MBRF    BIT3
#define I2C_IPD_START   BIT4
#define I2C_IPD_STOP    BIT5
#define I2C_IPD_NAK     BIT6
#define I2C_IPD_ALL     0x7F
#define I2C_TXDATA0     0x100
#define I2C_RXDATA0     0x200
#define I2C_TIMEOUT_US  100000

//
// RK806 registers (include/linux/mfd/rk808.h).
//
#define RK806_CHIP_NAME      0x5A
#define RK806_CHIP_VER       0x5B
#define RK806_SLEEP_CONFIG0  0x62
#define RK806_SYS_CFG3       0x72
#define RK806_DEV_OFF        BIT0

STATIC UINTN             mI2cBase;
STATIC UINT8             mAddress;
STATIC EFI_RESET_SYSTEM  mOriginalResetSystem;
STATIC EFI_EVENT         mVirtualAddressChangeEvent;

STATIC
EFI_STATUS
I2cWaitFor (
  IN UINT32  Bits
  )
{
  UINTN   Timeout;
  UINT32  Ipd;

  for (Timeout = 0; Timeout < I2C_TIMEOUT_US; Timeout++) {
    Ipd = MmioRead32 (mI2cBase + I2C_IPD);
    if ((Ipd & I2C_IPD_NAK) != 0) {
      MmioWrite32 (mI2cBase + I2C_IPD, I2C_IPD_NAK);
      return EFI_NO_RESPONSE;
    }

    if ((Ipd & Bits) != 0) {
      MmioWrite32 (mI2cBase + I2C_IPD, Bits);
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

STATIC
VOID
I2cStop (
  IN UINT32  Cfg
  )
{
  MmioWrite32 (mI2cBase + I2C_IPD, I2C_IPD_ALL);
  MmioWrite32 (mI2cBase + I2C_CON, I2C_CON_EN | I2C_CON_STOP | Cfg);
  I2cWaitFor (I2C_IPD_STOP);
  MmioWrite32 (mI2cBase + I2C_CON, Cfg);
}

STATIC
EFI_STATUS
I2cStart (
  IN UINT32  Con,
  IN UINT32  Cfg
  )
{
  MmioWrite32 (mI2cBase + I2C_IPD, I2C_IPD_ALL);
  MmioWrite32 (mI2cBase + I2C_IEN, 0);
  MmioWrite32 (mI2cBase + I2C_CON, I2C_CON_EN | I2C_CON_START | Con | Cfg);
  return I2cWaitFor (I2C_IPD_START);
}

STATIC
EFI_STATUS
Rk806Write (
  IN UINT8  Reg,
  IN UINT8  Value
  )
{
  EFI_STATUS  Status;
  UINT32      Cfg;

  Cfg = MmioRead32 (mI2cBase + I2C_CON) & I2C_CON_CFG;
  MmioWrite32 (mI2cBase + I2C_CON, Cfg);

  MmioWrite32 (mI2cBase + I2C_TXDATA0, ((UINT32)mAddress << 1) | ((UINT32)Reg << 8) | ((UINT32)Value << 16));
  Status = I2cStart (I2C_CON_MOD (I2C_MODE_TX), Cfg);
  if (!EFI_ERROR (Status)) {
    MmioWrite32 (mI2cBase + I2C_CON, I2C_CON_EN | I2C_CON_MOD (I2C_MODE_TX) | Cfg);
    MmioWrite32 (mI2cBase + I2C_MTXCNT, 3);
    Status = I2cWaitFor (I2C_IPD_MBTF);
  }

  I2cStop (Cfg);
  return Status;
}

STATIC
EFI_STATUS
Rk806Read (
  IN  UINT8  Reg,
  OUT UINT8  *Value
  )
{
  EFI_STATUS  Status;
  UINT32      Cfg;

  Cfg = MmioRead32 (mI2cBase + I2C_CON) & I2C_CON_CFG;
  MmioWrite32 (mI2cBase + I2C_CON, Cfg);

  MmioWrite32 (mI2cBase + I2C_MRXADDR, BIT24 | ((UINT32)mAddress << 1) | 1);
  MmioWrite32 (mI2cBase + I2C_MRXRADDR, BIT24 | Reg);
  Status = I2cStart (I2C_CON_MOD (I2C_MODE_TRX) | I2C_CON_LASTACK, Cfg);
  if (!EFI_ERROR (Status)) {
    MmioWrite32 (mI2cBase + I2C_MRXCNT, 1);
    Status = I2cWaitFor (I2C_IPD_MBRF);
    if (!EFI_ERROR (Status)) {
      *Value = (UINT8)MmioRead32 (mI2cBase + I2C_RXDATA0);
    }
  }

  I2cStop (Cfg);
  return Status;
}

STATIC
EFI_STATUS
Rk806PowerOff (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Cfg3;

  if (EFI_ERROR (Rk806Read (RK806_SYS_CFG3, &Cfg3))) {
    Cfg3 = 0;
  }

  Status = Rk806Write (RK806_SYS_CFG3, Cfg3 | RK806_DEV_OFF);
  if (!EFI_ERROR (Status)) {
    //
    // The rails go down within a few ms; anything after this is the
    // fallback for a PMIC that did not act.
    //
    MicroSecondDelay (500 * 1000);
  }

  return Status;
}

STATIC
VOID
EFIAPI
Rk806ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  EFI_STATUS  Status;

  if (ResetType == EfiResetShutdown) {
    Status = Rk806PowerOff ();
    if (!EfiAtRuntime ()) {
      DEBUG ((DEBUG_ERROR, "Rk806PowerOff: DEV_OFF write %r, PMIC did not cut power, trying PSCI\n", Status));
    }
  }

  mOriginalResetSystem (ResetType, ResetStatus, DataSize, ResetData);
}

STATIC
VOID
EFIAPI
Rk806VirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EfiConvertPointer (0x0, (VOID **)&mI2cBase);
  EfiConvertPointer (0x0, (VOID **)&mOriginalResetSystem);
}

/**
  Say at ReadyToBoot whether the PMIC answers, and how it is set, so that a
  failed power-off can be told apart from a PMIC that is not there.
**/
STATIC
VOID
EFIAPI
Rk806ReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT8  Name, Ver, Sleep0, Cfg3;

  gBS->CloseEvent (Event);

  if (EFI_ERROR (Rk806Read (RK806_CHIP_NAME, &Name)) ||
      EFI_ERROR (Rk806Read (RK806_CHIP_VER, &Ver)) ||
      EFI_ERROR (Rk806Read (RK806_SLEEP_CONFIG0, &Sleep0)) ||
      EFI_ERROR (Rk806Read (RK806_SYS_CFG3, &Cfg3)))
  {
    DEBUG ((DEBUG_WARN, "Rk806PowerOff: no answer from the PMIC at I2C 0x%lx/0x%02x\n", (UINT64)mI2cBase, mAddress));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "Rk806PowerOff: RK806 at I2C 0x%lx/0x%02x, CHIP_NAME 0x%02x VER 0x%02x SLEEP_CONFIG0 0x%02x SYS_CFG3 0x%02x\n",
    (UINT64)mI2cBase,
    mAddress,
    Name,
    Ver,
    Sleep0,
    Cfg3
    ));
}

EFI_STATUS
EFIAPI
Rk806I2cPowerOffDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_TPL     Tpl;
  EFI_EVENT   ReadyToBootEvent;

  mI2cBase = (UINTN)FixedPcdGet32 (PcdRk806I2cBase);
  mAddress = FixedPcdGet8 (PcdRk806I2cAddress);
  if (mI2cBase == 0) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  Rk806VirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddressChangeEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  EfiCreateEventReadyToBootEx (TPL_CALLBACK, Rk806ReadyToBoot, NULL, &ReadyToBootEvent);

  //
  // Wrap ResetSystem.  The table carries a CRC, so that is redone.
  //
  Tpl                  = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  mOriginalResetSystem = gRT->ResetSystem;
  gRT->ResetSystem     = Rk806ResetSystem;
  gRT->Hdr.CRC32       = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &gRT->Hdr.CRC32);
  gBS->RestoreTPL (Tpl);

  return EFI_SUCCESS;
}
