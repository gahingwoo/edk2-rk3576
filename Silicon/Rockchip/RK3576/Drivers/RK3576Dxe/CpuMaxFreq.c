/** @file
 *
 *  RK3576 "CPU Maximum Frequency" = Max: raise the two CPU rails on the
 *  RK806, then the two cluster clocks, to mainline's top operating points.
 *
 *  What is set, and where every number comes from
 *  ----------------------------------------------
 *
 *    cluster   rail                     RK806    rate       voltage
 *    A72 (B)   vdd_cpu_big_s0, BUCK1    0x1A     2208 MHz   925.0 mV
 *    A53 (L)   vdd_cpu_lit_s0, BUCK3    0x1C     2016 MHz   900.0 mV
 *
 *  - Rates and voltages: the last entries of cluster1_opp_table and
 *    cluster0_opp_table in mainline rk3576.dtsi (opp-microvolt
 *    <925000 925000 950000> and <900000 900000 950000>).  Never more.
 *  - Rails: dcdc-reg1 is vdd_cpu_big_s0 and dcdc-reg3 is vdd_cpu_lit_s0
 *    under pmic@23 on &i2c1, with regulator-min/max-microvolt 550000/950000,
 *    in all three boards' mainline device trees (rk3576-armsom-cm5.dtsi,
 *    rk3576-lckfb-tspi-3m.dts, rk3576-rock-4d.dts).
 *  - Registers: RK806_BUCK1_ON_VSEL 0x1A, RK806_BUCK3_ON_VSEL 0x1C,
 *    RK806_CHIP_NAME 0x5A, RK806_CHIP_VER 0x5B (include/linux/mfd/rk808.h).
 *    The selector is the whole byte, 500 mV + 6.25 mV per step up to 159
 *    (rk806_buck_voltage_ranges, vsel_mask 0xff, rk808-regulator.c), which
 *    is how mainline sets these rails (regulator_set_voltage_sel_regmap on
 *    the ON_VSEL register).  RK806_ID 0x8060 under RK8XX_ID_MSK 0xfff0 is
 *    the chip ID; the TaishanPi 3M's reads CHIP_NAME 0x80, VER 0x6A.
 *  - SoC variant: OTP byte 0x08 bits 4:0 (RK3576_OTP_SPECIFICATION_OFFSET in
 *    U-Boot's rk3576.c, specification-serial-number in the vendor DT), after
 *    the cpu-code at 0x02 reads 0x35 0x76.  The vendor kernel
 *    (rockchip_opp_select.c) gives 2016/2208 MHz only to the plain RK3576:
 *    RK3576S (0x13) stops at 1920/2112, RK3576J (0x0A) and RK3576M (0x0D)
 *    have their own lower tables, and mainline has no OPP for any of those
 *    rates.  So anything but a plain RK3576 stays at the default.
 *
 *  Order, and what is never written
 *  --------------------------------
 *  The cluster is already at the default rate (CpuPerf.c) when this runs.
 *  For each cluster: read the rail, raise it if it is below the target,
 *  read the selector back, wait out the ramp, and only then raise the
 *  clock.  Any failure leaves that cluster at the default rate, with its
 *  rail at or above what it was, which is the safe direction.  Nothing
 *  here lowers a voltage, so the "lower the clock before the voltage" half
 *  of the rule has no case to handle.  The only PMIC registers written are
 *  the two ON_VSEL selectors; the ramp configuration is not touched, so the
 *  wait assumes the slowest rate BUCKn_CONFIG[7:6] can select (6250 uV/us,
 *  rk806_ramp_delay_table_dcdc) plus a fixed margin.
 *
 *  The RK806 keeps its selectors across a SoC reset (seen on the TaishanPi
 *  3M for SLEEP_CONFIG0).  After a boot with Max, a warm restart into the
 *  default keeps the raised rails until the board is powered off; the log
 *  line "CpuMaxFreq: ... now" shows what they are on every boot.
 *
 *  PVTPLL
 *  ------
 *  TF-A runs both clusters from a PVTPLL (plat/rockchip/rk3576/scmi/
 *  rk3576_clk.c): a ring oscillator in the cluster's own supply whose
 *  output follows the voltage.  The SCMI rate only picks the ring length,
 *  and the A72 table uses the same length (5, 0) for 1608, 1800, 2016 and
 *  2112 MHz.  So the real frequency at a given setting is decided by the
 *  rail, and a rate read back over SCMI is TF-A's record of the request,
 *  not a measurement.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OtpLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/ArmScmiClock2Protocol.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/RockchipI2cMasterProtocol.h>

#include <ScmiDefinitions.h>
#include <VarStoreData.h>

#include "RK3576DxeFormSetGuid.h"
#include "CpuMaxFreq.h"

//
// RK806 (include/linux/mfd/rk808.h, drivers/regulator/rk808-regulator.c)
//
#define RK806_CHIP_NAME                0x5A
#define RK806_CHIP_VER                 0x5B
#define RK806_ID                       0x8060
#define RK8XX_ID_MSK                   0xFFF0
#define RK806_BUCK1_ON_VSEL            0x1A
#define RK806_BUCK3_ON_VSEL            0x1C
#define RK806_BUCK_UV_BASE             500000
#define RK806_BUCK_UV_STEP             6250
#define RK806_BUCK_SEL_LINEAR_MAX      159
#define RK806_BUCK_SLOWEST_RAMP_UV_US  6250

//
// regulator-min/max-microvolt of vdd_cpu_big_s0 and vdd_cpu_lit_s0.  A rail
// that reads outside this is not what the device tree describes, and is
// left alone.
//
#define CPU_RAIL_UV_MIN  550000
#define CPU_RAIL_UV_MAX  950000

//
// Time added to the computed ramp before the clock goes up.  The RK806's
// own regulator-enable-ramp-delay is 400 us; this is more than twice that.
//
#define CPU_RAIL_SETTLE_US  1000

//
// OTP (U-Boot arch/arm/mach-rockchip/rk3576/rk3576.c)
//
#define RK3576_OTP_CPU_CODE_OFFSET       0x02
#define RK3576_OTP_SPECIFICATION_OFFSET  0x08

typedef struct {
  CONST CHAR8    *Cluster;
  CONST CHAR8    *Rail;
  UINT8          VselReg;
  UINT32         ScmiClock;
  UINT64         MaxHz;
  UINT32         MaxUv;
} CPU_CLUSTER_MAX;

STATIC CONST CPU_CLUSTER_MAX  mClusters[] = {
  { "A72 (ARMCLK_B)", "vdd_cpu_big_s0 (RK806 BUCK1)", RK806_BUCK1_ON_VSEL, SCMI_ARMCLK_B, 2208000000ULL, 925000 },
  { "A53 (ARMCLK_L)", "vdd_cpu_lit_s0 (RK806 BUCK3)", RK806_BUCK3_ON_VSEL, SCMI_ARMCLK_L, 2016000000ULL, 900000 },
};

//
// Both targets must be exact selector values inside the rail's range.
//
STATIC_ASSERT ((925000 - RK806_BUCK_UV_BASE) % RK806_BUCK_UV_STEP == 0, "BUCK1 target not on a selector step");
STATIC_ASSERT ((900000 - RK806_BUCK_UV_BASE) % RK806_BUCK_UV_STEP == 0, "BUCK3 target not on a selector step");
STATIC_ASSERT (925000 <= CPU_RAIL_UV_MAX, "BUCK1 target above the rail maximum");
STATIC_ASSERT (900000 <= CPU_RAIL_UV_MAX, "BUCK3 target above the rail maximum");

typedef struct {
  UINTN                OperationCount;
  EFI_I2C_OPERATION    Operation[2];
} RK806_I2C_REQUEST;

STATIC SCMI_CLOCK2_PROTOCOL   *mClock;
STATIC EFI_I2C_MASTER_PROTOCOL  *mI2c;
STATIC EFI_EVENT              mI2cNotifyEvent;
STATIC VOID                   *mI2cRegistration;

STATIC
EFI_STATUS
Rk806Read (
  IN  UINT8  Reg,
  OUT UINT8  *Value
  )
{
  RK806_I2C_REQUEST  Request;

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

STATIC
UINT32
VselToUv (
  IN UINT8  Vsel
  )
{
  return RK806_BUCK_UV_BASE + (UINT32)Vsel * RK806_BUCK_UV_STEP;
}

/**
  Read a CPU rail.  Fails for a selector outside the 6.25 mV range or a
  voltage outside the rail's device tree limits.
**/
STATIC
EFI_STATUS
ReadRail (
  IN  CONST CPU_CLUSTER_MAX  *Cluster,
  OUT UINT8                  *Vsel
  )
{
  EFI_STATUS  Status;

  Status = Rk806Read (Cluster->VselReg, Vsel);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: read 0x%02x failed: %r\n", Cluster->Rail, Cluster->VselReg, Status));
    return Status;
  }

  if (*Vsel > RK806_BUCK_SEL_LINEAR_MAX) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: selector 0x%02x is above 1.5 V, not a CPU rail\n", Cluster->Rail, *Vsel));
    return EFI_DEVICE_ERROR;
  }

  if ((VselToUv (*Vsel) < CPU_RAIL_UV_MIN) || (VselToUv (*Vsel) > CPU_RAIL_UV_MAX)) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuMaxFreq: %a: %u uV is outside %u..%u uV\n",
      Cluster->Rail,
      VselToUv (*Vsel),
      CPU_RAIL_UV_MIN,
      CPU_RAIL_UV_MAX
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Bring a rail up to the cluster's top OPP voltage.  Never lowers it and
  never goes above MaxUv.
**/
STATIC
EFI_STATUS
RaiseRail (
  IN CONST CPU_CLUSTER_MAX  *Cluster
  )
{
  EFI_STATUS  Status;
  UINT8       Before;
  UINT8       Target;
  UINT8       After;
  UINT32      DeltaUv;

  After = 0;
  Status = ReadRail (Cluster, &Before);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Target = (UINT8)((Cluster->MaxUv - RK806_BUCK_UV_BASE) / RK806_BUCK_UV_STEP);
  if (Before >= Target) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuMaxFreq: %a: already %u uV, at or above %u uV, not written\n",
      Cluster->Rail,
      VselToUv (Before),
      Cluster->MaxUv
      ));
    return EFI_SUCCESS;
  }

  Status = Rk806Write (Cluster->VselReg, Target);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: write 0x%02x failed: %r\n", Cluster->Rail, Target, Status));
    return Status;
  }

  Status = Rk806Read (Cluster->VselReg, &After);
  if (EFI_ERROR (Status) || (After != Target)) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuMaxFreq: %a: wrote 0x%02x, read back 0x%02x (%r); putting 0x%02x back\n",
      Cluster->Rail,
      Target,
      After,
      Status,
      Before
      ));
    Rk806Write (Cluster->VselReg, Before);
    return EFI_DEVICE_ERROR;
  }

  DeltaUv = Cluster->MaxUv - VselToUv (Before);
  MicroSecondDelay (DeltaUv / RK806_BUCK_SLOWEST_RAMP_UV_US + 1 + CPU_RAIL_SETTLE_US);

  DEBUG ((
    DEBUG_ERROR,
    "CpuMaxFreq: %a: %u uV -> %u uV (selector 0x%02x -> 0x%02x)\n",
    Cluster->Rail,
    VselToUv (Before),
    Cluster->MaxUv,
    Before,
    Target
    ));
  return EFI_SUCCESS;
}

STATIC
VOID
RaiseClock (
  IN CONST CPU_CLUSTER_MAX  *Cluster
  )
{
  EFI_STATUS  Status;
  UINT64      Rate;

  Status = mClock->RateSet (mClock, Cluster->ScmiClock, Cluster->MaxHz);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: RateSet %lu Hz failed: %r\n", Cluster->Cluster, Cluster->MaxHz, Status));
    return;
  }

  Rate   = 0;
  Status = mClock->RateGet (mClock, Cluster->ScmiClock, &Rate);
  DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: now %lu Hz (%r)\n", Cluster->Cluster, Rate, Status));
}

/**
  TRUE for a plain RK3576, the only variant the top OPPs are for.
**/
STATIC
BOOLEAN
IsPlainRk3576 (
  VOID
  )
{
  UINT8  CpuCode[2];
  UINT8  Specification;

  ZeroMem (CpuCode, sizeof (CpuCode));
  Specification = 0xFF;
  OtpRead (RK3576_OTP_CPU_CODE_OFFSET, sizeof (CpuCode), CpuCode);
  OtpRead (RK3576_OTP_SPECIFICATION_OFFSET, sizeof (Specification), &Specification);

  DEBUG ((
    DEBUG_ERROR,
    "CpuMaxFreq: OTP cpu-code %02x %02x, specification 0x%02x\n",
    CpuCode[0],
    CpuCode[1],
    Specification & 0x1F
    ));

  //
  // A read that did not work comes back zeroed, and a zero specification
  // is also what a plain chip has, so the cpu-code has to prove the read.
  //
  if ((CpuCode[0] != 0x35) || (CpuCode[1] != 0x76)) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: OTP does not read as an RK3576, cannot tell the variant\n"));
    return FALSE;
  }

  //
  // U-Boot prints the variant as '@' + specification when it is above 1
  // (J = 0x0A, M = 0x0D, S = 0x13); 0 and 1 are the plain RK3576.
  //
  if ((Specification & 0x1F) > 1) {
    DEBUG ((
      DEBUG_ERROR,
      "CpuMaxFreq: this is an RK3576%c, whose top OPPs are below 2016/2208 MHz and not in mainline\n",
      '@' + (Specification & 0x1F)
      ));
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
IsRk806 (
  VOID
  )
{
  UINT8  Name;
  UINT8  Ver;

  if (EFI_ERROR (Rk806Read (RK806_CHIP_NAME, &Name)) ||
      EFI_ERROR (Rk806Read (RK806_CHIP_VER, &Ver)))
  {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: no answer from the PMIC at 0x%02x\n", FixedPcdGet8 (PcdRk806I2cAddress)));
    return FALSE;
  }

  if ((((UINT32)Name << 8 | Ver) & RK8XX_ID_MSK) != RK806_ID) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: PMIC ID 0x%02x%02x is not an RK806\n", Name, Ver));
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
CpuMaxFreqRun (
  VOID
  )
{
  UINT8  Setting;
  UINT8  Vsel;
  UINTN  Index;

  Setting = PcdGet8 (PcdCpuMaxFreq);

  if (!IsRk806 ()) {
    return;
  }

  //
  // Say what the rails are on every boot, whatever the setting: nothing
  // else in the firmware knows which voltage the default rate runs at.
  //
  for (Index = 0; Index < ARRAY_SIZE (mClusters); Index++) {
    if (!EFI_ERROR (ReadRail (&mClusters[Index], &Vsel))) {
      DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a: %u uV (selector 0x%02x)\n", mClusters[Index].Rail, VselToUv (Vsel), Vsel));
    }
  }

  if (Setting != CPU_MAX_FREQ_MAX) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: setting is the default rate, nothing changed\n"));
    return;
  }

  if (!IsPlainRk3576 ()) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: staying at the default rate\n"));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mClusters); Index++) {
    if (EFI_ERROR (RaiseRail (&mClusters[Index]))) {
      DEBUG ((DEBUG_ERROR, "CpuMaxFreq: %a stays at the default rate\n", mClusters[Index].Cluster));
      continue;
    }

    RaiseClock (&mClusters[Index]);
  }
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
  mI2cNotifyEvent = NULL;

  CpuMaxFreqRun ();
}

VOID
RK3576CpuMaxFreqSetupVariables (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT8       Var8;

  Var8   = FixedPcdGetBool (PcdCpuMaxFreqSupported) ? 1 : 0;
  Status = gRT->SetVariable (
                  L"CpuMaxFreqSupported",
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Var8),
                  &Var8
                  );
  ASSERT_EFI_ERROR (Status);

  Size   = sizeof (UINT8);
  Status = gRT->GetVariable (L"CpuMaxFreq", &gRK3576DxeFormSetGuid, NULL, &Size, &Var8);
  if (EFI_ERROR (Status) || (Var8 > CPU_MAX_FREQ_MAX)) {
    Status = PcdSet8S (PcdCpuMaxFreq, FixedPcdGet8 (PcdCpuMaxFreqDefault));
    ASSERT_EFI_ERROR (Status);
  }
}

VOID
RK3576CpuMaxFreqStart (
  IN SCMI_CLOCK2_PROTOCOL  *Clock
  )
{
  if (!FixedPcdGetBool (PcdCpuMaxFreqSupported)) {
    return;
  }

  //
  // The PMIC's bus is set up by I2cDxe, which may not have run yet.  The
  // notify fires at once for buses already there.
  //
  mClock          = Clock;
  mI2cNotifyEvent = EfiCreateProtocolNotifyEvent (
                      &gRockchipI2cMasterProtocolGuid,
                      TPL_CALLBACK,
                      OnI2cMasterAvailable,
                      NULL,
                      &mI2cRegistration
                      );
  if (mI2cNotifyEvent == NULL) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreq: could not register for the I2C bus, staying at the default rate\n"));
  }
}
