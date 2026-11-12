/** @file
 *
 *  RK3576 PWM ("PWM v4") output library.
 *
 *  RK3576 does not use the PWM block that Silicon/Rockchip/Library/PWMLib
 *  drives.  That library is the RK3588 (v1-v3) IP with four channels sharing
 *  one register block and RK3588 base addresses; on RK3576 each channel is its
 *  own 4 KiB instance with a different register layout.  See Rk3576PwmLib.c
 *  for the sources of every register used here.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_PWM_LIB_H_
#define RK3576_PWM_LIB_H_

#include <Uefi/UefiBaseType.h>

/**
  Start one PWM v4 channel in continuous mode.

  Only controller 2 (PWM2, the eight-channel block at 0x2ADE0000) is
  implemented.  The other controllers have their own clock gates, and nothing
  on the supported boards uses them yet.

  @param[in] Controller  PWM controller number, as in the device tree node name
                         (pwm2_8ch_3 is controller 2, channel 3).
  @param[in] Channel     Channel within the controller.
  @param[in] PeriodNs    Period in nanoseconds.
  @param[in] DutyNs      Active time per period in nanoseconds, <= PeriodNs.
  @param[in] Inverted    FALSE: the output is high for DutyNs and low when the
                         channel is idle (PWM_POLARITY_NORMAL in Linux).
                         TRUE: the opposite.

  @retval EFI_SUCCESS            The channel is running.
  @retval EFI_UNSUPPORTED        Controller or channel not implemented.
  @retval EFI_INVALID_PARAMETER  PeriodNs is 0, DutyNs > PeriodNs, or the
                                 period does not fit the counter.
**/
EFI_STATUS
EFIAPI
Rk3576PwmStart (
  IN UINT32   Controller,
  IN UINT32   Channel,
  IN UINT32   PeriodNs,
  IN UINT32   DutyNs,
  IN BOOLEAN  Inverted
  );

#endif // RK3576_PWM_LIB_H_
