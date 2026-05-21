##
#  CM5WaveshareA.Modules.fdf.inc — FV_MAIN module list for ArmSoM CM5 (Waveshare CM4-IO-BASE-A)
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf via
#  RK_PLATFORM_FVMAIN_MODULES define in CM5WaveshareA.dsc.
##

  # ACPI tables — disabled: FDT-only build.
  # To enable, uncomment the line below and the matching entries in CM5WaveshareA.dsc.
  # INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Compiled mainline DTS (rk3576-armsom-cm5-waveshare-cm4a-uefi.dts)
  INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor DTB — currently not included (requires pre-compiled BSP DTB).
  # To enable, obtain rk3576-armsom-cm5-waveshare-cm4a.dtb from the ArmSoM
  # rockchip-kernel (branch linux-6.1-stan-rkr6.1) or build from source,
  # copy it to:
  #   devicetree/vendor/rk3576-armsom-cm5-waveshare-cm4a.dtb
  # then uncomment the line below:
  # INF RuleOverride = DTB  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen / boot logo
  INF $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
