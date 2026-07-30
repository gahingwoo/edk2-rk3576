##
#  CM5IO.Modules.fdf.inc — FV_MAIN module list for ArmSoM CM5-IO
#
#  Included by Silicon/Rockchip/RK3576/RK3576.fdf via
#  RK_PLATFORM_FVMAIN_MODULES define in CM5IO.dsc.
##

  # ACPI tables — enabled alongside FDT (ConfigTableMode 0x3), so the same
  # image boots Linux via DTB and Windows on ARM / FreeBSD via ACPI.
  INF RuleOverride = ACPITABLE  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # ACPI platform driver. Without this the static tables install but nothing
  # fixes them up at runtime: it patches MCFG/IORT and sets \_SB.PCI0._STA /
  # PCI1._STA from the live ComboPHY mode. CM5-IO is combphy0=PCIe /
  # combphy1=USB3, so PCI1 must be reported absent — otherwise the DSDT's
  # static _STA of 0x0F tells Windows to probe an unclocked PCIe controller.
  #
  # It lives here rather than in the shared RK3576.fdf because it is only in
  # [Components] for the platforms that enable ACPI; referencing it from the
  # shared FDF would break the FDT-only builds (ROCK4D, CM5Waveshare*).
  # The driver itself no-ops unless ConfigTableMode has the ACPI bit set.
  INF Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

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
