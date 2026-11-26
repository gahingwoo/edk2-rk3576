/** @file
 *
 *  Rock4dAudio.c: the Radxa ROCK 4D's ES8388 headphone codec on SAI1, set
 *  up for an operating system that has no clock, power-domain or pin driver
 *  of its own (Windows with the woa-rk3576 audio drivers).
 *
 *  The same register writes as the TaishanPi 3M's Tspi3mAudio.c (the same
 *  SoC block, the same codec, the same SAI1 M0 and I2C3 M0 pin groups); only
 *  the data-out pin differs, and this board has no amplifier enable and no
 *  jack-detect GPIO.
 *
 *  Only with PcdAnalogAudioPreset.  The DSDT then publishes \_SB.SAI1 and the
 *  codec on \_SB.I2C3 (Sai1.asl, Es8388.asl); SAI1 reports itself present
 *  only if CLKSEL_CON46 holds what Rock4dAnalogAudioInit writes there.
 *
 *  Wiring, from the schematic (ROCK 4D V1.12 page 16 "Audio-CODEC", page 14
 *  "RK3576-GPIO VCCIO4/5") and the vendor kernel tree (rk3576-rock-4d.dts:
 *  157-184 es8388-sound, :457-464 &sai1, :519-535 &i2c3 es8388@10):
 *    sai1m0_mclk GPIO4 PA2, sai1m0_sclk GPIO4 PA3, sai1m0_lrck GPIO4 PA5,
 *    sai1m0_sdo0 GPIO4 PA7, sai1m0_sdi0 GPIO4 PB3 (all function 1,
 *    rk3576-pinctrl.dtsi); i2c3m0 SDA GPIO4 PB4, SCL GPIO4 PB5 (function 11),
 *    nets I2C3_SCL/SDA_M0_Audio to the ES8388 (U14) CCLK/CDATA.
 *    LOUT1/ROUT1 drive the headphone jack J18 directly (HPOUT_L/R, no
 *    amplifier).  LOUT2/ROUT2 go to the speaker amplifiers on page 24
 *    "Audio SPK", which are not fitted (every part there is NC), so the
 *    vendor tree's spk-con-gpio (GPIO2_C6) switches nothing; it is left
 *    alone.  The jack's detect is SARADC_VIN3_HP_HOOK, an analog input
 *    (io-channels = <&saradc 3> in the vendor tree), not a GPIO.
 *
 *  What is written, all from mainline clk-rk3576.c and pm-domains.c:
 *
 *  PMU (0x27380000), PD_AUDIO: PWR_CON 0x210 bit 8 (0 = on), repair status
 *  0x570 bit 8 (1 = on), bus idle request 0x114 bit 0, ack 0x120 bit 16,
 *  idle 0x128 bit 16.  Written only if the domain is off or idle.
 *
 *  CRU (0x27200000):
 *    CLKSEL_CON15 0x33C [1:0]  clk_audio_frac_1 source: 0 GPLL, 1 CPLL
 *    CLKSEL_CON14 0x338        clk_audio_frac_1 = (num << 16) | den
 *    GATE_CON01   0x804 bit 11 clk_audio_frac_1
 *    CLKSEL_CON46 0x3B8        [10:8] mclk_sai1_src = 2 (clk_audio_frac_1),
 *                              [7:0] divide by 1, [11] mclk_sai1 = src
 *    GATE_CON07   0x81C bit 1  hclk_audio_root
 *    GATE_CON08   0x820 bits 4, 5, 6  mclk_sai1_src, mclk_sai1, hclk_sai1
 *    GATE_CON09   0x824 bit 13 clk_sai1_mclkout
 *    GATE_CON12   0x830 bits 2, 14  pclk_i2c3, clk_i2c3
 *    GATE_CON18   0x848 bits 5, 6   pclk_gpio4, dbclk_gpio4
 *    GATE_CON19   0x84C bit 1  aclk_dmac0 (SAI1's DMA, dmac0 requests 2, 3)
 *  IOC (0x26040000): VCCIO_IOC_MISC_CON0 0x6400 bit 1 = 0, mclk_sai1_to_io
 *  (a set-to-disable gate).
 *
 *  clk_audio_frac_1 is used, not clk_audio_frac_0, so that the HDMI audio
 *  preset (DwHdmiQpAudio.c, frac_0 for SAI6) and this one never share a
 *  divider.  As there, no PLL is reprogrammed and the fraction is used only
 *  if it is exact.
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>
#include <Library/TimerLib.h>

#define AUD_CRU_BASE              0x27200000UL
#define AUD_CRU_CPLL_CON0         (AUD_CRU_BASE + 0x01A0)   // PLL_CON(104)
#define AUD_CRU_GPLL_CON0         (AUD_CRU_BASE + 0x01C0)   // PLL_CON(112)
#define AUD_CRU_MODE_CON00        (AUD_CRU_BASE + 0x0280)
#define AUD_CRU_GPLL_MODE_SHIFT   2
#define AUD_CRU_CPLL_MODE_SHIFT   8
#define AUD_CRU_CLKSEL_CON14      (AUD_CRU_BASE + 0x0338)
#define AUD_CRU_CLKSEL_CON15      (AUD_CRU_BASE + 0x033C)
#define AUD_CRU_CLKSEL_CON46      (AUD_CRU_BASE + 0x03B8)
#define AUD_CRU_GATE_CON01        (AUD_CRU_BASE + 0x0804)
#define AUD_CRU_GATE_CON07        (AUD_CRU_BASE + 0x081C)
#define AUD_CRU_GATE_CON08        (AUD_CRU_BASE + 0x0820)
#define AUD_CRU_GATE_CON09        (AUD_CRU_BASE + 0x0824)
#define AUD_CRU_GATE_CON12        (AUD_CRU_BASE + 0x0830)
#define AUD_CRU_GATE_CON18        (AUD_CRU_BASE + 0x0848)
#define AUD_CRU_GATE_CON19        (AUD_CRU_BASE + 0x084C)

#define AUD_IOC_MISC_CON0         0x26046400UL

#define AUD_PMU_BASE              0x27380000UL
#define AUD_PMU_PWR_CON           (AUD_PMU_BASE + 0x0210)
#define AUD_PMU_IDLE_REQ          (AUD_PMU_BASE + 0x0114)
#define AUD_PMU_IDLE_ACK          (AUD_PMU_BASE + 0x0120)
#define AUD_PMU_IDLE_STS          (AUD_PMU_BASE + 0x0128)
#define AUD_PMU_REPAIR_STATUS     (AUD_PMU_BASE + 0x0570)
#define AUD_PD_AUDIO_PWR          BIT8
#define AUD_PD_AUDIO_REQ          BIT0
#define AUD_PD_AUDIO_IDLE         BIT16

#define AUD_OSC_HZ                24000000U
#define AUD_MCLK_HZ               (256U * 48000U)            // 12.288 MHz
#define AUD_FRAC1_SEL_GPLL        0
#define AUD_FRAC1_SEL_CPLL        1
#define AUD_SAI1_SRC_SEL_FRAC1    2                          // audio_frac_int_p[2]

#define AUD_HIWORD(Mask, Val)     (((UINT32)(Mask) << 16) | (UINT32)(Val))

/**
  Rate of an RK3576 PLL from its registers, as DwHdmiQpAudio.c reads it:
  Fout = 24 MHz * (M + K / 65536) / (P * 2^S).  0 if bypassed or nonsense.
**/
STATIC
UINT32
Rock4dAudioPllRate (
  IN UINTN  Con0,
  IN UINTN  ModeShift
  )
{
  UINT32  Con0Val;
  UINT32  Con1Val;
  UINT32  M;
  UINT32  P;
  UINT32  S;
  INT16   K;
  UINT64  Rate;

  if (((MmioRead32 (AUD_CRU_MODE_CON00) >> ModeShift) & 0x3) != 1) {
    return 0;
  }

  Con0Val = MmioRead32 (Con0);
  Con1Val = MmioRead32 (Con0 + 4);
  K       = (INT16)(MmioRead32 (Con0 + 8) & 0xFFFF);

  if ((Con0Val & BIT15) != 0) {
    return 0;
  }

  M = Con0Val & 0x3FF;
  P = Con1Val & 0x3F;
  S = (Con1Val >> 6) & 0x7;
  if ((M == 0) || (P == 0)) {
    return 0;
  }

  Rate = MultU64x32 ((UINT64)AUD_OSC_HZ, M);
  if (K != 0) {
    Rate += DivS64x64Remainder (MultS64x64 ((INT64)AUD_OSC_HZ, K), 65536, NULL);
  }

  return (UINT32)DivU64x32 (Rate, P << S);
}

/**
  An exact Num/Den with Parent * Num / Den = Target, both 16-bit, and the
  parent at least 20 times the output (Rockchip's fractional divider rule).
**/
STATIC
BOOLEAN
Rock4dAudioExactFraction (
  IN  UINT32  Parent,
  IN  UINT32  Target,
  OUT UINT32  *Num,
  OUT UINT32  *Den
  )
{
  UINT32  A;
  UINT32  B;
  UINT32  T;

  if ((Parent == 0) || (Parent < 20U * Target)) {
    return FALSE;
  }

  A = Parent;
  B = Target;
  while (B != 0) {
    T = A % B;
    A = B;
    B = T;
  }

  *Num = Target / A;
  *Den = Parent / A;
  return (*Num <= 0xFFFF) && (*Den <= 0xFFFF);
}

STATIC
BOOLEAN
Rock4dAudioWait (
  IN UINTN   Reg,
  IN UINT32  Mask,
  IN UINT32  Want
  )
{
  UINTN  Timeout;

  for (Timeout = 0; Timeout < 1000; Timeout++) {
    if ((MmioRead32 (Reg) & Mask) == Want) {
      return TRUE;
    }
    MicroSecondDelay (10);
  }

  return FALSE;
}

STATIC
VOID
Rock4dAudioPowerDomain (
  VOID
  )
{
  DEBUG ((DEBUG_INFO,
          "Rock4dAudio: PD_AUDIO before: PWR_CON 0x%08x REPAIR 0x%08x IDLE_REQ 0x%08x ACK 0x%08x IDLE 0x%08x\n",
          MmioRead32 (AUD_PMU_PWR_CON), MmioRead32 (AUD_PMU_REPAIR_STATUS),
          MmioRead32 (AUD_PMU_IDLE_REQ), MmioRead32 (AUD_PMU_IDLE_ACK),
          MmioRead32 (AUD_PMU_IDLE_STS)));

  if ((MmioRead32 (AUD_PMU_REPAIR_STATUS) & AUD_PD_AUDIO_PWR) == 0) {
    MmioWrite32 (AUD_PMU_PWR_CON, AUD_HIWORD (AUD_PD_AUDIO_PWR, 0));
    if (!Rock4dAudioWait (AUD_PMU_REPAIR_STATUS, AUD_PD_AUDIO_PWR, AUD_PD_AUDIO_PWR)) {
      DEBUG ((DEBUG_WARN, "Rock4dAudio: PD_AUDIO did not power on\n"));
    }
  }

  if (((MmioRead32 (AUD_PMU_IDLE_REQ) & AUD_PD_AUDIO_REQ) != 0) ||
      ((MmioRead32 (AUD_PMU_IDLE_STS) & AUD_PD_AUDIO_IDLE) != 0))
  {
    MmioWrite32 (AUD_PMU_IDLE_REQ, AUD_HIWORD (AUD_PD_AUDIO_REQ, 0));
    if (!Rock4dAudioWait (AUD_PMU_IDLE_ACK, AUD_PD_AUDIO_IDLE, 0) ||
        !Rock4dAudioWait (AUD_PMU_IDLE_STS, AUD_PD_AUDIO_IDLE, 0))
    {
      DEBUG ((DEBUG_WARN, "Rock4dAudio: PD_AUDIO bus did not leave idle\n"));
    }
  }
}

STATIC
BOOLEAN
Rock4dAudioSetMclk (
  VOID
  )
{
  UINT32  Gpll;
  UINT32  Cpll;
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Num;
  UINT32  Den;

  Gpll = Rock4dAudioPllRate (AUD_CRU_GPLL_CON0, AUD_CRU_GPLL_MODE_SHIFT);
  Cpll = Rock4dAudioPllRate (AUD_CRU_CPLL_CON0, AUD_CRU_CPLL_MODE_SHIFT);

  if (Rock4dAudioExactFraction (Gpll, AUD_MCLK_HZ, &Num, &Den)) {
    Sel    = AUD_FRAC1_SEL_GPLL;
    Parent = Gpll;
  } else if (Rock4dAudioExactFraction (Cpll, AUD_MCLK_HZ, &Num, &Den)) {
    Sel    = AUD_FRAC1_SEL_CPLL;
    Parent = Cpll;
  } else {
    DEBUG ((DEBUG_WARN, "Rock4dAudio: no exact 12.288 MHz from GPLL %u or CPLL %u Hz, no audio\n",
            Gpll, Cpll));
    return FALSE;
  }

  MmioWrite32 (AUD_CRU_GATE_CON07, AUD_HIWORD (BIT1, 0));
  MmioWrite32 (AUD_CRU_CLKSEL_CON15, AUD_HIWORD (0x3, Sel));
  MmioWrite32 (AUD_CRU_CLKSEL_CON14, (Num << 16) | Den);
  MmioWrite32 (AUD_CRU_GATE_CON01, AUD_HIWORD (BIT11, 0));

  // [11] mclk_sai1 = mclk_sai1_src, [10:8] src = audio_frac_1, [7:0] div 1.
  MmioWrite32 (AUD_CRU_CLKSEL_CON46, AUD_HIWORD (0xFFF, AUD_SAI1_SRC_SEL_FRAC1 << 8));
  MmioWrite32 (AUD_CRU_GATE_CON08, AUD_HIWORD (BIT4 | BIT5 | BIT6, 0));
  MmioWrite32 (AUD_CRU_GATE_CON09, AUD_HIWORD (BIT13, 0));
  MmioWrite32 (AUD_IOC_MISC_CON0, AUD_HIWORD (BIT1, 0));

  DEBUG ((DEBUG_INFO, "Rock4dAudio: mclk_sai1 = %u Hz * %u / %u (%a), out on SAI1_MCLK_M0\n",
          Parent, Num, Den, (Sel == AUD_FRAC1_SEL_GPLL) ? "GPLL" : "CPLL"));
  return TRUE;
}

VOID
Rock4dAnalogAudioInit (
  VOID
  )
{
  Rock4dAudioPowerDomain ();

  // The bus clocks the OS drivers need and nothing else turns on for them.
  MmioWrite32 (AUD_CRU_GATE_CON12, AUD_HIWORD (BIT2 | BIT14, 0));
  MmioWrite32 (AUD_CRU_GATE_CON18, AUD_HIWORD (BIT5 | BIT6, 0));
  MmioWrite32 (AUD_CRU_GATE_CON19, AUD_HIWORD (BIT1, 0));

  if (!Rock4dAudioSetMclk ()) {
    return;
  }

  // SAI1 M0: MCLK, SCLK, LRCK, SDO0, SDI0.
  GpioPinSetFunction (4, GPIO_PIN_PA2, 1);
  GpioPinSetFunction (4, GPIO_PIN_PA3, 1);
  GpioPinSetFunction (4, GPIO_PIN_PA5, 1);
  GpioPinSetFunction (4, GPIO_PIN_PA7, 1);
  GpioPinSetFunction (4, GPIO_PIN_PB3, 1);

  // I2C3 M0: SDA PB4, SCL PB5.
  GpioPinSetFunction (4, GPIO_PIN_PB4, 11);
  GpioPinSetFunction (4, GPIO_PIN_PB5, 11);

  DEBUG ((DEBUG_INFO, "Rock4dAudio: SAI1 and I2C3 pins set\n"));
}
