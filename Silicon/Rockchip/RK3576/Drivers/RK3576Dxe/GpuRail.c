/** @file
 *
 *  Report the Mali-G52's supply, vdd_gpu_s0, and the per-chip OTP data that
 *  decide which GPU clock that supply is good for.  The report is always
 *  made and writes nothing.  On a board with PcdGpuRail775Supported the
 *  setup option "GPU voltage for 702 MHz (775 mV)" (PcdGpuRail775, its
 *  default the board's PcdGpuRail775Default) raises BUCK5 from 750 to
 *  775 mV and to no more (RaiseRail); with the option off, a rail an earlier boot left
 *  at 775 mV (the RK806 keeps its selectors across a SoC reset) is stepped
 *  back to 750 mV (LowerRail).  Nothing else here writes a PMIC register,
 *  a clock or anything else on the chip.
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
#include <Library/Rk3576TsadcLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipI2cMasterProtocol.h>

#include "RK3576DxeFormSetGuid.h"
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

//
// The raise to 775 mV (setup option GpuRail775, PcdGpuRail775), approved
// first for the TaishanPi 3M after its BUCK5 read 0x28 = 750 mV on
// 2026-11-23, then as an opt-in on boards that set PcdGpuRail775Supported
// (the user's decision, 2026-11-25): 775 mV is mainline's
// voltage for the 800 MHz operating point, which governs SPLL / 1 =
// 702 MHz.  The ceiling is this constant and nothing else; no PCD, setup
// option or variable can move it.
//
#define GPU_RAIL_FROM_VSEL      0x28    // 750.0 mV, the value read on the board
#define GPU_RAIL_CEIL_VSEL      0x2C    // 775.0 mV, never written above
#define GPU_RAIL_STEP_WAIT_US   100     // one 6.25 mV step: 1 us at the slowest ramp (6250 uV/us)
#define GPU_RAIL_SETTLE_US      1000    // after the last step, > 2x regulator-enable-ramp-delay
#define RK806_EXPECT_NAME       0x80    // CHIP_NAME / CHIP_VER the TaishanPi 3M's RK806S-5 reads
#define RK806_EXPECT_VER        0x6A
#define TSADC_CRU_EN_REG        (RK3576_TSADC_BASE + 0x01C)
#define TSADC_GPU_CHANNELS      (BIT3 | BIT5)   // the TRM says 3, the OTP trim and vendor say 5: both

STATIC_ASSERT (GPU_RAIL_CEIL_VSEL == 0x2C, "the approved ceiling is 775 mV");
STATIC_ASSERT (RK806_BUCK_UV_BASE + GPU_RAIL_CEIL_VSEL * RK806_BUCK_UV_STEP == 775000, "0x2C is 775 mV");

STATIC UINT8  mChipName;
STATIC UINT8  mChipVer;

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

  mChipName = Name;
  mChipVer  = Ver;

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
EFI_STATUS
Rk806Write (
  IN UINT8  Reg,
  IN UINT8  Value
  )
{
  RK806_I2C_REQUEST  Request;
  UINT8              Buffer[2];

  Buffer[0]                          = Reg;
  Buffer[1]                          = Value;
  Request.OperationCount             = 1;
  Request.Operation[0].Flags         = 0;
  Request.Operation[0].LengthInBytes = sizeof (Buffer);
  Request.Operation[0].Buffer        = Buffer;

  return mI2c->StartRequest (
                 mI2c,
                 FixedPcdGet8 (PcdRk806I2cAddress),
                 (EFI_I2C_REQUEST_PACKET *)&Request,
                 NULL,
                 NULL
                 );
}

/**
  Write one BUCK5 ON_VSEL selector, never above GPU_RAIL_CEIL_VSEL, and
  read it back.
**/
STATIC
EFI_STATUS
WriteVselChecked (
  IN  UINT8  Vsel,
  OUT UINT8  *ReadBack
  )
{
  EFI_STATUS  Status;

  *ReadBack = 0xFF;
  if (Vsel > GPU_RAIL_CEIL_VSEL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rk806Write (RK806_BUCK5_ON_VSEL, Vsel);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Rk806Read (RK806_BUCK5_ON_VSEL, ReadBack);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (*ReadBack == Vsel) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

/**
  TRUE when the TSADC is sampling and TSHUT (CRU reset) is armed on both
  channels that may be the GPU's.
**/
STATIC
BOOLEAN
TshutArmed (
  VOID
  )
{
  UINT32  CruEn;

  if (!FixedPcdGetBool (PcdTsadcEnable) || !FixedPcdGetBool (PcdTsadcTshutEnable) ||
      !Rk3576TsadcIsRunning ())
  {
    return FALSE;
  }

  CruEn = MmioRead32 (TSADC_CRU_EN_REG);
  DEBUG ((DEBUG_ERROR, "GpuRail: TSADC_CRU_EN 0x%08x, TSHUT %u mC\n", CruEn, FixedPcdGet32 (PcdTsadcTshutTemp)));
  return (CruEn & TSADC_GPU_CHANNELS) == TSADC_GPU_CHANNELS;
}

/**
  Raise BUCK5 from 0x28 to 0x2C (750 to 775 mV) one 6.25 mV step at a
  time, reading each step back.  Any precondition not met: nothing is
  written.  Any step that does not read back: 0x28 is written back.

  SLP_VSEL is left as it is.  It is the voltage the RK806 switches to only
  when its sleep function is entered (a PMIC sleep pin or a suspend);
  nothing in this firmware or in Windows on this board does that, and the
  device tree turns vdd_gpu_s0 off in suspend (regulator-off-in-suspend),
  so the GPU never runs on it.
**/
STATIC
VOID
RaiseRail (
  IN OUT GPU_RAIL_REPORT  *Report
  )
{
  EFI_STATUS  Status;
  UINT8       Vsel;
  UINT8       ReadBack;

  if ((mChipName != RK806_EXPECT_NAME) || (mChipVer != RK806_EXPECT_VER)) {
    DEBUG ((DEBUG_ERROR, "GpuRail: not raising BUCK5: PMIC reads 0x%02x 0x%02x, not 0x80 0x6A\n", mChipName, mChipVer));
    return;
  }

  if ((Report->Flags & GPU_RAIL_FLAG_ENABLED) == 0) {
    DEBUG ((DEBUG_ERROR, "GpuRail: not raising BUCK5: it is off (POWER_EN1 0x%02x)\n", Report->PowerEn1));
    return;
  }

  if (Report->OnVsel == GPU_RAIL_CEIL_VSEL) {
    DEBUG ((DEBUG_ERROR, "GpuRail: BUCK5 already 0x%02x (775000 uV), not written\n", Report->OnVsel));
    return;
  }

  if (Report->OnVsel != GPU_RAIL_FROM_VSEL) {
    DEBUG ((DEBUG_ERROR, "GpuRail: not raising BUCK5: ON_VSEL 0x%02x is not 0x28\n", Report->OnVsel));
    return;
  }

  if (((Report->Flags & GPU_RAIL_FLAG_OTP_VALID) == 0) || (Report->Specification != 0x01)) {
    DEBUG ((DEBUG_ERROR, "GpuRail: not raising BUCK5: OTP specification 0x%02x is not 0x01 (or OTP unread)\n", Report->Specification));
    return;
  }

  if (!TshutArmed ()) {
    DEBUG ((DEBUG_ERROR, "GpuRail: not raising BUCK5: TSHUT is not armed on TSADC channels 3 and 5\n"));
    return;
  }

  for (Vsel = GPU_RAIL_FROM_VSEL + 1; Vsel <= GPU_RAIL_CEIL_VSEL; Vsel++) {
    Status = WriteVselChecked (Vsel, &ReadBack);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "GpuRail: BUCK5 wrote 0x%02x, read back 0x%02x (%r); writing 0x28 back\n",
        Vsel,
        ReadBack,
        Status
        ));
      Rk806Write (RK806_BUCK5_ON_VSEL, GPU_RAIL_FROM_VSEL);
      if (!EFI_ERROR (Rk806Read (RK806_BUCK5_ON_VSEL, &ReadBack))) {
        DEBUG ((DEBUG_ERROR, "GpuRail: BUCK5 now reads 0x%02x\n", ReadBack));
        Report->OnVsel = ReadBack;
      } else {
        Report->OnVsel = 0xFF;
      }

      //
      // Whatever it reads now, it is not a value to build a clock on.
      //
      Report->RailUv = 0;
      Report->Flags &= (UINT8)~GPU_RAIL_FLAG_RAIL_VALID;
      return;
    }

    MicroSecondDelay (GPU_RAIL_STEP_WAIT_US);
  }

  MicroSecondDelay (GPU_RAIL_SETTLE_US);

  Status = Rk806Read (RK806_BUCK5_ON_VSEL, &ReadBack);
  if (EFI_ERROR (Status) || (ReadBack != GPU_RAIL_CEIL_VSEL)) {
    DEBUG ((DEBUG_ERROR, "GpuRail: BUCK5 final read 0x%02x (%r); writing 0x28 back\n", ReadBack, Status));
    Rk806Write (RK806_BUCK5_ON_VSEL, GPU_RAIL_FROM_VSEL);
    Report->RailUv = 0;
    Report->Flags &= (UINT8)~GPU_RAIL_FLAG_RAIL_VALID;
    return;
  }

  Report->OnVsel  = ReadBack;
  Report->RailUv  = RK806_BUCK_UV_BASE + (UINT32)ReadBack * RK806_BUCK_UV_STEP;
  Report->Flags  |= GPU_RAIL_FLAG_RAISED;
  DEBUG ((DEBUG_ERROR, "GpuRail: raised BUCK5 0x28 -> 0x2C, read back %u uV\n", Report->RailUv));
}

/**
  With the option off: a BUCK5 at 0x2C is what an earlier boot with the
  option on left (the RK806 keeps its selectors across a SoC reset).  Step
  it back to the PMIC's 0x28 the same way it went up, each step read back.
  Any other value is left alone.  Lowering is safe here: the GPU is at
  GPLL / 6 = 198 MHz, which every voltage of the table covers.
**/
STATIC
VOID
LowerRail (
  IN OUT GPU_RAIL_REPORT  *Report
  )
{
  EFI_STATUS  Status;
  UINT8       Vsel;
  UINT8       ReadBack;

  if (Report->OnVsel != GPU_RAIL_CEIL_VSEL) {
    DEBUG ((DEBUG_ERROR, "GpuRail: 775 mV option off; BUCK5 0x%02x left as it is\n", Report->OnVsel));
    return;
  }

  if ((mChipName != RK806_EXPECT_NAME) || (mChipVer != RK806_EXPECT_VER) ||
      ((Report->Flags & GPU_RAIL_FLAG_ENABLED) == 0))
  {
    DEBUG ((DEBUG_ERROR, "GpuRail: 775 mV option off, BUCK5 0x2C, but the PMIC is not as expected; not written\n"));
    return;
  }

  for (Vsel = GPU_RAIL_CEIL_VSEL - 1; Vsel >= GPU_RAIL_FROM_VSEL; Vsel--) {
    Status = WriteVselChecked (Vsel, &ReadBack);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "GpuRail: BUCK5 wrote 0x%02x, read back 0x%02x (%r); writing 0x28\n", Vsel, ReadBack, Status));
      Rk806Write (RK806_BUCK5_ON_VSEL, GPU_RAIL_FROM_VSEL);
      Report->RailUv = 0;
      Report->Flags &= (UINT8)~GPU_RAIL_FLAG_RAIL_VALID;
      return;
    }

    MicroSecondDelay (GPU_RAIL_STEP_WAIT_US);
  }

  MicroSecondDelay (GPU_RAIL_SETTLE_US);
  Status = Rk806Read (RK806_BUCK5_ON_VSEL, &ReadBack);
  if (EFI_ERROR (Status) || (ReadBack != GPU_RAIL_FROM_VSEL)) {
    DEBUG ((DEBUG_ERROR, "GpuRail: BUCK5 final read 0x%02x (%r)\n", ReadBack, Status));
    Report->RailUv = 0;
    Report->Flags &= (UINT8)~GPU_RAIL_FLAG_RAIL_VALID;
    return;
  }

  Report->OnVsel = ReadBack;
  Report->RailUv = RK806_BUCK_UV_BASE + (UINT32)ReadBack * RK806_BUCK_UV_STEP;
  DEBUG ((DEBUG_ERROR, "GpuRail: 775 mV option off: lowered BUCK5 0x2C -> 0x28, read back %u uV\n", Report->RailUv));
}

/**
  The setup option decides: raise, or put an earlier raise back.
**/
STATIC
VOID
ApplyRailOption (
  IN OUT GPU_RAIL_REPORT  *Report
  )
{
  if (!FixedPcdGetBool (PcdGpuRail775Supported)) {
    return;
  }

  if (PcdGet8 (PcdGpuRail775) == GPU_RAIL_775_ON) {
    RaiseRail (Report);
  } else {
    LowerRail (Report);
  }
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
  if ((Report.Flags & GPU_RAIL_FLAG_RAIL_VALID) != 0) {
    ApplyRailOption (&Report);
  }

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

VOID
RK3576GpuRailSetupVariables (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT8       Var8;

  Var8   = FixedPcdGetBool (PcdGpuRail775Supported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"GpuRail775Supported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (L"GpuRail775", &gRK3576DxeFormSetGuid, NULL, &Size, &Var8);
  if (EFI_ERROR (Status) || (Var8 > GPU_RAIL_775_ON)) {
    Status = PcdSet8S (PcdGpuRail775, FixedPcdGet8 (PcdGpuRail775Default));
    ASSERT_EFI_ERROR (Status);
  }
}
