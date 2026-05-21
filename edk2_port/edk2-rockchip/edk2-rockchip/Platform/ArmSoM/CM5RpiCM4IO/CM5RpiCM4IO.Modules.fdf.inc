##
#  CM5RpiCM4IO.Modules.fdf.inc — FV_MAIN module list for ArmSoM CM5 (RPi CM4 IO Board)
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf via
#  RK_PLATFORM_FVMAIN_MODULES define in CM5RpiCM4IO.dsc.
##

  # ACPI tables -- disabled: FDT-only build.
  # To enable, uncomment the line below and the matching entries in CM5RpiCM4IO.dsc.
  # INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Compiled mainline DTS (rk3576-armsom-cm5-rpi-cm4-io-uefi.dts)
  INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor DTB — currently not included (requires pre-compiled BSP DTB).
  # To enable, obtain rk3576-armsom-cm5-rpi-cm4-io.dtb from the ArmSoM
  # rockchip-kernel (branch linux-6.1-stan-rkr6.1 or armbian rk-6.1-rkr4.1),
  # copy it to:
  #   devicetree/vendor/rk3576-armsom-cm5-rpi-cm4-io.dtb
  # then uncomment the line below:
  # INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen / boot logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
