/** @file
 *
 *  Power up the NPU (RKNN, two cores) for an OS driver that has no clock,
 *  regulator or power-domain framework (Windows, woa-rk3576-npu).
 *
 *  On by default.  Runs only when "NPU (experimental)" is enabled in setup
 *  (PcdAcpiNpu), the firmware hands the OS ACPI tables, and the board names
 *  the I2C bus of its RK806 (PcdRk806I2cBase).  With any of those missing
 *  nothing in here touches the hardware, and RK3576AcpiPlatformDxe does not
 *  install the NPU's SSDT (Npu.asl) either.
 *
 *  The order is the one Linux uses: the rail, then the domains parent first,
 *  then the block.
 *
 *  1. The rail, vdd_npu_s0 = RK806 BUCK2 (dcdc-reg2 in the board device
 *     trees; RK806_BUCK2 -> VDD_NPU_S0 in the CM5 and ROCK 4D schematics),
 *     set to 800 mV and switched on.  The RK806 is on I2C1 at 0x23 on every
 *     RK3576 board this tree builds; the RK806 library in SpiLib speaks SPI
 *     only and is not used.  The I2C transfers below are the ones
 *     Rk806I2cPowerOffDxe makes, which have run on the TaishanPi 3M.  They
 *     need the bus set up by I2cDxe, which is why this runs at EndOfDxe and
 *     only on a board that sets PcdRk806I2cBase.  Registers from mainline
 *     include/linux/mfd/rk808.h and drivers/regulator/rk808-regulator.c:
 *       RK806_CHIP_NAME 0x5A, RK806_CHIP_VER 0x5B: the chip name 0x806 is
 *       0x5A[7:0] and 0x5B[7:4] (RK806_ID 0x8060 with RK8XX_ID_MSK)
 *       RK806_POWER_EN0 0x00, BUCK2 enable = BIT(1) | BIT(5) (ENABLE_MASK(1))
 *       RK806_BUCK2_ON_VSEL 0x1B, 500 mV + 6.25 mV * sel for sel 0..159
 *     800 mV is sel 0x30.  The NPU project measured 750 mV (the PMIC default
 *     on the CM5-IO) giving wrong words at 786 MHz and 800 mV none; this
 *     firmware runs the NPU at 594 MHz, where 800 mV is margin.
 *
 *  2. The clocks (clk-rk3576.c, "npu"), rates first, gates second:
 *       CLKSEL_CON(86) [8:7] clk_rknn_dsu0 mux (gpll cpll aupll spll)
 *                      [6:2] clk_rknn_dsu0 div
 *                      [1:0] hclk_rknn_root mux, not written (mainline does
 *                            not assign it)
 *       CLKGATE_CON(28) bit 9  aclk_rknn0
 *       CLKGATE_CON(29) bit 0  aclk_rknn1
 *       CLKGATE_CON(31) bit 4  hclk_rknn_root, 5 clk_rknn_dsu0,
 *                       8 pclk_nputop_root
 *       CLKGATE_CON(32) bit 0  aclk_rknn_cbuf, 5 hclk_npu_cm0_root,
 *                       12 hclk_rknn_cbuf
 *     clk_rknn_dsu0 = GPLL / 2 = 594 MHz, the rate the NPU nodes assign
 *     (assigned-clock-rates = <594000000>), rounded down the way
 *     clk_set_rate() rounds, from the GPLL read back.  The CRU, not SCMI:
 *     the SCMI/PVTPLL route was measured on hardware to complete no job.
 *     hclk_npu_cm0_root is not an NPU core clock; it is in PD_NPUTOP's clock
 *     list, which genpd holds on across that domain's transition.
 *
 *  3. The resets (rst-rk3576.c), released: SOFTRST_CON(28) bits 9, 11, 12
 *     (A_RKNN0, A/L_RKNN0_BIU), SOFTRST_CON(29) bits 0, 2, 3 (A_RKNN1,
 *     A/L_RKNN1_BIU), SOFTRST_CON(31) bits 1, 9 (L_NPUSUBSYS_BIU,
 *     P_NPUTOP_BIU), SOFTRST_CON(32) bits 0, 11, 12, 13 (A_RKNN_CBUF,
 *     A_RKNNTOP_BIU, H_RKNN_CBUF, H_RKNNTOP_BIU).  Released is their reset
 *     state; the write is for certainty.
 *
 *  4. The domains (pm-domains.c rk3576_pm_domains[], with the RK3576 NPU
 *     series' pmdomain patches: PD_NPU need_regulator, 15 us on NPUTOP):
 *       PD_NPU    PWR_CON0 (0x210) bit 0, on when STATUS (0x230) bit 0 = 0,
 *                 no bus idle, no clock ungate
 *       PD_NPUTOP PWR_CON1 (0x214) bit 6, on when REPAIR (0x570) bit 22,
 *                 idle req/ack/idle 0x18, ungate 0x18, 15 us settle
 *       PD_NPU0   PWR_CON1 bit 7, REPAIR bit 23, idle BIT1, ungate 0x1a
 *       PD_NPU1   PWR_CON1 bit 8, REPAIR bit 24, idle BIT2, ungate 0x1c
 *     Each one in rockchip_pd_power()'s order: ungate, power on, wait for
 *     it, release the bus idle request, wait for the ack, settle, gate.
 *     The 15 us after NPUTOP is where Linux restores the domain's QoS; a QoS
 *     access before it is an SError on a cold boot.  Nothing here touches a
 *     QoS register at all, before or after, and NPU0/NPU1 start only after
 *     the 15 us.  A domain already on is left as it is (Linux does the
 *     same), apart from releasing a bus idle request that is still set.
 *
 *  Afterwards, reads only: each core's PC version registers and the status
 *  word of all four IOMMU instances (bit 0 = paging enabled; it should be 0,
 *  the reset state, so the NPU's DMA goes through untranslated).
 *
 *  Linux is unaffected apart from the rail: genpd finds the domains on and
 *  takes over, as it does for the ones U-Boot leaves on, and the 800 mV
 *  stays (the regulator core does not move a voltage inside the DT range).
 *
 *  THIS FILE HAS NOT BEEN RUN ON HARDWARE.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

#include "Npu.h"

#define PMU_BASE          0x27380000UL
#define PMU_REQ0          (PMU_BASE + 0x110)
#define PMU_ACK0          (PMU_BASE + 0x120)
#define PMU_IDLE0         (PMU_BASE + 0x128)
#define PMU_CLK_UNGATE    (PMU_BASE + 0x140)
#define PMU_PWR_CON0      (PMU_BASE + 0x210)
#define PMU_PWR_CON1      (PMU_BASE + 0x214)
#define PMU_STATUS0       (PMU_BASE + 0x230)
#define PMU_REPAIR0       (PMU_BASE + 0x570)

#define NPU_PWR_BIT       BIT0              /* PWR_CON0, and STATUS0 (1 = off) */

#define CRU_BASE          0x27200000UL
#define CRU_CLKSEL(n)     (CRU_BASE + 0x300 + (n) * 4)
#define CRU_GATE(n)       (CRU_BASE + 0x800 + (n) * 4)
#define CRU_SOFTRST(n)    (CRU_BASE + 0xA00 + (n) * 4)

#define DSU0_SEL_MASK     0x1FCU            /* CLKSEL_CON(86) [8:2] */
#define DSU0_DIV_SHIFT    2
#define DSU0_MUX_SHIFT    7                 /* 0 = gpll */
#define DSU0_HZ           594000000U

/* PLL registers, as Vdec.c and DwcSdhciPlatformLib.c read them. */
#define RK3576_GPLL_CON0        (CRU_BASE + 0x1C0)
#define RK3576_MODE_CON00       (CRU_BASE + 0x280)
#define RK3576_GPLL_MODE_SHIFT  2
#define RK3576_OSC_HZ           24000000U

/* The two cores (rk3576.dtsi rknn_core_0/1, rknn_mmu_0/1). */
#define NPU_CORE0_BASE    0x27700000UL
#define NPU_CORE1_BASE    0x27708000UL
#define NPU_PC_VERSION    0x0000
#define NPU_PC_VERSION_NUM 0x0004
#define NPU_MMU_OFFSET    0x2000            /* instances at +0x2000, +0x2100 */
#define RK_MMU_STATUS     0x04

/* Rockchip I2C controller (as Rk806I2cPowerOffDxe drives it). */
#define I2C_CON           0x000
#define I2C_CON_EN        BIT0
#define I2C_CON_MOD(m)    ((m) << 1)
#define I2C_CON_START     BIT3
#define I2C_CON_STOP      BIT4
#define I2C_CON_LASTACK   BIT5
#define I2C_CON_CFG       0xFF00
#define I2C_MODE_TX       0
#define I2C_MODE_TRX      1
#define I2C_MRXADDR       0x008
#define I2C_MRXRADDR      0x00C
#define I2C_MTXCNT        0x010
#define I2C_MRXCNT        0x014
#define I2C_IEN           0x018
#define I2C_IPD           0x01C
#define I2C_IPD_MBTF      BIT2
#define I2C_IPD_MBRF      BIT3
#define I2C_IPD_START     BIT4
#define I2C_IPD_STOP      BIT5
#define I2C_IPD_NAK       BIT6
#define I2C_IPD_ALL       0x7F
#define I2C_TXDATA0       0x100
#define I2C_RXDATA0       0x200
#define I2C_TIMEOUT_US    100000

/* RK806 (include/linux/mfd/rk808.h). */
#define RK806_POWER_EN0      0x00
#define RK806_BUCK2_ON_VSEL  0x1B
#define RK806_CHIP_NAME      0x5A
#define RK806_CHIP_VER       0x5B
#define RK806_BUCK2_EN       BIT1
#define RK806_BUCK2_EN_WMSK  BIT5
#define RK806_BUCK_UV(sel)   (500000U + 6250U * (sel))
#define NPU_RAIL_SEL         0x30            /* 800 mV */
#define NPU_RAIL_SEL_MIN     0x08            /* 550 mV, the DT minimum */
#define NPU_RAIL_SEL_MAX     0x48            /* 950 mV, the DT maximum */

typedef struct {
  CONST CHAR8  *Name;
  UINT32       PwrBit;        /* PWR_CON1 */
  UINT32       RepairBit;     /* REPAIR0: 1 = on */
  UINT32       IdleBits;      /* REQ0 / ACK0 / IDLE0 */
  UINT32       UngateBits;    /* CLK_UNGATE */
  UINT32       SettleUs;
} NPU_DOMAIN;

STATIC CONST NPU_DOMAIN  mNpuTop = { "nputop", BIT6, BIT22, 0x18, 0x18, 15 };
STATIC CONST NPU_DOMAIN  mNpu0   = { "npu0",   BIT7, BIT23, BIT1, 0x1a, 0 };
STATIC CONST NPU_DOMAIN  mNpu1   = { "npu1",   BIT8, BIT24, BIT2, 0x1c, 0 };

STATIC UINTN  mI2cBase;
STATIC UINT8  mI2cAddress;

STATIC
BOOLEAN
PollBits (
  IN UINTN   Address,
  IN UINT32  Mask,
  IN UINT32  Want
  )
{
  UINTN  Retry;

  for (Retry = 0; Retry < 1000; Retry++) {
    if ((MmioRead32 (Address) & Mask) == Want) {
      return TRUE;
    }

    MicroSecondDelay (100);
  }

  return FALSE;
}

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

  MmioWrite32 (mI2cBase + I2C_TXDATA0, ((UINT32)mI2cAddress << 1) | ((UINT32)Reg << 8) | ((UINT32)Value << 16));
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

  MmioWrite32 (mI2cBase + I2C_MRXADDR, BIT24 | ((UINT32)mI2cAddress << 1) | 1);
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

/*
 * vdd_npu_s0 to 800 mV and on.  Refuses, and the caller then powers
 * nothing, unless the chip answers as an RK806 and BUCK2 already holds a
 * value inside the 550-950 mV the device trees allow: anything else means
 * this is not the PMIC or not the register it should be.  Every write is
 * read back.
 */
STATIC
BOOLEAN
NpuRailOn (
  VOID
  )
{
  UINT8  Name;
  UINT8  Ver;
  UINT8  Sel;
  UINT8  En;

  mI2cBase    = (UINTN)FixedPcdGet32 (PcdRk806I2cBase);
  mI2cAddress = FixedPcdGet8 (PcdRk806I2cAddress);

  if (EFI_ERROR (Rk806Read (RK806_CHIP_NAME, &Name)) ||
      EFI_ERROR (Rk806Read (RK806_CHIP_VER, &Ver)))
  {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: no answer from the RK806 at I2C 0x%lx/0x%02x\n", (UINT64)mI2cBase, mI2cAddress));
    return FALSE;
  }

  if ((Name != 0x80) || ((Ver & 0xF0) != 0x60)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: chip at I2C 0x%02x reads 0x%02x%02x, not an RK806\n", mI2cAddress, Name, Ver));
    return FALSE;
  }

  if (EFI_ERROR (Rk806Read (RK806_BUCK2_ON_VSEL, &Sel)) ||
      EFI_ERROR (Rk806Read (RK806_POWER_EN0, &En)))
  {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: BUCK2 could not be read\n"));
    return FALSE;
  }

  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: NPU: vdd_npu_s0 (BUCK2) found at sel 0x%02x = %u uV, %a\n",
    Sel,
    RK806_BUCK_UV (Sel),
    ((En & RK806_BUCK2_EN) != 0) ? "on" : "off"
    ));

  if ((Sel < NPU_RAIL_SEL_MIN) || (Sel > NPU_RAIL_SEL_MAX)) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: BUCK2 outside 550-950 mV, not touching it\n"));
    return FALSE;
  }

  if (Sel != NPU_RAIL_SEL) {
    if (EFI_ERROR (Rk806Write (RK806_BUCK2_ON_VSEL, NPU_RAIL_SEL)) ||
        EFI_ERROR (Rk806Read (RK806_BUCK2_ON_VSEL, &Sel)) ||
        (Sel != NPU_RAIL_SEL))
    {
      DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: BUCK2 did not take 800 mV (reads 0x%02x)\n", Sel));
      return FALSE;
    }
  }

  if ((En & RK806_BUCK2_EN) == 0) {
    if (EFI_ERROR (Rk806Write (RK806_POWER_EN0, RK806_BUCK2_EN | RK806_BUCK2_EN_WMSK)) ||
        EFI_ERROR (Rk806Read (RK806_POWER_EN0, &En)) ||
        ((En & RK806_BUCK2_EN) == 0))
    {
      DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: BUCK2 did not switch on (POWER_EN0 0x%02x)\n", En));
      return FALSE;
    }
  }

  /*
   * regulator-enable-ramp-delay = <400> (us) and regulator-ramp-delay =
   * <12500> (uV/us) in the device trees: 400 us + 4 us at most.
   */
  MicroSecondDelay (1000);

  DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: vdd_npu_s0 (BUCK2) %u uV, on\n", RK806_BUCK_UV (Sel)));
  return TRUE;
}

STATIC
UINT32
NpuGpllRate (
  VOID
  )
{
  UINT32  Con0Val;
  UINT32  Con1Val;
  UINT32  M;
  UINT32  P;
  UINT32  S;
  INT16   K;
  UINT64  Rate;

  if (((MmioRead32 (RK3576_MODE_CON00) >> RK3576_GPLL_MODE_SHIFT) & 0x3) != 1) {
    return RK3576_OSC_HZ;
  }

  Con0Val = MmioRead32 (RK3576_GPLL_CON0);
  Con1Val = MmioRead32 (RK3576_GPLL_CON0 + 4);
  K       = (INT16)(MmioRead32 (RK3576_GPLL_CON0 + 8) & 0xFFFF);

  if ((Con0Val & BIT15) != 0) {
    return RK3576_OSC_HZ;
  }

  M = Con0Val & 0x3FF;
  P = Con1Val & 0x3F;
  S = (Con1Val >> 6) & 0x7;
  if ((M == 0) || (P == 0)) {
    return RK3576_OSC_HZ;
  }

  Rate = MultU64x32 ((UINT64)RK3576_OSC_HZ, M);
  if (K != 0) {
    Rate += DivS64x64Remainder (MultS64x64 ((INT64)RK3576_OSC_HZ, K), 65536, NULL);
  }

  return (UINT32)DivU64x32 (Rate, P << S);
}

/* The smallest divider (1..32) that brings Parent to Target or below. */
STATIC
UINT32
NpuDivField (
  IN UINT32  Parent,
  IN UINT32  Target
  )
{
  UINT32  Div;

  Div = (Parent + Target - 1) / Target;
  if (Div == 0) {
    Div = 1;
  }

  if (Div > 32) {
    Div = 32;
  }

  return Div - 1;
}

/* Release a bus idle request and wait until the NIU has left idle. */
STATIC
BOOLEAN
NpuIdleRelease (
  IN CONST NPU_DOMAIN  *Pd
  )
{
  MmioWrite32 (PMU_REQ0, (Pd->IdleBits << 16) | 0);
  if (!PollBits (PMU_ACK0, Pd->IdleBits, 0) || !PollBits (PMU_IDLE0, Pd->IdleBits, 0)) {
    DEBUG ((
      DEBUG_ERROR,
      "RK3576Dxe: NPU: %a bus did not leave idle (ACK0=0x%08x IDLE0=0x%08x)\n",
      Pd->Name,
      MmioRead32 (PMU_ACK0),
      MmioRead32 (PMU_IDLE0)
      ));
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
NpuDomainOn (
  IN CONST NPU_DOMAIN  *Pd
  )
{
  BOOLEAN  Ok;

  if ((MmioRead32 (PMU_REPAIR0) & Pd->RepairBit) != 0) {
    if ((MmioRead32 (PMU_IDLE0) & Pd->IdleBits) == 0) {
      DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: %a was already on\n", Pd->Name));
      return TRUE;
    }

    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: %a was already on with its bus idle, releasing it\n", Pd->Name));
    MmioWrite32 (PMU_CLK_UNGATE, (Pd->UngateBits << 16) | Pd->UngateBits);
    Ok = NpuIdleRelease (Pd);
    MmioWrite32 (PMU_CLK_UNGATE, (Pd->UngateBits << 16) | 0);
    return Ok;
  }

  MmioWrite32 (PMU_CLK_UNGATE, (Pd->UngateBits << 16) | Pd->UngateBits);

  Ok = FALSE;
  MmioWrite32 (PMU_PWR_CON1, (Pd->PwrBit << 16) | 0);
  if (!PollBits (PMU_REPAIR0, Pd->RepairBit, Pd->RepairBit)) {
    DEBUG ((
      DEBUG_ERROR,
      "RK3576Dxe: NPU: %a did not come up (STATUS0=0x%08x REPAIR0=0x%08x)\n",
      Pd->Name,
      MmioRead32 (PMU_STATUS0),
      MmioRead32 (PMU_REPAIR0)
      ));
    goto Out;
  }

  if (!NpuIdleRelease (Pd)) {
    goto Out;
  }

  /*
   * Where Linux restores the domain's QoS.  No register access of any kind
   * in this window, and the next domain starts only after it.
   */
  if (Pd->SettleUs != 0) {
    MicroSecondDelay (Pd->SettleUs);
  }

  Ok = TRUE;

Out:
  MmioWrite32 (PMU_CLK_UNGATE, (Pd->UngateBits << 16) | 0);
  return Ok;
}

STATIC
VOID
NpuLogCore (
  IN UINTN  Base,
  IN UINTN  Index
  )
{
  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: NPU: core %u version=0x%08x version_num=0x%08x mmu status=0x%08x/0x%08x\n",
    (UINT32)Index,
    MmioRead32 (Base + NPU_PC_VERSION),
    MmioRead32 (Base + NPU_PC_VERSION_NUM),
    MmioRead32 (Base + NPU_MMU_OFFSET + RK_MMU_STATUS),
    MmioRead32 (Base + NPU_MMU_OFFSET + 0x100 + RK_MMU_STATUS)
    ));
}

BOOLEAN
RK3576NpuSupported (
  VOID
  )
{
  return FixedPcdGet32 (PcdRk806I2cBase) != 0;
}

VOID
RK3576NpuPowerOn (
  VOID
  )
{
  UINT32  Gpll;
  UINT32  Div;

  if (!RK3576NpuSupported ()) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: this board does not name its RK806's I2C bus, NPU left off\n"));
    return;
  }

  /* 1. The rail.  Nothing else is touched unless it is confirmed. */
  if (!NpuRailOn ()) {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: rail not confirmed, NPU left off\n"));
    return;
  }

  /* 2. Clocks: the rate first, then the gates. */
  Gpll = NpuGpllRate ();
  Div  = NpuDivField (Gpll, DSU0_HZ);
  MmioWrite32 (CRU_CLKSEL (86), (DSU0_SEL_MASK << 16) | (0U << DSU0_MUX_SHIFT) | (Div << DSU0_DIV_SHIFT));
  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: NPU: gpll %u Hz -> clk_rknn_dsu0 %u Hz (CLKSEL_CON86=0x%08x)\n",
    Gpll,
    Gpll / (Div + 1),
    MmioRead32 (CRU_CLKSEL (86))
    ));

  MmioWrite32 (CRU_GATE (28), (BIT9 << 16) | 0);
  MmioWrite32 (CRU_GATE (29), (BIT0 << 16) | 0);
  MmioWrite32 (CRU_GATE (31), ((BIT4 | BIT5 | BIT8) << 16) | 0);
  MmioWrite32 (CRU_GATE (32), ((BIT0 | BIT5 | BIT12) << 16) | 0);

  /* 3. Resets released. */
  MmioWrite32 (CRU_SOFTRST (28), ((BIT9 | BIT11 | BIT12) << 16) | 0);
  MmioWrite32 (CRU_SOFTRST (29), ((BIT0 | BIT2 | BIT3) << 16) | 0);
  MmioWrite32 (CRU_SOFTRST (31), ((BIT1 | BIT9) << 16) | 0);
  MmioWrite32 (CRU_SOFTRST (32), ((BIT0 | BIT11 | BIT12 | BIT13) << 16) | 0);

  /* 4. Domains, parent first. */
  if ((MmioRead32 (PMU_STATUS0) & NPU_PWR_BIT) != 0) {
    MmioWrite32 (PMU_PWR_CON0, (NPU_PWR_BIT << 16) | 0);
    if (!PollBits (PMU_STATUS0, NPU_PWR_BIT, 0)) {
      DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: npu did not come up (STATUS0=0x%08x)\n", MmioRead32 (PMU_STATUS0)));
      return;
    }
  } else {
    DEBUG ((DEBUG_ERROR, "RK3576Dxe: NPU: npu was already on\n"));
  }

  if (!NpuDomainOn (&mNpuTop)) {
    return;
  }

  if (!NpuDomainOn (&mNpu0) || !NpuDomainOn (&mNpu1)) {
    return;
  }

  DEBUG ((
    DEBUG_ERROR,
    "RK3576Dxe: NPU: powered, STATUS0=0x%08x REPAIR0=0x%08x IDLE0=0x%08x\n",
    MmioRead32 (PMU_STATUS0),
    MmioRead32 (PMU_REPAIR0),
    MmioRead32 (PMU_IDLE0)
    ));

  /* Safe to touch the block only now.  Reads only. */
  NpuLogCore (NPU_CORE0_BASE, 0);
  NpuLogCore (NPU_CORE1_BASE, 1);
}
