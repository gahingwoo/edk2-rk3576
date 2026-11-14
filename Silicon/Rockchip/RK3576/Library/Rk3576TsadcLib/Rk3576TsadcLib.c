/** @file
 *
 *  RK3576 TSADC (temperature sensor ADC): read-out only.
 *
 *  Sources, cited below as [L], [D], [C], [R], [T], [A]:
 *
 *    [L] mainline Linux drivers/thermal/rockchip_thermal.c (linux-next
 *        20260914): rk3576_tsadc_data (:1306-1329), rk_tsadcv8_initialize
 *        (:847-864), rk_tsadcv4_control (:928-937), rk_tsadcv3_tshut_temp
 *        (:1039-1057), rk_tsadcv4_tshut_mode (:1075-1090), the trim code in
 *        rockchip_get_trim_configuration (:1505-1557) and
 *        rockchip_thermal_register_sensor (:1625-1650), and rk3588_code_table
 *        (:568-575), which rk3576 uses.
 *    [D] mainline arch/arm64/boot/dts/rockchip/rk3576.dtsi: tsadc@2ae70000
 *        (:2753-2800), the OTP trim cells (:2190-2213).
 *    [C] mainline drivers/clk/rockchip/clk-rk3576.c:640-644 and
 *        rst-rk3576.c:116-117.
 *    [R] RK3576 TRM V1.2 Part 1, chapter 19 (TS-ADC) and the CRU register
 *        descriptions (CLKSEL_CON59, GATE_CON13, SOFTRST_CON13).
 *    [T] TF-A plat/rockchip/rk3576 (BL31 v2.14 source tree, read only).
 *    [A] Silicon/Rockchip/RK3576/Library/OtpLib/OtpLib.c (this tree).
 *
 *  What this does NOT do, on purpose
 *  ---------------------------------
 *
 *  TF-A has already armed both TSHUT paths downstream of the TSADC:
 *  system_reset_init() writes CRU_GLB_RST_CON = 0xffdf ("enable tsadc trigger
 *  global reset") and muxes GPIO0_A1 to tsadc_shut_m0 ([T] soc.c:84-117).
 *  The only thing between a TSADC comparator and a chip reset or a PMIC
 *  shutdown is therefore the TSADC's own TSADC_GPIO_EN (0x18) and
 *  TSADC_CRU_EN (0x1C), both 0 from reset ([R] 19.4.2).  This library never
 *  writes either of them, nor HT_INT_EN / LT_EN / LT_INT_EN (interrupts),
 *  nor USER_CON, nor any reset: mainline's probe asserts and deasserts
 *  SRST_P_TSADC and SRST_TSADC ([L]:1686-1688, :1740); the firmware does not.
 *  If the block is found in reset it is left alone and the read-out is off.
 *
 *  Mainline writes the TSHUT thresholds (COMPn_SHUT) with the code for
 *  120 C ([D] rockchip,hw-tshut-temp).  The comparator is ">=" ([R] 19.4.3)
 *  and resets to 0, so with sampling on and the threshold at 0 every sample
 *  would count as "too hot" and only GPIO_EN/CRU_EN would stop it.  This
 *  library writes 0x3FF, the largest value the 10-bit field holds and a
 *  code no sample reaches (the table ends at 395), so even an output enabled
 *  by mistake would not fire.
 *
 *  The TSHUT polarity write mainline makes (AUTO_CON bit 8 = 0, low active,
 *  [L]:861-863) is skipped: it writes the reset value, and if anything earlier
 *  had changed the bit, changing it back would toggle the PMIC pin.
 *
 *  Linux with a device tree
 *  ------------------------
 *
 *  rockchip_thermal's probe resets the block (both resets, [L]:1740) before
 *  it writes anything, and the clock framework sets CLK_TSADC itself from
 *  assigned-clock-rates.  Nothing written here survives into the kernel.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/OtpLib.h>
#include <Library/PcdLib.h>
#include <Library/Rk3576TsadcLib.h>
#include <Library/TimerLib.h>
#include <Soc.h>

/*
 * CRU ([C], [R]).  PCLK_TSADC is gate CLKGATE_CON(13) bit 8, CLK_TSADC gate
 * bit 9 and divider CLKSEL_CON(59) [7:0] from xin24m; resets SRST_P_TSADC
 * and SRST_TSADC are SOFTRST_CON(13) bits 8 and 9.  All three registers reset
 * to "running": gates 0 (open), resets 0 (deasserted), divider 0x0B
 * (24 MHz / 12 = 2 MHz, the rate the TRM asks for, [R] 19.5.2, and the rate
 * [D] assigns).
 *
 * The RK3588 numbers in RockchipIpRegs.h (SRST_P_TSADC 192, CLK_TSADC_DIV in
 * CLKSEL_CON41) are not these and are not used.
 */
#define TSADC_CLKGATE_CON     13
#define PCLK_TSADC_GATE_BIT   8
#define CLK_TSADC_GATE_BIT    9
#define TSADC_CLKSEL_CON      59
#define CLK_TSADC_DIV_MASK    0xFFU
#define CLK_TSADC_DIV_2MHZ    0x0BU
#define TSADC_SOFTRST_CON     13
#define SRST_P_TSADC_BIT      8
#define SRST_TSADC_BIT        9

#define TSADC_CRU_BITS  ((1U << PCLK_TSADC_GATE_BIT) | (1U << CLK_TSADC_GATE_BIT))

/* TSADC registers ([R] 19.4.2, [L]:189-212 TSADCV2_* / TSADCV3_*) */
#define TSADC_AUTO_CON              0x004
#define  TSADC_AUTO_EN              BIT0
#define TSADC_AUTO_SRC              0x00C
#define TSADC_HT_INT_EN             0x014
#define TSADC_GPIO_EN               0x018
#define TSADC_CRU_EN                0x01C
#define TSADC_DATA(n)               (0x02C + (n) * 4)
#define TSADC_COMP_SHUT(n)          (0x10C + (n) * 4)
#define TSADC_HIGH_INT_DEBOUNCE     0x14C
#define TSADC_HIGH_TSHUT_DEBOUNCE   0x150
#define TSADC_AUTO_PERIOD           0x154
#define TSADC_AUTO_PERIOD_HT        0x158

/* [L]:237-238, :246-247 */
#define TSADC_DEBOUNCE_COUNT        4
#define TSADC_AUTO_PERIOD_TIME      5000   /* 2.5 ms at 2 MHz */

#define TSADC_COMP_UNREACHABLE      0x3FF
#define TSADC_DATA_MASK             0x1FF  /* TSADCV4_DATA_MASK, [L]:235 */

#define HIWORD(Mask, Value)  (((UINT32)(Mask) << 16) | ((UINT32)(Value) & (Mask)))

#define TSADC_REG(Off)  (RK3576_TSADC_BASE + (Off))

/*
 * rk3588_code_table, which rk3576_tsadc_data uses ([L]:568-575, :1323-1328),
 * ADC_INCREMENT, data mask 0x1FF.  The first row {0, -40000} only bounds the
 * search; rk_tsadcv2_code_to_temp starts at row 1 ([L]:632).
 *
 * The TRM's own table differs ([R] Table 19-2: -40 C 220, 25 C 285, 85 C 345,
 * 125 C 385).  Mainline is followed, as it is what the kernel reports; at
 * 60 C the two disagree by about 3 codes, i.e. about 3 C.
 */
typedef struct {
  UINT32    Code;
  INT32     MilliCelsius;
} TSADC_TABLE_ROW;

STATIC CONST TSADC_TABLE_ROW  mCodeTable[] = {
  { 0,               -40000 },
  { 215,             -40000 },
  { 285,             25000  },
  { 350,             85000  },
  { 395,             125000 },
  { TSADC_DATA_MASK, 125000 },
};

/*
 * OTP trim cells, byte offsets in the non-secure OTP ([D]:2190-2213, each
 * reg = <off 0x2>, bits = <0 10>), in the order [D] assigns them to sensors
 * (sensor@0 soc, @1 bigcore, @2 litcore, @3 ddr, @4 npu, @5 gpu).
 *
 * These are the bytes mainline's nvmem driver serves from OTPC_NS at
 * 0x2A580000 with read_offset 0x700, i.e. OTP words 0x1C0 + off / 4:
 * 0x1D9 (soc) and 0x1C9..0x1CB (the others).  That is the non-secure block
 * (words 0x1C0-0x1FF), and it is read the same way and through the same
 * library ([A]) as the SMBIOS serial number (bytes 0x0A..0x19) and the GMAC
 * MAC seed (bytes 0x00..0x1F) are on every boot already.  It is not the
 * secure-boot key hash, which is in the secure OTP (words 0x80-0x87), behind
 * otp_s, a slave TF-A makes secure-only ([T] firewall.c:155); OTPC_NS is not
 * on that list.
 *
 * Note for channels 3 and 5: [D] gives sensor@3 the ddr trim and sensor@5 the
 * gpu trim, while the TRM ([R] Table 19-1) and [D]'s own thermal zones say
 * channel 3 is the GPU and 5 the DDR.  Only channels 0-2 are exposed to ACPI,
 * where the two agree.
 */
STATIC CONST UINT16  mTrimOtpOffset[RK3576_TSADC_CHANNEL_COUNT] = {
  0x64, 0x24, 0x26, 0x28, 0x2A, 0x2C
};

/* [L]:1322 trim_slope, [L]:1520-1524 trim_base defaults to 30 C, frac 0 */
#define TSADC_TRIM_SLOPE          923
#define TSADC_TRIM_BASE_MC        30000

/*
 * A trim this many codes (about 30 C) or more away from the 30 C code is
 * treated as a misread and ignored.  Mainline applies whatever it reads; this
 * bound is the firmware's own and only exists so that one bad OTP read cannot
 * shift every reading by tens of degrees.
 */
#define TSADC_TRIM_MAX_DELTA      32

EFI_STATUS
EFIAPI
Rk3576TsadcCodeToMilliCelsius (
  IN  UINT32  Code,
  OUT INT32   *MilliCelsius
  )
{
  UINTN   Row;
  UINT32  Lo;
  UINT32  Hi;

  /*
   * Below the table: rk_tsadcv2_code_to_temp returns -EAGAIN ([L]:660-661).
   * A data word with bits above the 9-bit mask set is treated as bad here
   * rather than masked as mainline does; the table ends at 395.
   */
  if ((Code > TSADC_DATA_MASK) || (Code < mCodeTable[1].Code)) {
    return EFI_NOT_READY;
  }

  for (Row = 2; Row < ARRAY_SIZE (mCodeTable); Row++) {
    if (Code <= mCodeTable[Row].Code) {
      break;
    }
  }

  Lo = mCodeTable[Row - 1].Code;
  Hi = mCodeTable[Row].Code;

  /* [L]:686-689, same integer division */
  *MilliCelsius = mCodeTable[Row - 1].MilliCelsius +
                  (INT32)(((UINT32)(mCodeTable[Row].MilliCelsius - mCodeTable[Row - 1].MilliCelsius) *
                           (Code - Lo)) / (Hi - Lo));
  return EFI_SUCCESS;
}

/*
 * rk_tsadcv2_temp_to_code ([L]:577-627) for one temperature inside the table,
 * as rk_tsadcv2_get_trim_code uses it ([L]:1092-1098).
 */
STATIC
UINT32
TsadcTempToCode (
  IN INT32  MilliCelsius
  )
{
  UINTN  Row;

  for (Row = 1; Row < ARRAY_SIZE (mCodeTable) - 2; Row++) {
    if (MilliCelsius < mCodeTable[Row + 1].MilliCelsius) {
      break;
    }
  }

  return mCodeTable[Row].Code +
         ((mCodeTable[Row + 1].Code - mCodeTable[Row].Code) *
          (UINT32)(MilliCelsius - mCodeTable[Row].MilliCelsius)) /
         (UINT32)(mCodeTable[Row + 1].MilliCelsius - mCodeTable[Row].MilliCelsius);
}

INT32
EFIAPI
Rk3576TsadcGetTrimMilliCelsius (
  IN UINT32  Channel
  )
{
  UINT8   Bytes[2];
  UINT32  Trim;
  INT32   Delta;

  if (Channel >= RK3576_TSADC_CHANNEL_COUNT) {
    return 0;
  }

  Bytes[0] = 0;
  Bytes[1] = 0;
  OtpRead (mTrimOtpOffset[Channel], sizeof (Bytes), Bytes);
  Trim = ((UINT32)Bytes[0] | ((UINT32)Bytes[1] << 8)) & 0x3FF;   /* bits = <0 10> */

  /* [L]:1641-1647: no trim when the cell reads 0 */
  if (Trim == 0) {
    return 0;
  }

  /* trim_code = trim - temp_to_code(30 C) = trim - 290; trim_temp = 923 * that */
  Delta = (INT32)Trim - (INT32)TsadcTempToCode (TSADC_TRIM_BASE_MC);
  if ((Delta >= TSADC_TRIM_MAX_DELTA) || (Delta <= -TSADC_TRIM_MAX_DELTA)) {
    DEBUG ((
      DEBUG_WARN,
      "Rk3576Tsadc: ch%u OTP trim 0x%x is %d codes from the 30 C code, ignoring it\n",
      Channel,
      Trim,
      Delta
      ));
    return 0;
  }

  return TSADC_TRIM_SLOPE * Delta;
}

/*
 * Reads only the CRU, which is always clocked and never in reset while the
 * firmware runs.  TRUE when both TSADC resets are deasserted and both gates
 * open.
 */
STATIC
BOOLEAN
TsadcCruReady (
  OUT UINT32  *Gate   OPTIONAL,
  OUT UINT32  *Reset  OPTIONAL
  )
{
  UINT32  G;
  UINT32  R;

  G = MmioRead32 (CRU_CLKGATE_CON (TSADC_CLKGATE_CON));
  R = MmioRead32 (CRU_SOFTRST_CON (TSADC_SOFTRST_CON));
  if (Gate != NULL) {
    *Gate = G;
  }

  if (Reset != NULL) {
    *Reset = R;
  }

  return ((G & TSADC_CRU_BITS) == 0) &&
         ((R & ((1U << SRST_P_TSADC_BIT) | (1U << SRST_TSADC_BIT))) == 0);
}

BOOLEAN
EFIAPI
Rk3576TsadcIsRunning (
  VOID
  )
{
  if (!TsadcCruReady (NULL, NULL)) {
    return FALSE;
  }

  return (MmioRead32 (TSADC_REG (TSADC_AUTO_CON)) & TSADC_AUTO_EN) != 0;
}

STATIC
VOID
TsadcLogReadings (
  VOID
  )
{
  UINT32      Channel;
  UINT32      Code;
  INT32       MilliC;
  INT32       Trim;
  EFI_STATUS  Status;

  for (Channel = 0; Channel < RK3576_TSADC_CHANNEL_COUNT; Channel++) {
    Code   = MmioRead32 (TSADC_REG (TSADC_DATA (Channel)));
    Trim   = Rk3576TsadcGetTrimMilliCelsius (Channel);
    Status = Rk3576TsadcCodeToMilliCelsius (Code, &MilliC);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "Rk3576Tsadc: ch%u code 0x%03x (no valid reading), trim %d mC\n", Channel, Code, Trim));
    } else {
      DEBUG ((
        DEBUG_INFO,
        "Rk3576Tsadc: ch%u code 0x%03x = %d mC untrimmed, trim %d mC, %d mC trimmed\n",
        Channel,
        Code,
        MilliC,
        Trim,
        MilliC - Trim
        ));
    }
  }
}

EFI_STATUS
EFIAPI
Rk3576TsadcInit (
  VOID
  )
{
  UINT32  Gate;
  UINT32  Reset;
  UINT32  Div;
  UINT32  Channel;
  UINT32  GpioEn;
  UINT32  CruEn;
  UINT32  AutoCon;
  UINTN   Wait;

  if (!FixedPcdGetBool (PcdTsadcEnable)) {
    return EFI_UNSUPPORTED;
  }

  /*
   * Reset state: read, never written.  Both bits reset to 0 ([R]
   * SOFTRST_CON13) and nothing in TF-A or the SPL of this boot chain writes
   * them; if something did, leave the block alone.
   */
  TsadcCruReady (&Gate, &Reset);
  Div = MmioRead32 (CRU_CLKSEL_CON (TSADC_CLKSEL_CON)) & CLK_TSADC_DIV_MASK;
  DEBUG ((
    DEBUG_INFO,
    "Rk3576Tsadc: CRU GATE_CON13 0x%08x SOFTRST_CON13 0x%08x CLKSEL_CON59 div 0x%02x\n",
    Gate,
    Reset,
    Div
    ));

  if ((Reset & ((1U << SRST_P_TSADC_BIT) | (1U << SRST_TSADC_BIT))) != 0) {
    DEBUG ((DEBUG_WARN, "Rk3576Tsadc: held in reset by an earlier stage, not deasserting it; read-out off\n"));
    return EFI_NOT_READY;
  }

  /* Open PCLK_TSADC and CLK_TSADC, what devm_clk_get_enabled does ([L]:1730-1738) */
  if ((Gate & TSADC_CRU_BITS) != 0) {
    MmioWrite32 (CRU_CLKGATE_CON (TSADC_CLKGATE_CON), HIWORD (TSADC_CRU_BITS, 0));
  }

  /* 2 MHz, [D]:2759-2760 assigned-clock-rates */
  if (Div != CLK_TSADC_DIV_2MHZ) {
    MmioWrite32 (CRU_CLKSEL_CON (TSADC_CLKSEL_CON), HIWORD (CLK_TSADC_DIV_MASK, CLK_TSADC_DIV_2MHZ));
  }

  GpioEn  = MmioRead32 (TSADC_REG (TSADC_GPIO_EN));
  CruEn   = MmioRead32 (TSADC_REG (TSADC_CRU_EN));
  AutoCon = MmioRead32 (TSADC_REG (TSADC_AUTO_CON));
  DEBUG ((
    DEBUG_INFO,
    "Rk3576Tsadc: AUTO_CON 0x%08x AUTO_SRC 0x%08x GPIO_EN 0x%08x CRU_EN 0x%08x HT_INT_EN 0x%08x\n",
    AutoCon,
    MmioRead32 (TSADC_REG (TSADC_AUTO_SRC)),
    GpioEn,
    CruEn,
    MmioRead32 (TSADC_REG (TSADC_HT_INT_EN))
    ));

  if (((GpioEn | CruEn) & 0xFFFF) != 0) {
    DEBUG ((DEBUG_WARN, "Rk3576Tsadc: a TSHUT output is already enabled by an earlier stage; not touching the TSADC\n"));
    return EFI_ACCESS_DENIED;
  }

  if ((AutoCon & TSADC_AUTO_EN) != 0) {
    DEBUG ((DEBUG_INFO, "Rk3576Tsadc: already sampling, left as found\n"));
    TsadcLogReadings ();
    return EFI_SUCCESS;
  }

  /* rk_tsadcv8_initialize, [L]:850-856 (the polarity write at :857-863 is skipped, see top) */
  MmioWrite32 (TSADC_REG (TSADC_AUTO_PERIOD), TSADC_AUTO_PERIOD_TIME);
  MmioWrite32 (TSADC_REG (TSADC_AUTO_PERIOD_HT), TSADC_AUTO_PERIOD_TIME);
  MmioWrite32 (TSADC_REG (TSADC_HIGH_INT_DEBOUNCE), TSADC_DEBOUNCE_COUNT);
  MmioWrite32 (TSADC_REG (TSADC_HIGH_TSHUT_DEBOUNCE), TSADC_DEBOUNCE_COUNT);

  /*
   * rk_tsadcv3_tshut_temp, [L]:1050 and :1053-1054, per channel: threshold
   * first, then the channel's AUTO_SRC bit.  The threshold is the unreachable
   * 0x3FF, not the 120 C code.  rk_tsadcv4_tshut_mode ([L]:1088-1089), which
   * mainline calls just before, is not: it is the GPIO_EN / CRU_EN write.
   */
  for (Channel = 0; Channel < RK3576_TSADC_CHANNEL_COUNT; Channel++) {
    MmioWrite32 (TSADC_REG (TSADC_COMP_SHUT (Channel)), TSADC_COMP_UNREACHABLE);
  }

  MmioWrite32 (
    TSADC_REG (TSADC_AUTO_SRC),
    HIWORD ((1U << RK3576_TSADC_CHANNEL_COUNT) - 1, (1U << RK3576_TSADC_CHANNEL_COUNT) - 1)
    );

  /* rk_tsadcv4_control (enable), [L]:933-936 */
  MmioWrite32 (TSADC_REG (TSADC_AUTO_CON), HIWORD (TSADC_AUTO_EN, TSADC_AUTO_EN));

  /* First round: one 2.5 ms period plus six conversions; allow 50 ms. */
  for (Wait = 0; Wait < 50; Wait++) {
    if (MmioRead32 (TSADC_REG (TSADC_DATA (RK3576_TSADC_CHANNEL_COUNT - 1))) != 0) {
      break;
    }

    MicroSecondDelay (1000);
  }

  DEBUG ((
    DEBUG_INFO,
    "Rk3576Tsadc: sampling, AUTO_CON 0x%08x, first round after %u ms\n",
    MmioRead32 (TSADC_REG (TSADC_AUTO_CON)),
    (UINT32)Wait
    ));
  TsadcLogReadings ();

  return EFI_SUCCESS;
}
