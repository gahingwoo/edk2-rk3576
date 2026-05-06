## @file
#  ROCK 4D board-level FV module list
#  Primary mode: FDT (Device Tree) for Armbian/Radxa/Rockchip BSP Linux.
#  ACPI tables (for Windows ARM64) are a future work item.
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ROCK 4D RK3576 Port
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  # Vendor Device Tree (pre-compiled DTB from U-Boot upstream)
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Mainline Device Tree (mainline kernel DTS + UEFI overlay, built at compile time)
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
