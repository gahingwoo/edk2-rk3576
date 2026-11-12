##
#  TaishanPi3M.Modules.fdf.inc -- FV_MAIN module list for the LCKFB TaishanPi 3M
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf through the
#  RK_PLATFORM_FVMAIN_MODULES define in TaishanPi3M.dsc.
#
#  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  # ACPI tables, installed alongside the DTB (ConfigTableMode ACPI + FDT), as
  # on the CM5-IO.  Not run with any operating system on this board.
  INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # ACPI platform driver: patches PCI0/PCI1 _STA and the MCFG at boot.  Here
  # rather than in the shared RK3576.fdf because only the ACPI boards list it
  # in [Components]; the FDT-only ROCK 4D build would fail on it.
  INF Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # Compiled mainline DTS (rk3576-lckfb-tspi-3m-uefi.dts)
  INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
