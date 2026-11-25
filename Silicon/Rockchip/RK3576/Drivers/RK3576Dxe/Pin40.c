/** @file
 *
 *  Pin40.c -- the 40-pin header for an operating system with no pin or clock
 *  driver of its own (Windows with the woa-rk3576 rk3xuart, rk3xgpio,
 *  rk3xi2c and rk3xspi drivers).  Runs only when a setup option asks for it
 *  ("40-pin UART", PcdPin40Uart; "40-pin GPIO/I2C/SPI for apps",
 *  PcdPin40RhProxy), the OS gets ACPI, and the board describes its header
 *  (PcdPin40Supported, PlatformGetPin40Config).  RK3576AcpiPlatformDxe
 *  installs the matching SSDTs (OEM table IDs "RKUART40" and "RKRHPX")
 *  under the same options.  With both options off nothing here runs.
 *
 *  Clocks, all in CRU (0x27200000), from mainline clk-rk3576.c and
 *  rst-rk3576.c (Linux 7.3), for controller n:
 *
 *    UART2..11  sclk_uartN: CLKSEL_CON(59 + n), mux [10:8] (3 = xin24m),
 *               div [7:0]; pclk gate and reset share a bit, as do sclk
 *               gate and reset (tables below).  24 MHz gives exact 1.5
 *               Mbaud, 115200 at 0.16 %, and every standard rate below it
 *               within 0.2 %; the ACPI device says 24 MHz in _DSD.
 *    I2C1..9    pclk_i2cN GATE_CON12 bit n-1; clk_i2cN GATE_CON12 bit 11+n
 *               (n <= 4) or GATE_CON13 bit n-5; mux CLKSEL_CON57 [2(n-1)+1:
 *               2(n-1)] for n <= 8, CLKSEL_CON58 [1:0] for 9, 0 = gpll/6.
 *               I2c.asl's "rockchip,bclk" is 200 MHz.
 *    SPI0..4    pclk_spiN GATE_CON15 bit 13+n (n <= 2) or GATE_CON16 bit
 *               n-3; clk_spiN GATE_CON16 bit 2+n; mux CLKSEL_CON70 [14:13]
 *               for 0, CLKSEL_CON71 [2(n-1)+1:2(n-1)] for 1..4, 0 = gpll/6.
 *               rk3xspi assumes 200 MHz.
 *
 *  Gates are written open (0), resets released (0), with the upper 16 bits
 *  of each CRU register as the write mask.  The muxes are set to the values
 *  above whatever an earlier boot left.
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/GpioLib.h>
#include <Library/RockchipPlatformLib.h>
#include <VarStoreData.h>

#include "../../Include/Soc.h"
#include "Pin40.h"

#define CRU_UART_SCLK_XIN24M  3

/* {gate/reset register index, bit} pairs for UART0..UART11; UART1 is PMU. */
typedef struct {
  UINT8    Con;
  UINT8    Bit;
} CRU_BIT;

STATIC CONST CRU_BIT  mUartPclk[12] = {
  { 13, 10 }, { 0, 0 }, { 13, 11 }, { 13, 12 }, { 13, 13 }, { 13, 14 },
  { 13, 15 }, { 14, 0  }, { 14, 1  }, { 14, 2  }, { 14, 3  }, { 14, 4 }
};
STATIC CONST CRU_BIT  mUartSclk[12] = {
  { 14, 5  }, { 0, 0 }, { 14, 6  }, { 14, 9  }, { 14, 12 }, { 14, 15 },
  { 15, 2  }, { 15, 5 }, { 15, 8  }, { 15, 9  }, { 15, 10 }, { 15, 11 }
};

STATIC
VOID
CruBitClear (
  IN UINTN  Reg,
  IN UINT8  Bit
  )
{
  MmioWrite32 (Reg, (BIT0 << Bit) << 16);
}

STATIC
VOID
Pin40UartClock (
  IN UINT8  Uart
  )
{
  if ((Uart < 2) || (Uart > 11)) {
    /* UART0 is the console; UART1 sits in the PMU CRU and is not handled */
    DEBUG ((DEBUG_ERROR, "Pin40: UART%u not handled, skipped\n", Uart));
    return;
  }

  MmioWrite32 (CRU_CLKSEL_CON (59 + Uart), (0x7FFU << 16) | (CRU_UART_SCLK_XIN24M << 8) | 0);
  CruBitClear (CRU_CLKGATE_CON (mUartPclk[Uart].Con), mUartPclk[Uart].Bit);
  CruBitClear (CRU_CLKGATE_CON (mUartSclk[Uart].Con), mUartSclk[Uart].Bit);
  CruBitClear (CRU_SOFTRST_CON (mUartPclk[Uart].Con), mUartPclk[Uart].Bit);
  CruBitClear (CRU_SOFTRST_CON (mUartSclk[Uart].Con), mUartSclk[Uart].Bit);
}

STATIC
VOID
Pin40I2cClock (
  IN UINT8  I2c
  )
{
  UINT32  Shift;

  if ((I2c < 1) || (I2c > 9)) {
    /* I2C0 is in the PMU domain and is the board's, not the header's */
    DEBUG ((DEBUG_ERROR, "Pin40: I2C%u not handled, skipped\n", I2c));
    return;
  }

  CruBitClear (CRU_CLKGATE_CON (12), (UINT8)(I2c - 1));
  if (I2c <= 4) {
    CruBitClear (CRU_CLKGATE_CON (12), (UINT8)(11 + I2c));
  } else {
    CruBitClear (CRU_CLKGATE_CON (13), (UINT8)(I2c - 5));
  }

  if (I2c <= 8) {
    Shift = 2 * (I2c - 1);
    MmioWrite32 (CRU_CLKSEL_CON (57), (0x3U << (16 + Shift)) | (0U << Shift));
  } else {
    MmioWrite32 (CRU_CLKSEL_CON (58), (0x3U << 16) | 0U);
  }
}

STATIC
VOID
Pin40SpiClock (
  IN UINT8  Spi
  )
{
  UINT32  Shift;

  if (Spi > 4) {
    DEBUG ((DEBUG_ERROR, "Pin40: SPI%u not handled, skipped\n", Spi));
    return;
  }

  if (Spi <= 2) {
    CruBitClear (CRU_CLKGATE_CON (15), (UINT8)(13 + Spi));
  } else {
    CruBitClear (CRU_CLKGATE_CON (16), (UINT8)(Spi - 3));
  }
  CruBitClear (CRU_CLKGATE_CON (16), (UINT8)(2 + Spi));

  if (Spi == 0) {
    MmioWrite32 (CRU_CLKSEL_CON (70), (0x3U << (16 + 13)) | (0U << 13));
  } else {
    Shift = 2 * (Spi - 1);
    MmioWrite32 (CRU_CLKSEL_CON (71), (0x3U << (16 + Shift)) | (0U << Shift));
  }
}

STATIC
VOID
Pin40ApplyPins (
  IN CONST GPIO_IOMUX_CONFIG  *Pins,
  IN UINT32                   Count,
  IN BOOLEAN                  KeepPull
  )
{
  UINT32  Index;

  for (Index = 0; Index < Count; Index++) {
    GpioPinSetFunction (Pins[Index].Group, Pins[Index].Pin, Pins[Index].Function);
    if (!KeepPull) {
      GpioPinSetPull (Pins[Index].Group, Pins[Index].Pin, Pins[Index].Pull);
    }
  }
}

VOID
RK3576Pin40Init (
  VOID
  )
{
  CONST PIN40_CONFIG  *Cfg;
  BOOLEAN             Uart;
  BOOLEAN             RhProxy;
  UINT32              Index;

  if (!FixedPcdGetBool (PcdPin40Supported) ||
      ((PcdGet32 (PcdConfigTableMode) & CONFIG_TABLE_MODE_ACPI) == 0))
  {
    return;
  }

  Uart    = PcdGet8 (PcdPin40Uart) != 0;
  RhProxy = PcdGet8 (PcdPin40RhProxy) != 0;
  if (!Uart && !RhProxy) {
    return;
  }

  Cfg = PlatformGetPin40Config ();
  if (Cfg == NULL) {
    DEBUG ((DEBUG_ERROR, "Pin40: the board sets PcdPin40Supported but describes no header\n"));
    return;
  }

  if (Uart) {
    for (Index = 0; Index < Cfg->UartCount; Index++) {
      Pin40UartClock (Cfg->Uart[Index]);
    }
    Pin40ApplyPins (Cfg->UartPins, Cfg->UartPinCount, FALSE);
    DEBUG ((DEBUG_ERROR, "Pin40: %u UART(s) on the header, %u pins, sclk 24 MHz\n",
            Cfg->UartCount, Cfg->UartPinCount));
  }

  if (RhProxy) {
    for (Index = 0; Index < Cfg->I2cCount; Index++) {
      Pin40I2cClock (Cfg->I2c[Index]);
    }
    for (Index = 0; Index < Cfg->SpiCount; Index++) {
      Pin40SpiClock (Cfg->Spi[Index]);
    }
    Pin40ApplyPins (Cfg->BusPins, Cfg->BusPinCount, FALSE);
    /*
     * The GPIO pins get their GPIO function only.  Direction, level and pull
     * stay as they are: only the user knows what is wired to a pin
     * (Microsoft's RhProxy rules ask firmware not to change a pin's state).
     * With the UART option on, the UART pins are not GPIOs, and the RhProxy
     * SSDT leaves them out as well.
     */
    for (Index = 0; Index < Cfg->GpioPinCount; Index++) {
      CONST GPIO_IOMUX_CONFIG  *P = &Cfg->GpioPins[Index];
      UINT32                   U;
      BOOLEAN                  IsUartPin = FALSE;

      if (Uart) {
        for (U = 0; U < Cfg->UartPinCount; U++) {
          if ((Cfg->UartPins[U].Group == P->Group) && (Cfg->UartPins[U].Pin == P->Pin)) {
            IsUartPin = TRUE;
          }
        }
      }
      if (!IsUartPin) {
        GpioPinSetFunction (P->Group, P->Pin, 0);
      }
    }
    DEBUG ((DEBUG_ERROR, "Pin40: RhProxy on: %u I2C, %u SPI, %u bus pins, %u GPIO pins\n",
            Cfg->I2cCount, Cfg->SpiCount, Cfg->BusPinCount, Cfg->GpioPinCount));
  }
}
