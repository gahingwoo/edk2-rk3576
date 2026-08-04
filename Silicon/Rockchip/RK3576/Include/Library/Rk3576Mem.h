/** @file
 *
 *  Copyright (c) 2021, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2019, Pete Batard <pete@akeo.ie>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_MEM_H__
#define RK3576_MEM_H__

#define RK3576_MEM_UNMAPPED_REGION  0
#define RK3576_MEM_BASIC_REGION     1
#define RK3576_MEM_RUNTIME_REGION   2
#define RK3576_MEM_RESERVED_REGION  3

typedef struct {
  CONST CHAR16    *Name;
  UINTN           Type;
} RK3576_MEMORY_REGION_INFO;

VOID
Rk3576PlatformGetVirtualMemoryInfo (
  IN RK3576_MEMORY_REGION_INFO  **MemoryInfo
  );

#endif /* RK3576_MEM_H__ */
