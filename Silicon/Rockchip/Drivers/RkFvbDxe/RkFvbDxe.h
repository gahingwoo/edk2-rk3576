/** @file
 *
 *  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.
 *  Copyright (c) 2017 Marvell International Ltd.
 *  Copyright (c) 2021-2022 Rockchip Electronics Co., Ltd.
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __FVB_FLASH_DXE_H__
#define __FVB_FLASH_DXE_H__

#include <Protocol/BlockIo.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Protocol/NorFlashProtocol.h>

#define GET_DATA_OFFSET(BaseAddr, Lba, LbaSize)  ((BaseAddr) + (UINTN)((Lba) * (LbaSize)))

#define FVB_FLASH_SIGNATURE  SIGNATURE_32('S', 'n', 'o', 'r')
#define INSTANCE_FROM_FVB_THIS(a)  CR(a, FVB_DEVICE, FvbProtocol, FVB_FLASH_SIGNATURE)

//
// Define two helper macro to extract the Capability field or Status field in FVB
// bit fields.
//
#define EFI_FVB2_CAPABILITIES  (EFI_FVB2_READ_DISABLED_CAP |\
                               EFI_FVB2_READ_ENABLED_CAP | \
                               EFI_FVB2_WRITE_DISABLED_CAP | \
                               EFI_FVB2_WRITE_ENABLED_CAP | \
                               EFI_FVB2_LOCK_CAP)

#define EFI_FVB2_STATUS  (EFI_FVB2_READ_STATUS |      \
                               EFI_FVB2_WRITE_STATUS | \
                               EFI_FVB2_LOCK_STATUS)

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} FVB_DEVICE_PATH;

typedef struct {
  UNI_NOR_FLASH_PROTOCOL                 *SpiFlashProtocol;
  BOOLEAN                                IsSpiFlashAvailable;
  BOOLEAN                                IsFvbHeaderValid;

  EFI_DEVICE_PATH_PROTOCOL               *DiskDevice;
  UINT32                                 DiskMediaId;
  BOOLEAN                                DiskDataInvalidated;

  EFI_HANDLE                             Handle;

  UINT32                                 Signature;

  UINTN                                  DeviceBaseAddress;
  UINTN                                  RegionBaseAddress;
  UINTN                                  Size;
  UINTN                                  FvbOffset;
  UINTN                                  FvbSize;
  EFI_LBA                                StartLba;

  EFI_BLOCK_IO_MEDIA                     Media;
  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL    FvbProtocol;

  FVB_DEVICE_PATH                        DevicePath;

  //
  // SD/eMMC only: where variable writes made after ExitBootServices are
  // kept until the next boot commits them (PcdRkFvbRuntimeStoreBase, 0 when
  // not in use), whether writes go there now, and the CRC32 of the store as
  // it is on the medium, which is what those writes were made on top of.
  //
  UINTN                                  RuntimeStoreBase;
  BOOLEAN                                RuntimeStoreActive;
  UINT32                                 DiskCrc;
} FVB_DEVICE;

//
// Header of the runtime variable store.  The store data follows at
// FVB_RUNTIME_STORE_DATA_OFFSET and is a full image of the FVB region
// (variables, FTW working and FTW spare), so committing it is the same
// write as any other NV dump.
//
#define FVB_RUNTIME_STORE_SIGNATURE    SIGNATURE_64 ('R', 'K', 'F', 'V', 'B', 'R', 'T', '2')
#define FVB_RUNTIME_STORE_DATA_OFFSET  SIZE_4KB

typedef struct {
  UINT64    Signature;
  UINT32    HeaderSize;
  UINT32    DataSize;
  UINT32    BaseCrc;   // CRC32 of the store on the medium these writes apply to
  UINT32    DataCrc;   // CRC32 of the DataSize bytes of data
  UINT32    Updates;   // writes folded in since the record was started
  UINT32    HeaderCrc; // CRC32 of this header with Signature and HeaderCrc 0
  UINT32    BootDev;   // RKATAG_BOOTDEV_TYPE the SPL loaded the store from
  UINT32    Reserved;  // 0
} FVB_RUNTIME_STORE_HEADER;

//
// Diagnostics for the runtime store (board bring-up of the feature).
//
// A block at FVB_RT_DIAG_OFFSET in the header page counts what happened to
// the store during a boot, including after ExitBootServices where nothing
// can be printed, and two canaries further into the range show whether the
// DRAM came through the reset.  At a reset made before ExitBootServices a
// grid of canaries is also written into free DRAM, every FVB_RT_GRID_STEP,
// so the next boot can tell which DRAM the reset and the boot chain leave
// alone.  The next boot prints all of it.
//
#define FVB_RT_DIAG_SIGNATURE    SIGNATURE_64 ('R', 'K', 'F', 'V', 'B', 'D', 'G', '1')
#define FVB_RT_CANARY_SIGNATURE  SIGNATURE_64 ('R', 'K', 'F', 'V', 'B', 'C', 'N', '1')
#define FVB_RT_GRID_SIGNATURE    SIGNATURE_64 ('R', 'K', 'F', 'V', 'B', 'G', 'R', 'D')
#define FVB_RT_DIAG_OFFSET       0x800
#define FVB_RT_CANARY_MID        0x38000
#define FVB_RT_CANARY_TAIL       0x3FFC0
#define FVB_RT_GRID_STEP         SIZE_1MB
#define FVB_RT_GRID_MIN          0x40000000ULL
#define FVB_RT_GRID_MAX          0x200000000ULL

#define FVB_RT_EV_EBS            BIT0  // ExitBootServices handler ran
#define FVB_RT_EV_RT_WRITE       BIT1  // FvbWrite/FvbEraseBlocks after ExitBootServices
#define FVB_RT_EV_RECORD         BIT2  // a record was written
#define FVB_RT_EV_SVAM           BIT3  // SetVirtualAddressMap converted the pointers
#define FVB_RT_EV_BOOT_RESET     BIT4  // reset notify ran (reset before ExitBootServices)
#define FVB_RT_EV_GRID           BIT5  // the canary grid was written

typedef struct {
  UINT64    Signature;
  UINT32    Boot;           // boot generation that wrote this block
  UINT32    Events;         // FVB_RT_EV_*
  UINT32    RtWrites;       // FVB writes and erases after ExitBootServices
  UINT32    RecordUpdates;  // records written this boot
  UINT32    LastDataCrc;    // DataCrc of the last record written
  UINT32    LastHeaderCrc;  // HeaderCrc of the last record written
  UINT32    GridWritten;    // canaries placed by the reset notify
  UINT32    Crc;            // CRC32 of this block with Crc 0
} FVB_RT_DIAG;

typedef struct {
  UINT64    Signature;
  UINT64    Address;        // where it was written
  UINT32    Boot;           // boot generation that wrote it
  UINT32    Crc;            // CRC32 of the 20 bytes above
} FVB_RT_CANARY;

typedef struct {
  UINT32    AtagBootDevType;
  UINT32    ControllerBaseAddress;
} FVB_RK_BOOT_DEVICE;

EFI_STATUS
EFIAPI
FvbGetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_FVB_ATTRIBUTES_2                 *Attributes
  );

EFI_STATUS
EFIAPI
FvbSetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN OUT    EFI_FVB_ATTRIBUTES_2                 *Attributes
  );

EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_PHYSICAL_ADDRESS                 *Address
  );

EFI_STATUS
EFIAPI
FvbGetBlockSize (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  OUT       UINTN                                *BlockSize,
  OUT       UINTN                                *NumberOfBlocks
  );

EFI_STATUS
EFIAPI
FvbRead (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN OUT    UINT8                                *Buffer
  );

EFI_STATUS
EFIAPI
FvbWrite (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN OUT    UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  );

EFI_STATUS
EFIAPI
FvbEraseBlocks (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  ...
  );

#endif /* __FVB_FLASH_DXE_H__ */
