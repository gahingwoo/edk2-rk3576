/** @file
 *
 *  RK3576 TSADC (temperature sensor ADC): read-out only.
 *
 *  Starts the TSADC sampling in auto mode the way mainline Linux does for
 *  rk3576, minus everything that can reset or shut down the machine: the
 *  TSHUT outputs to the CRU and to the PMIC GPIO are never enabled, no
 *  interrupt is unmasked and no reset is touched.  See Rk3576TsadcLib.c.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_TSADC_LIB_H_
#define RK3576_TSADC_LIB_H_

#include <Uefi/UefiBaseType.h>

#define RK3576_TSADC_BASE           0x2AE70000UL
#define RK3576_TSADC_CHANNEL_COUNT  6

/*
 * Code to temperature origin used by the ACPI _TMP methods: the bias a
 * channel's AML integer carries is this minus the channel's trim, in
 * millidegrees (233150 mK is -40 C, the first row of the code table).
 */
#define RK3576_TSADC_BIAS_MK  233150

/**
  Start the TSADC in auto mode, read-out only.

  Does nothing unless PcdTsadcEnable is TRUE.  Refuses (and writes nothing to
  the TSADC) if an earlier stage holds the block in reset, or has already
  routed a TSHUT output, or has the block already running.

  @retval EFI_SUCCESS       Sampling started, or was already running.
  @retval EFI_UNSUPPORTED   PcdTsadcEnable is FALSE.
  @retval EFI_NOT_READY     The TSADC is held in reset; not touched.
  @retval EFI_ACCESS_DENIED A TSHUT output is already enabled; not touched.
**/
EFI_STATUS
EFIAPI
Rk3576TsadcInit (
  VOID
  );

/**
  Whether the TSADC is out of reset, clocked and sampling in auto mode.

  Reads the CRU before it reads the TSADC, so it never reads a block that is
  in reset or has its APB clock gated.
**/
BOOLEAN
EFIAPI
Rk3576TsadcIsRunning (
  VOID
  );

/**
  The trim temperature mainline subtracts from a channel's reading, in
  millidegrees Celsius, read from the non-secure OTP.

  0 when the OTP word is blank (mainline then applies no trim) or when the
  value is implausible (see Rk3576TsadcLib.c).
**/
INT32
EFIAPI
Rk3576TsadcGetTrimMilliCelsius (
  IN UINT32  Channel
  );

/**
  Convert a raw TSADC data word to millidegrees Celsius with mainline's
  rk3576 table, before trim.

  @retval EFI_SUCCESS     *MilliCelsius holds the temperature.
  @retval EFI_NOT_READY   The code is below the table (no sample yet, or bad).
**/
EFI_STATUS
EFIAPI
Rk3576TsadcCodeToMilliCelsius (
  IN  UINT32  Code,
  OUT INT32   *MilliCelsius
  );

#endif // RK3576_TSADC_LIB_H_
