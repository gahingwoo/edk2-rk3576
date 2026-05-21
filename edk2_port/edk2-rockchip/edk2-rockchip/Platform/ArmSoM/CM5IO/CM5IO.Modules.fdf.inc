##
#  CM5IO.Modules.fdf.inc — FV_MAIN module list for ArmSoM CM5-IO
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf via
#  RK_PLATFORM_FVMAIN_MODULES define in CM5IO.dsc.
##

  # ACPI tables — disabled: FDT-only build.
  # To enable, uncomment the line below and the matching entries in CM5IO.dsc.
  # INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Compiled mainline DTS (rk3576-armsom-cm5-io-uefi.dts)
  INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor DTB — currently not included (requires pre-compiled BSP DTB).
  # To enable, obtain rk3576-armsom-cm5-io.dtb from the ArmSoM rockchip-kernel
  # (branch linux-6.1-stan-rkr6.1), copy it to:
  #   devicetree/vendor/rk3576-armsom-cm5-io.dtb
  # then uncomment the line below:
  # INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen / boot logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
