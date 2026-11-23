/** @file
 *
 *  Report the Mali-G52's supply, vdd_gpu_s0, and the per-chip OTP data that
 *  decide which GPU clock that supply is good for.  Read only: nothing here
 *  writes a PMIC register, a clock or anything else on the chip.
 *
 *  Why
 *  ---
 *  Nothing in this firmware or in Windows sets vdd_gpu_s0, so it sits at
 *  whatever the RK806 powers up with (its OTP) or an earlier stage left.
 *  The Windows GPU driver (wddm-mali-g52) caps the clock at 600 MHz, the
 *  highest operating point at the lowest voltage, because nobody had read
 *  the rail.  These log lines, and the GpuRail variable below, are that
 *  reading.
 *
 *  Where every number comes from
 *  -----------------------------
 *  - The rail: vdd_gpu_s0 is dcdc-reg5 (BUCK5) under pmic@23 on &i2c1,
 *    regulator-boot-on, regulator-min/max-microvolt 550000/900000, in all
 *    three boards' mainline device trees (rk3576-lckfb-tspi-3m.dts:429,
 *    rk3576-armsom-cm5.dtsi:269, rk3576-rock-4d.dts:478).  The vendor
 *    rk3576-rk806.dtsi asks U-Boot for 750 mV (regulator-init-microvolt);
 *    this firmware's boot chain has no such step.
 *  - Registers (mainline include/linux/mfd/rk808.h, rk808-regulator.c):
 *      RK806_POWER_EN1     0x01  BUCK5 enable = bit 0 (ENABLE_MASK(0))
 *      RK806_BUCK5_CONFIG  0x14  logged raw
 *      RK806_BUCK5_ON_VSEL 0x1E  500 mV + 6.25 mV * sel for sel 0..159
 *      RK806_BUCK5_SLP_VSEL 0x28 logged raw
 *      RK806_CHIP_NAME/VER 0x5A/0x5B, RK806_ID 0x8060 under mask 0xfff0
 *  - OTP bytes (vendor rk3576.dtsi otp node; the same OtpLib the TSADC trim
 *    and the SMBIOS serial use): 0x02 cpu-code (35 76), 0x08
 *    specification-serial-number bits 4:0 (0 or 1 plain, 0x0A J, 0x0D M,
 *    0x13 S), 0x21 gpu-leakage, 0x48..0x4D gpu-opp-info
 *    { u16 min_freq MHz, u16 max_freq MHz, u8 volt mV, u8 length }, which
 *    the vendor kernel adds (volt) to every GPU OPP between min and max
 *    (rockchip_adjust_opp_by_otp, rockchip_opp_select.c).
 *  - Operating points: mainline rk3576.dtsi gpu_opp_table (300 to 600 MHz
 *    700 mV, 700 MHz 725 mV, 800 MHz 775 mV, 900 MHz 825 mV, 950 MHz
 *    850 mV).  The vendor kernel's table for a plain RK3576 picks a level
 *    L0..L10 per chip from a PVTM ring-oscillator count taken at 800 MHz and
 *    750 mV; this firmware cannot take that count without changing the
 *    clock and the rail, so the vendor column below is its slowest level
 *    (L0, or opp-microvolt where L0 is not given): 700 MHz 750 mV, 800 MHz
 *    812.5 mV, 900 MHz 875 mV, no 950 MHz.
 *  - The clock: CRU_CLKSEL_CON165 (TRM: mux [7:5] 0 GPLL, 1 CPLL, 2 AUPLL,
 *    3 SPLL, 4 LPLL; div [4:0]; [8] selects the PVTPLL path).  Without the
 *    PVTPLL the only rates between 600 and 950 MHz a fixed PLL gives are
 *    SPLL / 1 = 702 MHz and AUPLL / 1 = 786 MHz, and AUPLL belongs to audio.
 *    702 MHz is above the 700 MHz OPP, so the 800 MHz OPP governs it.
 *
 *  What Windows gets
 *  -----------------
 *  A volatile variable GpuRail (GPU_RAIL_REPORT below) under
 *  gRK3576GpuRailVariableGuid, readable at runtime, so the GPU driver can
 *  check the rail before it allows any clock above 600 MHz.  The layout is
 *  versioned; the driver ignores a size or version it does not know.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OtpLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipI2cMasterProtocol.h>

#include "GpuRail.h"

STATIC_ASSERT (sizeof (GPU_RAIL_REPORT) == 32, "GpuRail layout is shared with the Windows driver");

#define RK806_POWER_EN1             0x01
#define RK806_BUCK5_CONFIG          0x14
#define RK806_BUCK5_ON_VSEL         0x1E
#define RK806_BUCK5_SLP_VSEL        0x28
#define RK806_CHIP_NAME             0x5A
#define RK806_CHIP_VER              0x5B
#define RK806_ID                    0x8060
#define RK8XX_ID_MSK                0xFFF0
#define RK806_BUCK_UV_BASE          500000
#define RK806_BUCK_UV_STEP          6250
#define RK806_BUCK_SEL_LINEAR_MAX   159

#define GPU_RAIL_UV_MIN             550000   // regulator-min-microvolt
#define GPU_RAIL_UV_MAX             900000   // regulator-max-microvolt

#define RK3576_OTP_CPU_CODE         0x02
#define RK3576_OTP_SPECIFICATION    0x08
#define RK3576_OTP_GPU_LEAKAGE      0x21
#define RK3576_OTP_GPU_OPP_INFO     0x48

#define CRU_CLKSEL165               0x27200594UL

//
// Shared with the Windows driver (wddm-mali-g52, adapter.c).
//
STATIC EFI_GUID  gRK3576GpuRailVariableGuid = {
  0x7701c75f, 0xf454, 0x4aab, { 0x94, 0x45, 0x11, 0x7f, 0x09, 0xfb, 0x87, 0xcf }
};

typedef struct {
  UINT16    Mhz;
  UINT32    MainlineUv;   // mainline rk3576.dtsi gpu_opp_table
  UINT32    VendorL0Uv;   // vendor gpu_opp_table, plain RK3576, slowest level; 0 = no such OPP
} GPU_OPP;

STATIC CONST GPU_OPP  mGpuOpps[] = {
  { 300, 700000, 712500 },
  { 400, 700000, 712500 },
  { 500, 700000, 712500 },
  { 600, 700000, 712500 },
  { 700, 725000, 750000 },
  { 800, 775000, 812500 },
  { 900, 825000, 875000 },
  { 950, 850000, 0      },
};

//
// The vendor kernel's own cap on an OTP-raised voltage (opp-microvolt max).
//
#define GPU_VENDOR_UV_MAX  875000

typedef struct {
  UINTN                OperationCount;
  EFI_I2C_OPERATION    Operation[2];
} RK806_I2C_REQUEST;

STATIC EFI_I2C_MASTER_PROTOCOL  *mI2c;
STATIC VOID                     *mI2cRegistration;

STATIC
EFI_STATUS
Rk806Read (
  IN  UINT8  Reg,
  OUT UINT8  *Value
  )
{
  RK806_I2C_REQUEST  Request;

  //
  // The first operation sends the register number, the second reads it:
  // the register itself is not written.
  //
  Request.OperationCount             = 2;
  Request.Operation[0].Flags         = 0;
  Request.Operation[0].LengthInBytes = 1;
  Request.Operation[0].Buffer        = &Reg;
  Request.Operation[1].Flags         = I2C_FLAG_READ;
  Request.Operation[1].LengthInBytes = 1;
  Request.Operation[1].Buffer        = Value;

  return mI2c->StartRequest (
                 mI2c,
                 FixedPcdGet8 (PcdRk806I2cAddress),
                 (EFI_I2C_REQUEST_PACKET *)&Request,
                 NULL,
                 NULL
                 );
}

/**
  The required voltage of the lowest OPP at or above Mhz, from one column,
  with the OTP's per-chip offset added the way the vendor kernel adds it.
  0 when no OPP covers Mhz.
**/
STATIC
UINT32
RequiredUv (
  IN UINT32                 Mhz,
  IN BOOLEAN                Vendor,
  IN CONST GPU_RAIL_REPORT  *Report
  )
{
  UINTN   Index;
  UINT32  Uv;

  for (Index = 0; Index < ARRAY_SIZE (mGpuOpps); Index++) {
    if (mGpuOpps[Index].Mhz < Mhz) {
      continue;
    }

    Uv = Vendor ? mGpuOpps[Index].VendorL0Uv : mGpuOpps[Index].MainlineUv;
    if (Uv == 0) {
      return 0;
    }

    if (((Report->Flags & GPU_RAIL_FLAG_OTP_VALID) != 0) && (Report->OppInfoVoltMv != 0) &&
        (mGpuOpps[Index].Mhz >= Report->OppInfoMinMhz) && (mGpuOpps[Index].Mhz <= Report->OppInfoMaxMhz))
    {
      Uv += (UINT32)Report->OppInfoVoltMv * 1000;
      if (Vendor && (Uv > GPU_VENDOR_UV_MAX)) {
        Uv = GPU_VENDOR_UV_MAX;
      }
    }

    return Uv;
  }

  return 0;
}

STATIC
UINT32
HighestOppMhz (
  IN BOOLEAN                Vendor,
  IN CONST GPU_RAIL_REPORT  *Report
  )
{
  UINTN   Index;
  UINT32  Best;
  UINT32  Need;

  Best = 0;
  for (Index = 0; Index < ARRAY_SIZE (mGpuOpps); Index++) {
    Need = RequiredUv (mGpuOpps[Index].Mhz, Vendor, Report);
    if ((Need != 0) && (Need <= Report->RailUv)) {
      Best = mGpuOpps[Index].Mhz;
    }
  }

  return Best;
}

STATIC
VOID
ReadOtp (
  IN OUT GPU_RAIL_REPORT  *Report
  )
{
  UINT8  CpuCode[2];
  UINT8  OppInfo[6];
  UINT8  Byte;

  ZeroMem (CpuCode, sizeof (CpuCode));
  ZeroMem (OppInfo, sizeof (OppInfo));
  OtpRead (RK3576_OTP_CPU_CODE, sizeof (CpuCode), CpuCode);

  //
  // A read that failed comes back zeroed, which is also what a plain chip
  // has in most of these bytes, so the cpu-code has to prove the read.
  //
  if ((CpuCode[0] != 0x35) || (CpuCode[1] != 0x76)) {
    DEBUG ((DEBUG_ERROR, "GpuRail: OTP cpu-code reads %02x %02x, not an RK3576; OTP not used\n", CpuCode[0], CpuCode[1]));
    return;
  }

  Report->Flags |= GPU_RAIL_FLAG_OTP_VALID;

  Byte = 0;
  OtpRead (RK3576_OTP_SPECIFICATION, 1, &Byte);
  Report->Specification = Byte & 0x1F;
  if (Report->Specification <= 1) {
    Report->Flags |= GPU_RAIL_FLAG_PLAIN_RK3576;
  }

  Byte = 0;
  OtpRead (RK3576_OTP_GPU_LEAKAGE, 1, &Byte);
  Report->GpuLeakage = Byte;

  OtpRead (RK3576_OTP_GPU_OPP_INFO, sizeof (OppInfo), OppInfo);
  Report->OppInfoMinMhz = (UINT16)(OppInfo[0] | (OppInfo[1] << 8));
  Report->OppInfoMaxMhz = (UINT16)(OppInfo[2] | (OppInfo[3] << 8));
  Report->OppInfoVoltMv = OppInfo[4];
  Report->OppInfoLength = OppInfo[5];

  DEBUG ((
    DEBUG_ERROR,
    "GpuRail: OTP specification 0x%02x (%a), gpu-leakage 0x%02x, gpu-opp-info min %u MHz max %u MHz volt +%u mV length %u\n",
    Report->Specification,
    (Report->Specification <= 1) ? "RK3576" : (Report->Specification == 0x0A) ? "RK3576J" :
    (Report->Specification == 0x0D) ? "RK3576M" : (Report->Specification == 0x13) ? "RK3576S" : "unknown",
    Report->GpuLeakage,
    Report->OppInfoMinMhz,
    Report->OppInfoMaxMhz,
    Report->OppInfoVoltMv,
    Report->OppInfoLength
    ));
}

STATIC
VOID
ReadRail (
  IN OUT GPU_RAIL_REPORT  *Report
  )
{
  UINT8  Name;
  UINT8  Ver;

  if (EFI_ERROR (Rk806Read (RK806_CHIP_NAME, &Name)) ||
      EFI_ERROR (Rk806Read (RK806_CHIP_VER, &Ver)))
  {
    DEBUG ((DEBUG_ERROR, "GpuRail: no answer from the PMIC at 0x%02x\n", FixedPcdGet8 (PcdRk806I2cAddress)));
    return;
  }

  if ((((UINT32)Name << 8 | Ver) & RK8XX_ID_MSK) != RK806_ID) {
    DEBUG ((DEBUG_ERROR, "GpuRail: PMIC ID 0x%02x%02x is not an RK806\n", Name, Ver));
    return;
  }

  if (EFI_ERROR (Rk806Read (RK806_BUCK5_ON_VSEL, &Report->OnVsel)) ||
      EFI_ERROR (Rk806Read (RK806_BUCK5_SLP_VSEL, &Report->SlpVsel)) ||
      EFI_ERROR (Rk806Read (RK806_POWER_EN1, &Report->PowerEn1)) ||
      EFI_ERROR (Rk806Read (RK806_BUCK5_CONFIG, &Report->Buck5Config)))
  {
    DEBUG ((DEBUG_ERROR, "GpuRail: reading BUCK5 failed\n"));
    return;
  }

  if ((Report->PowerEn1 & BIT0) != 0) {
    Report->Flags |= GPU_RAIL_FLAG_ENABLED;
  }

  DEBUG ((
    DEBUG_ERROR,
    "GpuRail: RK806 BUCK5 ON_VSEL 0x%02x SLP_VSEL 0x%02x CONFIG 0x%02x POWER_EN1 0x%02x (%a)\n",
    Report->OnVsel,
    Report->SlpVsel,
    Report->Buck5Config,
    Report->PowerEn1,
    ((Report->PowerEn1 & BIT0) != 0) ? "on" : "off"
    ));

  if (Report->OnVsel > RK806_BUCK_SEL_LINEAR_MAX) {
    DEBUG ((DEBUG_ERROR, "GpuRail: ON_VSEL 0x%02x is above 1.5 V, not a GPU rail\n", Report->OnVsel));
    return;
  }

  Report->RailUv = RK806_BUCK_UV_BASE + (UINT32)Report->OnVsel * RK806_BUCK_UV_STEP;
  if ((Report->RailUv < GPU_RAIL_UV_MIN) || (Report->RailUv > GPU_RAIL_UV_MAX)) {
    DEBUG ((
      DEBUG_ERROR,
      "GpuRail: %u uV is outside the device tree's %u..%u uV\n",
      Report->RailUv,
      GPU_RAIL_UV_MIN,
      GPU_RAIL_UV_MAX
      ));
    return;
  }

  if ((Report->Flags & GPU_RAIL_FLAG_ENABLED) != 0) {
    Report->Flags |= GPU_RAIL_FLAG_RAIL_VALID;
  }

  DEBUG ((DEBUG_ERROR, "GpuRail: vdd_gpu_s0 (RK806 BUCK5) %u uV\n", Report->RailUv));
}

STATIC
VOID
LogVerdict (
  IN CONST GPU_RAIL_REPORT  *Report
  )
{
  STATIC CONST struct {
    UINT32         Mhz;
    CONST CHAR8    *Source;
  } Rates[] = {
    { 594, "GPLL / 2" },
    { 702, "SPLL / 1" },
  };
  UINTN    Index;
  UINT32   Mainline;
  UINT32   Vendor;
  BOOLEAN  Plain;

  if ((Report->Flags & GPU_RAIL_FLAG_RAIL_VALID) == 0) {
    DEBUG ((DEBUG_ERROR, "GpuRail: rail not read; no GPU clock above 600 MHz can be justified\n"));
    return;
  }

  Plain = (Report->Flags & GPU_RAIL_FLAG_PLAIN_RK3576) != 0;
  DEBUG ((
    DEBUG_ERROR,
    "GpuRail: at %u uV mainline's OPP table allows up to %u MHz; the vendor table's slowest level up to %u MHz%a\n",
    Report->RailUv,
    HighestOppMhz (FALSE, Report),
    HighestOppMhz (TRUE, Report),
    Plain ? "" : " (vendor column is for a plain RK3576 only)"
    ));

  for (Index = 0; Index < ARRAY_SIZE (Rates); Index++) {
    Mainline = RequiredUv (Rates[Index].Mhz, FALSE, Report);
    Vendor   = RequiredUv (Rates[Index].Mhz, TRUE, Report);
    DEBUG ((
      DEBUG_ERROR,
      "GpuRail: %u MHz (%a) needs %u uV by mainline (%a), %u uV by the vendor's slowest level (%a)\n",
      Rates[Index].Mhz,
      Rates[Index].Source,
      Mainline,
      (Mainline != 0 && Mainline <= Report->RailUv) ? "met" : "NOT met",
      Vendor,
      (Vendor != 0 && Vendor <= Report->RailUv) ? "met" : "NOT met"
      ));
  }
}

STATIC
VOID
GpuRailRun (
  VOID
  )
{
  GPU_RAIL_REPORT  Report;
  EFI_STATUS       Status;

  ZeroMem (&Report, sizeof (Report));
  Report.Signature = GPU_RAIL_REPORT_SIGNATURE;
  Report.Version   = GPU_RAIL_REPORT_VERSION;
  Report.Size      = sizeof (Report);
  Report.ClkSel165 = MmioRead32 (CRU_CLKSEL165);

  DEBUG ((
    DEBUG_ERROR,
    "GpuRail: CRU_CLKSEL_CON165 0x%08x (mux %u, div %u%a)\n",
    Report.ClkSel165,
    (Report.ClkSel165 >> 5) & 7,
    (Report.ClkSel165 & 0x1F) + 1,
    ((Report.ClkSel165 & BIT8) != 0) ? ", PVTPLL path" : ""
    ));

  ReadOtp (&Report);
  ReadRail (&Report);
  LogVerdict (&Report);

  Status = gRT->SetVariable (
                  GPU_RAIL_VARIABLE_NAME,
                  &gRK3576GpuRailVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (Report),
                  &Report
                  );
  DEBUG ((DEBUG_ERROR, "GpuRail: variable %s flags 0x%02x: %r\n", GPU_RAIL_VARIABLE_NAME, Report.Flags, Status));
}

STATIC
EFI_I2C_MASTER_PROTOCOL *
FindPmicBus (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  ROCKCHIP_I2C_MASTER_PROTOCOL  *Rockchip;
  EFI_I2C_MASTER_PROTOCOL       *Master;

  Master = NULL;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gRockchipI2cMasterProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gRockchipI2cMasterProtocolGuid, (VOID **)&Rockchip);
    if (EFI_ERROR (Status) || (Rockchip->Bus != FixedPcdGet8 (PcdRk806I2cBus))) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiI2cMasterProtocolGuid, (VOID **)&Master);
    if (EFI_ERROR (Status)) {
      Master = NULL;
    }

    break;
  }

  FreePool (Handles);
  return Master;
}

STATIC
VOID
EFIAPI
OnI2cMasterAvailable (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mI2c = FindPmicBus ();
  if (mI2c == NULL) {
    return;   // another bus; wait for the PMIC's
  }

  gBS->CloseEvent (Event);
  GpuRailRun ();
}

VOID
RK3576GpuRailStart (
  VOID
  )
{
  EFI_EVENT  Event;

  if (!FixedPcdGetBool (PcdGpuRailReport)) {
    return;
  }

  //
  // The PMIC's bus is set up by I2cDxe, which may not have run yet.  The
  // notify fires at once for buses already there.
  //
  Event = EfiCreateProtocolNotifyEvent (
            &gRockchipI2cMasterProtocolGuid,
            TPL_CALLBACK,
            OnI2cMasterAvailable,
            NULL,
            &mI2cRegistration
            );
  if (Event == NULL) {
    DEBUG ((DEBUG_ERROR, "GpuRail: could not register for the I2C bus; no report\n"));
  }
}
