/** @file
 *
 *  RK3576 OTP Library.
 *
 *  RK3576 uses the same OTPC (Rockchip OTP Controller) hardware as RK3588,
 *  with different base address and non-secure block offset.
 *
 *  Hardware source: Linux kernel drivers/nvmem/rockchip-otp.c
 *    rk3576_data → { .reg_read = rk3588_otp_read, .read_offset = 0x700,
 *                    .word_size = 4, .size = 0x100 }
 *
 *  OTP cell layout (from rk3576.dtsi):
 *    cpu-code  @ byte 0x02, length 2
 *    cpu-version @ byte 0x05, length 1
 *    id        @ byte 0x0A, length 16   (chip serial number)
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/OtpLib.h>

#define RK3576_OTP_BASE  0x2A580000UL

/* OTPC registers — same layout as RK3588 */
#define RK3576_OTPC_AUTO_CTRL  (RK3576_OTP_BASE + 0x0004U)
#define  RK3576_ADDR_SHIFT     16
#define  RK3576_BURST_SHIFT    8
#define RK3576_OTPC_AUTO_EN    (RK3576_OTP_BASE + 0x0008U)
#define  RK3576_AUTO_EN        BIT0
#define RK3576_OTPC_DOUT0      (RK3576_OTP_BASE + 0x0020U)
#define RK3576_OTPC_INT_ST     (RK3576_OTP_BASE + 0x0084U)
#define  RK3576_RD_DONE        BIT1

#define RK3576_BLOCK_SIZE         4U    /* bytes per OTP word (u32) */
#define RK3576_NO_SECURE_OFFSET   0x1C0U /* 0x700 / 4 = first non-secure block */

VOID
OtpRead (
  IN  UINT16  Offset,
  IN  UINT16  Length,
  OUT UINT8   *Data
  )
{
  UINT32  Addr;
  UINT16  AddrOffset;
  UINT16  BytesRemaining;
  UINT32  Value;
  UINTN   Retry;

  if ((Data == NULL) || (Length == 0)) {
    return;
  }

  Addr           = (Offset / RK3576_BLOCK_SIZE) + RK3576_NO_SECURE_OFFSET;
  AddrOffset     = Offset % RK3576_BLOCK_SIZE;
  BytesRemaining = Length;
  Retry          = 100000;

  while (BytesRemaining > 0) {
    MmioWrite32 (
      RK3576_OTPC_AUTO_CTRL,
      (Addr << RK3576_ADDR_SHIFT) | (1U << RK3576_BURST_SHIFT)
      );
    MmioWrite32 (RK3576_OTPC_AUTO_EN, RK3576_AUTO_EN);

    while ((MmioRead32 (RK3576_OTPC_INT_ST) & RK3576_RD_DONE) == 0) {
      MicroSecondDelay (1);
      if (--Retry == 0) {
        DEBUG ((DEBUG_WARN, "RK3576 OTP: read timeout at block 0x%X\n", Addr));
        ZeroMem (Data, BytesRemaining);
        return;
      }
    }

    MmioWrite32 (RK3576_OTPC_INT_ST, RK3576_RD_DONE);  /* clear interrupt */

    Value = MmioRead32 (RK3576_OTPC_DOUT0);

    while ((AddrOffset < RK3576_BLOCK_SIZE) && (BytesRemaining > 0)) {
      *Data++ = (UINT8)((Value >> (8U * AddrOffset)) & 0xFFU);
      AddrOffset++;
      BytesRemaining--;
    }

    AddrOffset = 0;
    Addr++;
    Retry = 100000;
  }
}

VOID
OtpReadCpuCode (
  OUT UINT16  *CpuCode
  )
{
  if (CpuCode != NULL) {
    OtpRead (0x02U, 0x2U, (UINT8 *)CpuCode);
  }
}

VOID
OtpReadId (
  OUT UINT8  Id[16]
  )
{
  if (Id != NULL) {
    OtpRead (0x0AU, 0x10U, Id);
  }
}

VOID
OtpReadCpuVersion (
  OUT UINT8  *Version
  )
{
  if (Version != NULL) {
    OtpRead (0x05U, 0x1U, Version);
  }
}

