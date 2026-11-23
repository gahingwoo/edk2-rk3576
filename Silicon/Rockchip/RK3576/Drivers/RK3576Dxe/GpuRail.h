/** @file
 *
 *  GpuRail.c: a read-only report of vdd_gpu_s0 and the GPU's OTP data.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_GPU_RAIL_H_
#define RK3576_GPU_RAIL_H_

#define GPU_RAIL_VARIABLE_NAME     L"GpuRail"
#define GPU_RAIL_REPORT_SIGNATURE  SIGNATURE_32 ('G', 'P', 'U', 'R')
#define GPU_RAIL_REPORT_VERSION    1

#define GPU_RAIL_FLAG_RAIL_VALID    BIT0   // RK806 answered, BUCK5 on, RailUv inside 550..900 mV
#define GPU_RAIL_FLAG_ENABLED       BIT1   // POWER_EN1 bit 0
#define GPU_RAIL_FLAG_OTP_VALID     BIT2   // cpu-code read as 35 76
#define GPU_RAIL_FLAG_PLAIN_RK3576  BIT3   // specification 0 or 1 (not J, M or S)

//
// The variable's layout, 32 bytes.  Shared with wddm-mali-g52 (adapter.c,
// MW_GPU_RAIL_REPORT); a new field means a new version.
//
#pragma pack(1)
typedef struct {
  UINT32    Signature;       // 'GPUR'
  UINT16    Version;         // 1
  UINT16    Size;            // sizeof (GPU_RAIL_REPORT)
  UINT32    RailUv;          // BUCK5 ON_VSEL as microvolts; 0 when not read
  UINT8     OnVsel;          // RK806 0x1E
  UINT8     SlpVsel;         // RK806 0x28
  UINT8     PowerEn1;        // RK806 0x01
  UINT8     Buck5Config;     // RK806 0x14
  UINT8     Specification;   // OTP 0x08 bits 4:0
  UINT8     GpuLeakage;      // OTP 0x21
  UINT8     Flags;           // GPU_RAIL_FLAG_*
  UINT8     Reserved0;
  UINT16    OppInfoMinMhz;   // OTP 0x48
  UINT16    OppInfoMaxMhz;   // OTP 0x4A
  UINT8     OppInfoVoltMv;   // OTP 0x4C
  UINT8     OppInfoLength;   // OTP 0x4D
  UINT16    Reserved1;
  UINT32    ClkSel165;       // CRU_CLKSEL_CON165 when the report was made
} GPU_RAIL_REPORT;
#pragma pack()

/**
  On a board that sets PcdGpuRailReport: once the RK806's I2C bus is up,
  read BUCK5 and the GPU OTP bytes, log them with the operating points they
  allow, and publish the GpuRail variable.  Writes nothing to the hardware.
**/
VOID
RK3576GpuRailStart (
  VOID
  );

#endif // RK3576_GPU_RAIL_H_
