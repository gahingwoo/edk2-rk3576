/** @file
 *
 *  PCI Host Bridge Init for Rockchip RK3576
 *
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef PCI_HOST_BRIDGE_INIT_H_
#define PCI_HOST_BRIDGE_INIT_H_

#include <Uefi.h>

EFI_STATUS  InitializePciHost (UINT32 Segment);
BOOLEAN     IsPcieNumEnabled (UINTN PcieNum);

#endif // PCI_HOST_BRIDGE_INIT_H_
