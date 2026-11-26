/** @file
 *
 *  Where this boot's firmware clock came from, for the setup About page.
 *
 *  A volatile, boot-services-only UINT8 variable.  Two writers:
 *
 *    - Hym8563RealTimeClockLib, when it checks the RTC chip at boot (RTC or
 *      RTC_LOST) and after a SetTime() it wrote to the chip (RTC).
 *    - NetworkTimeSyncDxe, after its SNTP answer set the clock or found it
 *      right already (NETWORK).  This comes after the RTC library's write,
 *      so it is the last word.
 *
 *  No variable means nobody knows: a board without the chip, or a build
 *  with the plain virtual RTC.  Nothing is stored across a reset.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ROCKCHIP_TIME_SOURCE_H_
#define ROCKCHIP_TIME_SOURCE_H_

#define ROCKCHIP_TIME_SOURCE_GUID \
  { 0x1facb4df, 0xd5ea, 0x4143, { 0x82, 0x5d, 0x63, 0xe3, 0x8c, 0x1c, 0x96, 0x24 } }

#define ROCKCHIP_TIME_SOURCE_NAME        L"TimeSource"
#define ROCKCHIP_TIME_SOURCE_ATTRIBUTES  EFI_VARIABLE_BOOTSERVICE_ACCESS

#define ROCKCHIP_TIME_SOURCE_RTC       1   // the RTC chip holds a valid time
#define ROCKCHIP_TIME_SOURCE_RTC_LOST  2   // the RTC chip lost power: not set
#define ROCKCHIP_TIME_SOURCE_NETWORK   3   // set from NTP this boot

extern EFI_GUID  gRockchipTimeSourceGuid;

#endif // ROCKCHIP_TIME_SOURCE_H_
