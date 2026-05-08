## @file
#  NanoPi M5 board-level FV module list (RK3576)
#
#  Copyright (c) 2025, FriendlyElec NanoPi M5 EDK2 Port
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  # ACPI tables (for Windows ARM64, FreeBSD, and ACPI-capable OS)
  INF RuleOverride = ACPITABLE $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Vendor Device Tree (pre-compiled DTB from FriendlyElec BSP kernel).
  # rk3576-nanopi-m5.dtb must be present before building with DTB support.
  # See: Platform/FriendlyElec/NanoPi-M5/DeviceTree/Vendor.inf for instructions.
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo (shared with other FriendlyElec boards)
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
