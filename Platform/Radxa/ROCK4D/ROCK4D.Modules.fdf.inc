## @file
#  ROCK 4D board-level FV module list
#  Device tree for Linux and ACPI tables for Windows on Arm, both installed
#  (ConfigTableMode ACPI + FDT, see ROCK4D.dsc).
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  # ACPI tables, installed alongside the DTB (ConfigTableMode ACPI + FDT), as
  # on the CM5-IO.  Not run with any operating system on this board.
  INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # ACPI platform driver: patches PCI0/PCI1 _STA and the MCFG at boot.  Here
  # rather than in the shared RK3576.fdf because only the ACPI boards list it
  # in [Components].
  INF Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # Vendor Device Tree (pre-compiled DTB from U-Boot upstream)
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Mainline Device Tree (mainline kernel DTS + UEFI overlay, built at compile time)
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # Powers the board off through the RK806 at a shutdown (ROCK4D.dsc)
  INF Silicon/Rockchip/RK3576/Drivers/Rk806I2cPowerOffDxe/Rk806I2cPowerOffDxe.inf
