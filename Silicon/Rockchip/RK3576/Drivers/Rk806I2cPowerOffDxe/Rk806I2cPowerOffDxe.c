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
 *  Windows on Arm does not go through ResetSystem: with PSCI in the FADT it
 *  calls SYSTEM_OFF itself, so its shutdown froze on the last frame while
 *  "Shut Down" in the firmware's menu worked.  For that path the PMIC has
 *  to act on PWR_CTRL1 after all.  So at ExitBootServices, and only then,
 *  GPIO0_A3 is made a GPIO driven low and PWRCTRL1 is set to power off;
 *  TF-A's SYSTEM_OFF then raises the pin and the PMIC cuts power.  At every
 *  boot the driver puts PWRCTRL1 back to "null" first thing and logs how it
 *  found the pin, which tells whether a warm reset (PSCI SYSTEM_RESET, a
 *  CRU global reset) keeps GPIO0_A3 low.  If it does not, a restart from
 *  the OS becomes a power-off; the next power-on is a cold start with the
 *  PMIC back at its defaults.
 *
 *  With "Power button (RK806)" on in setup (PcdAcpiPwrKey) this driver also
 *  gets the PMIC's interrupt ready for woa-rk3576 rk806pwrkey
 *  (AcpiTables/PwrKey.asl): at boot GPIO0_A6, the PMIC INT line on every
 *  RK3576 board here, becomes a GPIO input with its pull-up (mainline
 *  rk3576-pinctrl.dtsi pmic_pins: pull_up; the Windows GPIO driver sets no
 *  pin function or pull), and at ExitBootServices the PMIC's interrupt is
 *  made active low (GPIO_INT_CONFIG bit 1 = 0, as mainline rk8xx-core.c
 *  rk806_pre_init_reg) with everything but PWRON_FALL and PWRON_RISE masked
 *  and whatever is pending cleared, so that a stale event (the power-on
 *  itself, VDC_RISE) does not hold the line low before the driver starts.
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
#define RK806_PWRCTRL1_FUN   0x07
#define RK806_FUN_NULL       0
#define RK806_FUN_POWOFF     2
#define RK806_INT_STS0       0x77
#define RK806_INT_MSK0       0x78
#define RK806_INT_STS1       0x79
#define RK806_INT_MSK1       0x7A
#define RK806_GPIO_INT_CONFIG 0x7B
#define RK806_INT_POL_H      BIT1
#define RK806_PWRON_EVENTS   (BIT0 | BIT1)   // PWRON_FALL, PWRON_RISE

//
// GPIO0_A3 = PWR_CTRL1 (RK3576 TRM, PMU0_IOC_GPIO0A_IOMUX_SEL_L), the pin
// TF-A raises for SYSTEM_OFF (plat/rockchip/rk3576/drivers/pmu/pmu.c).
//
#define PMU0_IOC_GPIO0A_IOMUX_SEL_L  0x26040000
#define GPIO0_BASE                   0x27320000
#define GPIO_SWPORT_DR_L             0x0000
#define GPIO_SWPORT_DDR_L            0x0008
#define GPIO_EXT_PORT                0x0070
#define GPIO0_A3                     BIT3
#define PMU0_IOC_GPIO0A_IOMUX_SEL_H  0x26040004   // GPIO0_A4..A7, 4 bits each
#define PMU0_IOC_GPIO0A_PULL         0x26040020   // RK3576_PULL_GPIO0_AL_OFFSET
#define GPIO0_A6                     BIT6

STATIC UINTN             mI2cBase;
STATIC UINT8             mAddress;
STATIC EFI_RESET_SYSTEM  mOriginalResetSystem;
STATIC EFI_EVENT         mVirtualAddressChangeEvent;
STATIC EFI_EVENT         mExitBootServicesEvent;
STATIC BOOLEAN           mPwrKey;

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
  The PMIC interrupt for the OS's power button driver: active low, only the
  power key's press and release unmasked, nothing pending.
**/
STATIC
VOID
Rk806PwrKeyArm (
  VOID
  )
{
  UINT8  Config;

  if (!EFI_ERROR (Rk806Read (RK806_GPIO_INT_CONFIG, &Config))) {
    Rk806Write (RK806_GPIO_INT_CONFIG, Config & ~RK806_INT_POL_H);
  }

  Rk806Write (RK806_INT_MSK0, (UINT8)~RK806_PWRON_EVENTS);
  Rk806Write (RK806_INT_MSK1, 0xFF);
  Rk806Write (RK806_INT_STS0, 0xFF);
  Rk806Write (RK806_INT_STS1, 0xFF);
}

/**
  GPIO0_A6, the PMIC INT line: GPIO function, input, pulled up.
**/
STATIC
VOID
Rk806PwrKeyPin (
  VOID
  )
{
  UINT8  Sts0;
  UINT8  Msk0;

  MmioWrite32 (PMU0_IOC_GPIO0A_IOMUX_SEL_H, (0xFU << (8 + 16)) | 0);
  MmioWrite32 (PMU0_IOC_GPIO0A_PULL, (0x3U << (12 + 16)) | (0x3U << 12));
  MmioWrite32 (GPIO0_BASE + GPIO_SWPORT_DDR_L, (GPIO0_A6 << 16) | 0);

  if (EFI_ERROR (Rk806Read (RK806_INT_STS0, &Sts0)) ||
      EFI_ERROR (Rk806Read (RK806_INT_MSK0, &Msk0)))
  {
    Sts0 = 0xFF;
    Msk0 = 0xFF;
  }

  DEBUG ((
    DEBUG_INFO,
    "Rk806PowerOff: power button on: GPIO0_A6 GPIO in, pull-up, level %u; INT_STS0 0x%02x INT_MSK0 0x%02x\n",
    (MmioRead32 (GPIO0_BASE + GPIO_EXT_PORT) & GPIO0_A6) != 0,
    Sts0,
    Msk0
    ));
}

/**
  Arm the PMIC for TF-A's SYSTEM_OFF: GPIO0_A3 low first, then PWRCTRL1 =
  power off.  Order matters: with the pin high (its reset pull is up) the
  second write would cut power on the spot.
**/
STATIC
VOID
EFIAPI
Rk806ExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT8  Sleep0;

  if (mPwrKey) {
    Rk806PwrKeyArm ();
  }

  MmioWrite32 (GPIO0_BASE + GPIO_SWPORT_DR_L, (GPIO0_A3 << 16) | 0);
  MmioWrite32 (GPIO0_BASE + GPIO_SWPORT_DDR_L, (GPIO0_A3 << 16) | GPIO0_A3);
  MmioWrite32 (PMU0_IOC_GPIO0A_IOMUX_SEL_L, (0xFU << (12 + 16)) | 0);
  MicroSecondDelay (10);

  if ((MmioRead32 (GPIO0_BASE + GPIO_EXT_PORT) & GPIO0_A3) != 0) {
    return;   // the pin does not read low: leave the PMIC alone
  }

  if (EFI_ERROR (Rk806Read (RK806_SLEEP_CONFIG0, &Sleep0))) {
    return;
  }

  Rk806Write (RK806_SLEEP_CONFIG0, (Sleep0 & ~RK806_PWRCTRL1_FUN) | RK806_FUN_POWOFF);
}

/**
  First thing at every boot: PWRCTRL1 back to "null", so that nothing the
  firmware does with GPIO0_A3 can power the board off, and a record of how
  the pin came out of the reset.
**/
STATIC
VOID
Rk806DisarmAtBoot (
  VOID
  )
{
  UINT8   Sleep0;
  UINT32  Dr, Ddr, Ext, Mux;

  Dr  = MmioRead32 (GPIO0_BASE + GPIO_SWPORT_DR_L);
  Ddr = MmioRead32 (GPIO0_BASE + GPIO_SWPORT_DDR_L);
  Ext = MmioRead32 (GPIO0_BASE + GPIO_EXT_PORT);
  Mux = MmioRead32 (PMU0_IOC_GPIO0A_IOMUX_SEL_L);

  if (EFI_ERROR (Rk806Read (RK806_SLEEP_CONFIG0, &Sleep0))) {
    DEBUG ((DEBUG_WARN, "Rk806PowerOff: PMIC not answering at entry\n"));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "Rk806PowerOff: at entry SLEEP_CONFIG0 0x%02x, GPIO0_A3 mux %u dir %a out %u level %u\n",
    Sleep0,
    (Mux >> 12) & 0xF,
    ((Ddr & GPIO0_A3) != 0) ? "out" : "in",
    (Dr & GPIO0_A3) != 0,
    (Ext & GPIO0_A3) != 0
    ));

  if ((Sleep0 & RK806_PWRCTRL1_FUN) != RK806_FUN_NULL) {
    Rk806Write (RK806_SLEEP_CONFIG0, Sleep0 & ~RK806_PWRCTRL1_FUN);
    DEBUG ((DEBUG_INFO, "Rk806PowerOff: PWRCTRL1 was armed (warm reset kept the board on), set back to null\n"));
  }
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

  Rk806DisarmAtBoot ();

  //
  // Only on a board with a button on PWRON (PcdAcpiPwrKeySupported): a
  // stored AcpiPwrKey = 1 on any other board is ignored.
  //
  mPwrKey = FixedPcdGetBool (PcdAcpiPwrKeySupported) && (PcdGet8 (PcdAcpiPwrKey) != 0);
  if (mPwrKey) {
    Rk806PwrKeyPin ();
  }

  EfiCreateEventReadyToBootEx (TPL_CALLBACK, Rk806ReadyToBoot, NULL, &ReadyToBootEvent);

  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  Rk806ExitBootServices,
                  NULL,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

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
