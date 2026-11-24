/** @file
 *
 *  Finding a U-Boot for "Armbian (U-Boot)": shared by the ArmbianOnce
 *  application, which acts on it, and PlatformBootManagerLib, which shows
 *  the boot option only when there is something for it to boot.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ARMBIAN_ONCE_LIB_H_
#define ARMBIAN_ONCE_LIB_H_

#include <Uefi.h>
#include <ArmbianOnce.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DiskIo.h>

#define ARMBIAN_ONCE_SECTOR          512
#define ARMBIAN_ONCE_FIT_HEADER_MAX  SIZE_64KB

typedef enum {
  ArmbianOnceFitNone,
  ArmbianOnceFitFirmware,   // has /images/edk2: this firmware
  ArmbianOnceFitUBoot       // another FIT, with an arm64 U-Boot image in it
} ARMBIAN_ONCE_FIT_KIND;

typedef struct {
  EFI_HANDLE                  Handle;
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;
  EFI_BLOCK_IO_PROTOCOL       *BlockIo;
  EFI_DISK_IO_PROTOCOL        *DiskIo;
} ARMBIAN_ONCE_DISK;

/**
  TRUE if Handle is on the Rockchip MMC controller at BaseAddress.
**/
BOOLEAN
EFIAPI
ArmbianOnceIsOnController (
  IN EFI_HANDLE  Handle,
  IN UINT64      BaseAddress
  );

/**
  Open Handle as a whole disk with media in it: not a partition, not an
  eMMC boot or general purpose hardware partition.
**/
BOOLEAN
EFIAPI
ArmbianOnceOpenDisk (
  IN  EFI_HANDLE         Handle,
  OUT ARMBIAN_ONCE_DISK  *Disk
  );

/**
  The whole-disk handle (user data area) on the MMC controller at
  BaseAddress, if it has media.
**/
BOOLEAN
EFIAPI
ArmbianOnceFindMmcDisk (
  IN  UINT64             BaseAddress,
  OUT ARMBIAN_ONCE_DISK  *Disk
  );

EFI_STATUS
EFIAPI
ArmbianOnceDiskRead (
  IN  ARMBIAN_ONCE_DISK  *Disk,
  IN  UINT64             Offset,
  IN  UINTN              Size,
  OUT VOID               *Buffer
  );

/**
  What kind of FIT starts at Header, and how many bytes it spans with its
  external data (rounded up to a sector).  A FIT that is not this firmware's
  counts as a U-Boot only if one of its images is os "u-boot", arch "arm64",
  and no image names another architecture.
**/
ARMBIAN_ONCE_FIT_KIND
EFIAPI
ArmbianOnceClassifyFit (
  IN  VOID   *Header,
  IN  UINTN  Size,
  OUT UINTN  *Extent  OPTIONAL
  );

/**
  ArmbianOnceClassifyFit on the FIT_HEADER_MAX bytes at sector Lba of Disk.
**/
ARMBIAN_ONCE_FIT_KIND
EFIAPI
ArmbianOnceClassifyFitOnDisk (
  IN  ARMBIAN_ONCE_DISK  *Disk,
  IN  UINT64             Lba,
  OUT UINTN              *Extent  OPTIONAL
  );

/**
  TRUE if the idbloader at sector 64 of Disk has an SPL that takes requests.
**/
BOOLEAN
EFIAPI
ArmbianOnceSplTakesRequests (
  IN ARMBIAN_ONCE_DISK  *Disk
  );

/**
  TRUE if the eMMC slot is on the disk and in no partition other than the
  firmware region.  On FALSE because of a partition, its first and last
  sector are returned (both 0 when the disk is too small).
**/
BOOLEAN
EFIAPI
ArmbianOnceSlotIsFree (
  IN  ARMBIAN_ONCE_DISK  *Emmc,
  OUT UINT64             *PartStart  OPTIONAL,
  OUT UINT64             *PartEnd    OPTIONAL
  );

/**
  Connect the SD card controller, so that its disk exists even when the
  boot discovery policy connects nothing.  Cheap when it is connected
  already, and when the slot is empty (the driver has card detect).
**/
VOID
EFIAPI
ArmbianOnceConnectSd (
  VOID
  );

/**
  Whether "Armbian (U-Boot)" has something to boot now, from sector reads
  only (no file system is searched):

  - an SPL that takes requests on the eMMC or the SD card, and
  - a U-Boot FIT at sector 0x4000 of the SD card, or at sector 0x4000 of
    the eMMC (this firmware then runs from the SD card), or at sector
    0x4000 of another whole disk (USB, NVMe), or already in the eMMC slot.

  A U-Boot that only exists as /usr/lib/linux-u-boot-<branch>-<board>/u-boot-rockchip.bin
  in a file system is not looked for here; the application still finds it.

  @param[out] Where  a static description of what was found, or NULL

  @retval EFI_SUCCESS     there is a U-Boot to boot
  @retval EFI_NOT_FOUND   there is none
  @retval EFI_UNSUPPORTED the SPL does not take requests
**/
EFI_STATUS
EFIAPI
ArmbianOnceProbe (
  OUT CONST CHAR16  **Where  OPTIONAL
  );

#endif // ARMBIAN_ONCE_LIB_H_
