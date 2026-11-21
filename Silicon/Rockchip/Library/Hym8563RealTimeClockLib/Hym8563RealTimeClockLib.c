/** @file
 *
 *  RealTimeClockLib for an HYM8563 / PCF8563-compatible RTC behind
 *  Rockchip's I2cDxe, with the virtual RTC as a fallback.
 *
 *  All three RK3576 boards carry an 8563-family chip at 0x51 on I2C2:
 *  HYM8563S on the TaishanPi 3M, LK8563S on the ArmSoM CM5 module and
 *  TCS8563 on the ROCK 4D.  The register layout used here is the subset the
 *  three share with the NXP PCF8563 and that Linux's rtc-hym8563 uses:
 *
 *    0x00 control 1  (bit 7 TEST, bit 5 STOP)
 *    0x01 control 2  (alarm and timer interrupt enables and flags)
 *    0x02 seconds    (bit 7 VL: the oscillator stopped, time not reliable)
 *    0x03 minutes, 0x04 hours, 0x05 day, 0x06 weekday,
 *    0x07 month      (bit 7 century, ignored as rtc-hym8563 does)
 *    0x08 year       (00..99, 2000..2099)
 *    0x09..0x0F      alarm, CLKOUT and timer: never written here
 *
 *  What this library writes, and when:
 *
 *    - Only in SetTime(), only registers 0x02..0x08, and only after a read
 *      of 0x00..0x08 has just succeeded and control 1 shows no test mode.
 *    - If that read found STOP set, control 1 is written back with STOP
 *      cleared and every other bit as read; otherwise control 1 is not
 *      written.
 *    - Control 2, the alarm registers, CLKOUT (0x0D: the 32 kHz clock that
 *      feeds the SoC and the WiFi module on all three boards) and the timer
 *      are never written.  The TaishanPi 3M wires INT to the PMIC's PWRON,
 *      which is one more reason not to touch the alarm.
 *    - GetTime() never writes.  A time with VL set, STOP set or a register
 *      that is not valid BCD is reported from the virtual clock instead;
 *      nothing is "repaired".
 *
 *  The chip is checked once, at boot, when I2cDxe brings up its bus: a read
 *  of registers 0x00..0x08 must be ACKed, control 1 must not be in test
 *  mode, and, unless VL is set, the time must decode.  Anything else and the
 *  chip is never used again this boot, so a board without the chip, without
 *  its bus or with something else at that address still boots with the
 *  virtual clock it had before.
 *
 *  Time zone and daylight are handled as in EmbeddedPkg's
 *  VirtualRealTimeClockLib: the chip holds UTC, and a time passed with a
 *  time zone is converted to UTC before it is written.  A caller that passes
 *  EFI_UNSPECIFIED_TIMEZONE (the UEFI Shell, the network time sync driver,
 *  Windows) has its time stored as given.
 *
 *  Runtime (after ExitBootServices):
 *
 *    The OS may drive the same I2C controller itself, and two masters on one
 *    controller with no lock between them corrupt each other's transfers --
 *    on the TaishanPi 3M that bus also carries the FUSB302 USB-C controller.
 *    So the chip is used at runtime only when ExitBootServicesHookDxe
 *    identified the OS loader as Windows: under ACPI Windows has no driver
 *    for the RTC (HYMB0001) or for anything else on I2C2.  For Linux the
 *    services return EFI_UNSUPPORTED while the chip is present, so that
 *    rtc-efi stays out of the way of rtc-hym8563, which binds from the
 *    device tree or from HYMB0001.  For any other OS, and whenever the chip
 *    is absent, the virtual clock is used at runtime, as before.
 *
 *  Copyright (c) 2017, Linaro, Ltd. All rights reserved.<BR>
 *  Copyright (c) 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2019, Pete Batard <pete@akeo.ie>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/TimeBaseLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Protocol/ExitBootServicesOsNotify.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipI2cMasterProtocol.h>

#define RTC_BUS      (FixedPcdGet8 (PcdRtc8563Bus))
#define RTC_ADDRESS  (FixedPcdGet8 (PcdRtc8563Address))

#define RTC_REG_CTL1  0x00
#define RTC_REG_SEC   0x02

#define RTC_CTL1_TEST  BIT7
#define RTC_CTL1_STOP  BIT5
#define RTC_SEC_VL     BIT7

#define RTC_SEC_MASK    0x7F
#define RTC_MIN_MASK    0x7F
#define RTC_HOUR_MASK   0x3F
#define RTC_DAY_MASK    0x3F
#define RTC_MONTH_MASK  0x1F

#define RTC_BASE_YEAR  2000
#define RTC_LAST_YEAR  2099

//
// Registers 0x00..0x08 as read in one transfer.
//
typedef struct {
  UINT8    Ctl1;
  UINT8    Ctl2;
  UINT8    Seconds;
  UINT8    Minutes;
  UINT8    Hours;
  UINT8    Days;
  UINT8    Weekdays;
  UINT8    Months;
  UINT8    Years;
} RTC_REGS;

typedef struct {
  UINTN                OperationCount;
  EFI_I2C_OPERATION    Operation[2];
} RTC_I2C_PACKET;

STATIC CONST CHAR16  mEpochVariableName[] = L"RtcEpochSeconds";

//
// Set only after the chip passed its checks.  Converted for runtime.
//
STATIC EFI_I2C_MASTER_PROTOCOL  *mI2cMaster;

//
// Decided at ExitBootServices from the OS loader that called it.
//
STATIC EXIT_BOOT_SERVICES_OS_TYPE  mOsType = ExitBootServicesOsUnknown;

STATIC VOID       *mI2cMasterRegistration;
STATIC VOID       *mOsNotifyRegistration;
STATIC EFI_EVENT  mVirtualAddrChangeEvent;
STATIC BOOLEAN    mReportedFallback;

//
// ---------------------------------------------------------------------------
// Virtual clock: EmbeddedPkg's VirtualRealTimeClockLib, kept here as the
// fallback.  Same variable, same GUID (the driver's), so a board that was
// running the virtual clock keeps its time when the chip is not usable.
// ---------------------------------------------------------------------------
//

STATIC
UINTN
VirtualGetUtcEpoch (
  OUT UINT32  *Nanosecond
  )
{
  EFI_STATUS  Status;
  UINT64      Freq;
  UINT64      Counter;
  UINT64      Remainder;
  UINTN       EpochSeconds;
  UINTN       Size;

  *Nanosecond = 0;

  Size         = sizeof (EpochSeconds);
  EpochSeconds = 0;
  Status       = EfiGetVariable (
                   (CHAR16 *)mEpochVariableName,
                   &gEfiCallerIdGuid,
                   NULL,
                   &Size,
                   &EpochSeconds
                   );
  if (EFI_ERROR (Status)) {
    EpochSeconds = BUILD_EPOCH;
    EfiSetVariable (
      (CHAR16 *)mEpochVariableName,
      &gEfiCallerIdGuid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
      sizeof (EpochSeconds),
      &EpochSeconds
      );
  }

  Freq = GetPerformanceCounterProperties (NULL, NULL);
  if (Freq != 0) {
    Counter       = GetPerformanceCounter ();
    EpochSeconds += (UINTN)DivU64x64Remainder (Counter, Freq, &Remainder);
    if (Remainder <= 18446744073U) {
      *Nanosecond = (UINT32)(MultU64x64 (Remainder, 1000000000U) / Freq);
    }
  }

  return EpochSeconds;
}

STATIC
EFI_STATUS
VirtualSetUtcEpoch (
  IN UINTN  EpochSeconds
  )
{
  UINT64  Freq;
  UINT64  Uptime;
  UINT64  Remainder;

  //
  // Store the time at platform reset, so that a read right after this
  // returns what was just set rather than that plus the uptime.
  //
  Freq = GetPerformanceCounterProperties (NULL, NULL);
  if (Freq != 0) {
    Uptime = DivU64x64Remainder (GetPerformanceCounter (), Freq, &Remainder);
    if (EpochSeconds > Uptime) {
      EpochSeconds -= (UINTN)Uptime;
    }
  }

  return EfiSetVariable (
           (CHAR16 *)mEpochVariableName,
           &gEfiCallerIdGuid,
           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
           sizeof (EpochSeconds),
           &EpochSeconds
           );
}

//
// ---------------------------------------------------------------------------
// Time zone handling, as VirtualRealTimeClockLib does it.
// ---------------------------------------------------------------------------
//

STATIC
UINTN
UtcToLocalEpoch (
  IN UINTN     UtcEpoch,
  IN EFI_TIME  *Time
  )
{
  INT64  Epoch;

  Epoch = (INT64)UtcEpoch;
  if (Time->TimeZone != EFI_UNSPECIFIED_TIMEZONE) {
    Epoch += (INT64)Time->TimeZone * SEC_PER_MIN;
  }

  if ((Time->Daylight & EFI_TIME_IN_DAYLIGHT) == EFI_TIME_IN_DAYLIGHT) {
    Epoch += SEC_PER_HOUR;
  }

  return (Epoch < 0) ? 0 : (UINTN)Epoch;
}

STATIC
UINTN
LocalToUtcEpoch (
  IN EFI_TIME  *Time
  )
{
  INT64  Epoch;

  Epoch = (INT64)EfiTimeToEpoch (Time);
  if (Time->TimeZone != EFI_UNSPECIFIED_TIMEZONE) {
    Epoch -= (INT64)Time->TimeZone * SEC_PER_MIN;
  }

  if ((Time->Daylight & EFI_TIME_IN_DAYLIGHT) == EFI_TIME_IN_DAYLIGHT) {
    Epoch -= SEC_PER_HOUR;
  }

  return (Epoch < 0) ? 0 : (UINTN)Epoch;
}

//
// ---------------------------------------------------------------------------
// The chip.
// ---------------------------------------------------------------------------
//

STATIC
BOOLEAN
BcdToBin (
  IN  UINT8  Bcd,
  IN  UINT8  Min,
  IN  UINT8  Max,
  OUT UINT8  *Bin
  )
{
  if (((Bcd & 0x0F) > 9) || ((Bcd >> 4) > 9)) {
    return FALSE;
  }

  *Bin = (UINT8)((Bcd >> 4) * 10 + (Bcd & 0x0F));
  return (*Bin >= Min) && (*Bin <= Max);
}

STATIC
UINT8
BinToBcd (
  IN UINT8  Bin
  )
{
  return (UINT8)(((Bin / 10) << 4) | (Bin % 10));
}

STATIC
EFI_STATUS
RtcReadRegs (
  IN  EFI_I2C_MASTER_PROTOCOL  *Master,
  OUT RTC_REGS                 *Regs
  )
{
  EFI_STATUS      Status;
  RTC_I2C_PACKET  Packet;
  UINT8           Reg;

  Reg = RTC_REG_CTL1;

  Packet.OperationCount             = 2;
  Packet.Operation[0].Flags         = 0;
  Packet.Operation[0].LengthInBytes = sizeof (Reg);
  Packet.Operation[0].Buffer        = &Reg;
  Packet.Operation[1].Flags         = I2C_FLAG_READ;
  Packet.Operation[1].LengthInBytes = sizeof (*Regs);
  Packet.Operation[1].Buffer        = (UINT8 *)Regs;

  Status = Master->StartRequest (
                     Master,
                     RTC_ADDRESS,
                     (EFI_I2C_REQUEST_PACKET *)&Packet,
                     NULL,
                     NULL
                     );
  if (!EFI_ERROR (Status) && (Packet.Operation[1].LengthInBytes != sizeof (*Regs))) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

STATIC
EFI_STATUS
RtcWriteRegs (
  IN EFI_I2C_MASTER_PROTOCOL  *Master,
  IN UINT8                    Reg,
  IN CONST UINT8              *Data,
  IN UINTN                    Length
  )
{
  EFI_STATUS      Status;
  RTC_I2C_PACKET  Packet;
  UINT8           Buffer[8];
  UINTN           Index;

  if (Length > sizeof (Buffer) - 1) {
    return EFI_INVALID_PARAMETER;
  }

  Buffer[0] = Reg;
  for (Index = 0; Index < Length; Index++) {
    Buffer[Index + 1] = Data[Index];
  }

  Packet.OperationCount             = 1;
  Packet.Operation[0].Flags         = 0;
  Packet.Operation[0].LengthInBytes = (UINT32)(Length + 1);
  Packet.Operation[0].Buffer        = Buffer;

  Status = Master->StartRequest (
                     Master,
                     RTC_ADDRESS,
                     (EFI_I2C_REQUEST_PACKET *)&Packet,
                     NULL,
                     NULL
                     );
  if (!EFI_ERROR (Status) && (Packet.Operation[0].LengthInBytes != Length + 1)) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

/**
  Decode the time registers.  The weekday is not checked: nothing here
  reads it, and a chip whose only odd field is the weekday still keeps time.

  @retval TRUE   Every field is valid BCD and in range; Utc holds the time.
  @retval FALSE  At least one field is not.
**/
STATIC
BOOLEAN
RtcDecode (
  IN  CONST RTC_REGS  *Regs,
  OUT EFI_TIME        *Utc
  )
{
  UINT8  Second;
  UINT8  Minute;
  UINT8  Hour;
  UINT8  Day;
  UINT8  Month;
  UINT8  Year;

  if (!BcdToBin (Regs->Seconds & RTC_SEC_MASK, 0, 59, &Second) ||
      !BcdToBin (Regs->Minutes & RTC_MIN_MASK, 0, 59, &Minute) ||
      !BcdToBin (Regs->Hours & RTC_HOUR_MASK, 0, 23, &Hour) ||
      !BcdToBin (Regs->Days & RTC_DAY_MASK, 1, 31, &Day) ||
      !BcdToBin (Regs->Months & RTC_MONTH_MASK, 1, 12, &Month) ||
      !BcdToBin (Regs->Years, 0, 99, &Year))
  {
    return FALSE;
  }

  ZeroMem (Utc, sizeof (*Utc));
  Utc->Year     = (UINT16)(RTC_BASE_YEAR + Year);
  Utc->Month    = Month;
  Utc->Day      = Day;
  Utc->Hour     = Hour;
  Utc->Minute   = Minute;
  Utc->Second   = Second;
  Utc->TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  Utc->Daylight = 0;

  //
  // Catches 31 February and the like.
  //
  return IsTimeValid (Utc);
}

/**
  Whether the chip may be used for this call.
**/
STATIC
BOOLEAN
RtcUsable (
  VOID
  )
{
  if (mI2cMaster == NULL) {
    return FALSE;
  }

  if (!EfiAtRuntime ()) {
    return TRUE;
  }

  return mOsType == ExitBootServicesOsWindows;
}

/**
  Whether the services should refuse at runtime because the OS has a driver
  of its own for the chip.
**/
STATIC
BOOLEAN
RtcOwnedByOs (
  VOID
  )
{
  return (mI2cMaster != NULL) && EfiAtRuntime () &&
         (mOsType == ExitBootServicesOsLinux);
}

STATIC
VOID
ReportFallback (
  IN CONST CHAR8  *Why,
  IN UINTN        Value
  )
{
  //
  // Once per boot, and never at runtime: the serial port is not mapped.
  //
  if (mReportedFallback || EfiAtRuntime ()) {
    return;
  }

  mReportedFallback = TRUE;
  DEBUG ((
    DEBUG_WARN,
    "Hym8563Rtc: %a (0x%lx); reporting the virtual clock instead, nothing written\n",
    Why,
    (UINT64)Value
    ));
}

/**
  Returns the current time and date information, and the time-keeping
  capabilities of the hardware platform.
**/
EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities
  )
{
  EFI_STATUS  Status;
  RTC_REGS    Regs;
  EFI_TIME    Utc;
  UINTN       UtcEpoch;
  UINT32      Nanosecond;
  INT16       TimeZone;
  UINT8       Daylight;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (RtcOwnedByOs ()) {
    return EFI_UNSUPPORTED;
  }

  //
  // RealTimeClockRuntimeDxe fills these in from its settings before calling.
  //
  TimeZone = Time->TimeZone;
  Daylight = Time->Daylight;
  if (!IsValidTimeZone (TimeZone)) {
    TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  }

  UtcEpoch   = 0;
  Nanosecond = 0;

  if (RtcUsable ()) {
    Status = RtcReadRegs (mI2cMaster, &Regs);
    if (EFI_ERROR (Status)) {
      ReportFallback ("read failed", (UINTN)Status);
    } else if ((Regs.Ctl1 & (RTC_CTL1_TEST | RTC_CTL1_STOP)) != 0) {
      ReportFallback ("control 1 has TEST or STOP set", Regs.Ctl1);
    } else if ((Regs.Seconds & RTC_SEC_VL) != 0) {
      ReportFallback ("VL set: the clock lost power, its time is not valid", Regs.Seconds);
    } else if (!RtcDecode (&Regs, &Utc)) {
      ReportFallback ("time registers are not a valid time, seconds", Regs.Seconds);
    } else {
      UtcEpoch = EfiTimeToEpoch (&Utc);
    }
  }

  if (UtcEpoch == 0) {
    UtcEpoch = VirtualGetUtcEpoch (&Nanosecond);
  }

  Time->TimeZone = TimeZone;
  Time->Daylight = Daylight;
  EpochToEfiTime (UtcToLocalEpoch (UtcEpoch, Time), Time);
  Time->Nanosecond = Nanosecond;
  Time->TimeZone   = TimeZone;
  Time->Daylight   = Daylight;

  if (Capabilities != NULL) {
    Capabilities->Resolution = 1;
    Capabilities->Accuracy   = 0;
    Capabilities->SetsToZero = FALSE;
  }

  return EFI_SUCCESS;
}

/**
  Sets the current local time and date information.
**/
EFI_STATUS
EFIAPI
LibSetTime (
  IN EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  RTC_REGS    Regs;
  EFI_TIME    Utc;
  UINTN       UtcEpoch;
  UINT8       Data[7];
  UINT8       Ctl1;

  if ((Time == NULL) || !IsTimeValid (Time)) {
    return EFI_INVALID_PARAMETER;
  }

  if (RtcOwnedByOs ()) {
    return EFI_UNSUPPORTED;
  }

  //
  // EfiTimeToEpoch() counts from 1970 and has no sign.
  //
  if (Time->Year < 1971) {
    return EFI_INVALID_PARAMETER;
  }

  UtcEpoch = LocalToUtcEpoch (Time);

  if (!RtcUsable ()) {
    return VirtualSetUtcEpoch (UtcEpoch);
  }

  EpochToEfiTime (UtcEpoch, &Utc);
  if ((Utc.Year < RTC_BASE_YEAR) || (Utc.Year > RTC_LAST_YEAR)) {
    //
    // The chip holds 2000..2099 only.
    //
    return EFI_INVALID_PARAMETER;
  }

  //
  // Read before write: the chip must still answer, and must not be in test
  // mode, or nothing is written.
  //
  Status = RtcReadRegs (mI2cMaster, &Regs);
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  if ((Regs.Ctl1 & RTC_CTL1_TEST) != 0) {
    return EFI_DEVICE_ERROR;
  }

  Data[0] = BinToBcd (Utc.Second);          // VL cleared
  Data[1] = BinToBcd (Utc.Minute);
  Data[2] = BinToBcd (Utc.Hour);
  Data[3] = BinToBcd (Utc.Day);
  Data[4] = (UINT8)EfiTimeToWday (&Utc);    // 0 = Sunday, as rtc-hym8563
  Data[5] = BinToBcd (Utc.Month);           // century bit 0, as rtc-hym8563
  Data[6] = BinToBcd ((UINT8)(Utc.Year - RTC_BASE_YEAR));

  Status = RtcWriteRegs (mI2cMaster, RTC_REG_SEC, Data, sizeof (Data));
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  //
  // A stopped oscillator would keep the time just written frozen.  Start it,
  // leaving the other bits of control 1 as they were read.
  //
  if ((Regs.Ctl1 & RTC_CTL1_STOP) != 0) {
    Ctl1   = (UINT8)(Regs.Ctl1 & ~RTC_CTL1_STOP);
    Status = RtcWriteRegs (mI2cMaster, RTC_REG_CTL1, &Ctl1, sizeof (Ctl1));
    if (EFI_ERROR (Status)) {
      return EFI_DEVICE_ERROR;
    }
  }

  if (!EfiAtRuntime ()) {
    DEBUG ((
      DEBUG_INFO,
      "Hym8563Rtc: set to %04u-%02u-%02u %02u:%02u:%02u UTC%a\n",
      Utc.Year,
      Utc.Month,
      Utc.Day,
      Utc.Hour,
      Utc.Minute,
      Utc.Second,
      ((Regs.Ctl1 & RTC_CTL1_STOP) != 0) ? ", oscillator started" : ""
      ));
  }

  //
  // The chip is right now; a later fallback starts from here too.
  //
  mReportedFallback = FALSE;
  return EFI_SUCCESS;
}

/**
  Returns the current wakeup alarm clock setting.  The alarm is not
  supported: on the TaishanPi 3M its output is wired to the PMIC's power-on
  input, and this library does not touch it.
**/
EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Sets the system wakeup alarm clock time.  Not supported; see above.
**/
EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN BOOLEAN    Enabled,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Check the chip on its bus, once, when I2cDxe has brought the bus up.
  Reads only.
**/
STATIC
VOID
RtcProbe (
  IN EFI_I2C_MASTER_PROTOCOL  *Master
  )
{
  EFI_STATUS  Status;
  RTC_REGS    Regs;
  EFI_TIME    Utc;
  BOOLEAN     TimeValid;

  Status = RtcReadRegs (Master, &Regs);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "Hym8563Rtc: no answer from I2C%u 0x%02x (%r); using the virtual clock\n",
      RTC_BUS,
      RTC_ADDRESS,
      Status
      ));
    return;
  }

  if ((Regs.Ctl1 & RTC_CTL1_TEST) != 0) {
    DEBUG ((
      DEBUG_WARN,
      "Hym8563Rtc: I2C%u 0x%02x control 1 = 0x%02x (TEST set): not an RTC in normal mode, not used\n",
      RTC_BUS,
      RTC_ADDRESS,
      Regs.Ctl1
      ));
    return;
  }

  TimeValid = RtcDecode (&Regs, &Utc);

  if (((Regs.Seconds & RTC_SEC_VL) == 0) && !TimeValid) {
    DEBUG ((
      DEBUG_WARN,
      "Hym8563Rtc: I2C%u 0x%02x registers %02x %02x %02x %02x %02x %02x %02x %02x %02x"
      " are not a time and VL is clear: not used\n",
      RTC_BUS,
      RTC_ADDRESS,
      Regs.Ctl1,
      Regs.Ctl2,
      Regs.Seconds,
      Regs.Minutes,
      Regs.Hours,
      Regs.Days,
      Regs.Weekdays,
      Regs.Months,
      Regs.Years
      ));
    return;
  }

  if (TimeValid) {
    DEBUG ((
      DEBUG_INFO,
      "Hym8563Rtc: I2C%u 0x%02x ctl1 0x%02x ctl2 0x%02x, %04u-%02u-%02u %02u:%02u:%02u UTC%a%a\n",
      RTC_BUS,
      RTC_ADDRESS,
      Regs.Ctl1,
      Regs.Ctl2,
      Utc.Year,
      Utc.Month,
      Utc.Day,
      Utc.Hour,
      Utc.Minute,
      Utc.Second,
      ((Regs.Seconds & RTC_SEC_VL) != 0) ? ", VL set (time not valid)" : "",
      ((Regs.Ctl1 & RTC_CTL1_STOP) != 0) ? ", STOP set" : ""
      ));
  } else {
    DEBUG ((
      DEBUG_INFO,
      "Hym8563Rtc: I2C%u 0x%02x ctl1 0x%02x ctl2 0x%02x, VL set and no valid time:"
      " the clock lost power; virtual clock until SetTime\n",
      RTC_BUS,
      RTC_ADDRESS,
      Regs.Ctl1,
      Regs.Ctl2
      ));
  }

  mI2cMaster = Master;
}

STATIC
VOID
EFIAPI
RockchipI2cMasterRegistrationEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    Handle;
  UINTN                         BufferSize;
  ROCKCHIP_I2C_MASTER_PROTOCOL  *RockchipI2cMaster;
  EFI_I2C_MASTER_PROTOCOL       *Master;

  while (mI2cMaster == NULL) {
    BufferSize = sizeof (Handle);
    Status     = gBS->LocateHandle (
                        ByRegisterNotify,
                        &gRockchipI2cMasterProtocolGuid,
                        mI2cMasterRegistration,
                        &BufferSize,
                        &Handle
                        );
    if (EFI_ERROR (Status)) {
      return;
    }

    Status = gBS->HandleProtocol (Handle, &gRockchipI2cMasterProtocolGuid, (VOID **)&RockchipI2cMaster);
    if (EFI_ERROR (Status) || (RockchipI2cMaster->Bus != RTC_BUS)) {
      continue;
    }

    Status = gBS->HandleProtocol (Handle, &gEfiI2cMasterProtocolGuid, (VOID **)&Master);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "Hym8563Rtc: I2C%u has no I2C master protocol (%r)\n", RTC_BUS, Status));
      continue;
    }

    gBS->CloseEvent (Event);
    RtcProbe (Master);
    return;
  }
}

STATIC
VOID
EFIAPI
ExitBootServicesOsHandler (
  IN EXIT_BOOT_SERVICES_OS_CONTEXT  *Context
  )
{
  mOsType = Context->OsType;

  if (mI2cMaster == NULL) {
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "Hym8563Rtc: runtime time services %a\n",
    (mOsType == ExitBootServicesOsWindows) ? "use the chip (Windows: no OS driver for it)" :
    (mOsType == ExitBootServicesOsLinux) ? "return EFI_UNSUPPORTED (Linux drives the chip itself)" :
    "use the virtual clock (OS not identified)"
    ));
}

STATIC
VOID
EFIAPI
ExitBootServicesOsNotifyRegistrationEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                             Status;
  EXIT_BOOT_SERVICES_OS_NOTIFY_PROTOCOL  *Notify;

  Status = gBS->LocateProtocol (&gExitBootServicesOsNotifyProtocolGuid, NULL, (VOID **)&Notify);
  if (EFI_ERROR (Status)) {
    return;
  }

  gBS->CloseEvent (Event);

  Status = Notify->RegisterHandler (Notify, ExitBootServicesOsHandler);
  if (EFI_ERROR (Status)) {
    //
    // mOsType stays Unknown: the chip is not used at runtime.
    //
    DEBUG ((DEBUG_WARN, "Hym8563Rtc: no OS identification at ExitBootServices (%r)\n", Status));
  }
}

STATIC
VOID
EFIAPI
VirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EfiConvertPointer (0x0, (VOID **)&mI2cMaster);
}

/**
  Library entry point.  Installs nothing that waits on the bus: the time
  services work from the start, on the virtual clock until the chip has
  been found.
**/
EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  EfiCreateProtocolNotifyEvent (
    &gRockchipI2cMasterProtocolGuid,
    TPL_CALLBACK,
    RockchipI2cMasterRegistrationEvent,
    NULL,
    &mI2cMasterRegistration
    );

  EfiCreateProtocolNotifyEvent (
    &gExitBootServicesOsNotifyProtocolGuid,
    TPL_CALLBACK,
    ExitBootServicesOsNotifyRegistrationEvent,
    NULL,
    &mOsNotifyRegistration
    );

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  VirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
