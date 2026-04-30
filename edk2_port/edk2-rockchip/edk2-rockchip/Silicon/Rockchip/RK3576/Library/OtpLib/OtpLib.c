/** @file
 *
 *  RK3576 OTP stub library.
 *
 *  RK3576 OTP controller is at a different MMIO base than RK3588 (0xFECC0000)
 *  and the silicon revision on ROCK 4D firewalls all access from non-secure
 *  EL2. Until proper RK3576 OTP support is implemented, return zeros so the
 *  consumers (PlatformSmbiosDxe serial/board ID) work without faulting.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/OtpLib.h>

VOID
OtpRead (
  IN UINT16  Offset,
  IN UINT16  Length,
  OUT UINT8  *Data
  )
{
  if (Data != NULL) {
    ZeroMem (Data, Length);
  }
}

VOID
OtpReadCpuCode (
  OUT UINT16  *CpuCode
  )
{
  if (CpuCode != NULL) {
    *CpuCode = 0;
  }
}

VOID
OtpReadId (
  OUT UINT8  Id[16]
  )
{
  if (Id != NULL) {
    ZeroMem (Id, 16);
  }
}

VOID
OtpReadCpuVersion (
  OUT UINT8  *Version
  )
{
  if (Version != NULL) {
    *Version = 0;
  }
}
