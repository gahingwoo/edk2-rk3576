##
#  TaishanPi3M.Modules.fdf.inc -- FV_MAIN module list for the LCKFB TaishanPi 3M
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf through the
#  RK_PLATFORM_FVMAIN_MODULES define in TaishanPi3M.dsc.
#
#  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  # FDT only.  No ACPI tables are built for this board, so neither
  # AcpiTables.inf nor RK3576AcpiPlatformDxe is listed.

  # Compiled mainline DTS (rk3576-lckfb-tspi-3m-uefi.dts)
  INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
