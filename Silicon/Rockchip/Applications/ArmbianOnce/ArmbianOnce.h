/** @file
 *
 *  The request record ArmbianOnce leaves on the eMMC for the SPL.
 *
 *  The SPL side is uboot/v2026.10/0001-rockchip-spl-boot-another-U-Boot-FIT-
 *  once-on-request.patch (arch/arm/mach-rockchip/spl-armbian-once.c).  The
 *  two must agree on everything in this file.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ARMBIAN_ONCE_H_
#define ARMBIAN_ONCE_H_

//
// eMMC user area, 512-byte sectors.
//
#define ARMBIAN_ONCE_FIT_LBA       0x4000   // where every Rockchip SPL looks for its FIT
#define ARMBIAN_ONCE_REQUEST_LBA   0x3FFF   // the sector just below it
#define ARMBIAN_ONCE_SLOT_LBA      0x10000  // 32 MiB: a copied U-Boot FIT
#define ARMBIAN_ONCE_SLOT_SECTORS  0x8000   // 16 MiB, to 48 MiB

#define ARMBIAN_ONCE_MAGIC    "ARMBONCE"
#define ARMBIAN_ONCE_VERSION  1

#define ARMBIAN_ONCE_TARGET_SD         1    // the FIT at sector 0x4000 of the SD card
#define ARMBIAN_ONCE_TARGET_EMMC_SLOT  2    // the copy at ARMBIAN_ONCE_SLOT_LBA
#define ARMBIAN_ONCE_TARGET_EMMC_FIT   3    // a U-Boot FIT at sector 0x4000 of the eMMC

//
// The SPL prints this string, so it is in every SPL that can do it.
//
#define ARMBIAN_ONCE_SPL_TAG  "ArmbianOnce/1"

#pragma pack(1)
typedef struct {
  CHAR8     Magic[8];
  UINT32    Version;
  UINT32    Target;
  UINT32    SlotSector;     // ARMBIAN_ONCE_SLOT_LBA for TARGET_EMMC_SLOT, else 0
  UINT32    Reserved;
  UINT8     Pad[484];
  UINT32    Crc32;          // CRC-32 (IEEE) of the 508 bytes before it
} ARMBIAN_ONCE_REQUEST;
#pragma pack()

#endif // ARMBIAN_ONCE_H_
