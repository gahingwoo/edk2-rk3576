/** @file
 *
 *  The one-shot request that makes the next boot start from an empty UEFI
 *  variable store, as a freshly flashed board does.
 *
 *  FullReset (Silicon/Rockchip/Applications/FullReset) sets it and resets.
 *  RkFvbDxe looks for it in the raw store on the next boot, before the
 *  variable driver runs, and if it is there erases the whole store region
 *  (variables, FTW working and FTW spare) and formats it.  Nothing outside
 *  that region is touched.  The request is erased with everything else, so
 *  it fires once.
 *
 *  It is a variable, not a register or a DRAM flag, so it survives a power
 *  cut between the request and the boot that carries it out, and it works the
 *  same on SPI NOR and on SD/eMMC.  It is checked by name, GUID, size and
 *  value, so nothing else can trigger it by accident.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef FULL_RESET_REQUEST_H_
#define FULL_RESET_REQUEST_H_

#define FULL_RESET_REQUEST_GUID \
  { 0xa4eceb1a, 0xfa2a, 0x4c41, { 0x8d, 0x68, 0xb4, 0xce, 0xe0, 0x60, 0xa2, 0xd9 } }

#define FULL_RESET_REQUEST_NAME        L"FullResetRequest"
#define FULL_RESET_REQUEST_ATTRIBUTES  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS)
#define FULL_RESET_REQUEST_VALUE       SIGNATURE_32 ('W', 'I', 'P', 'E')

extern EFI_GUID  gRockchipFullResetRequestGuid;

#endif // FULL_RESET_REQUEST_H_
