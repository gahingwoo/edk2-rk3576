## @file
#
#  UEFI Platform Description for Radxa ROCK 4D (RK3576)
#  Ported from ROCK 5B (RK3588) — uses RK3588Platform as base,
#  overrides SoC-specific PCDs for RK3576.
#
#  Board:  Radxa ROCK 4D
#  SoC:    RK3576 (4×A72 + 4×A53)
#  PMIC:   RK806 @ I2C1 0x23
#  RTC:    HYM8563 @ I2C2 0x51
#  Debug:  UART0 @ 0x2AD40000, 1.5Mbaud
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ROCK 4D RK3576 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = ROCK4D
  PLATFORM_VENDOR                = Radxa
  PLATFORM_GUID                  = 9c947b5c-f9ad-4605-8d0d-6d2118fe2660
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/ROCK4D.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  # ROCK4D / RK3576: SD/eMMC temporarily disabled — enabling RK_SD_ENABLE
  # broke boot (no serial). Need to bisect: SD lib init or RkSdmmcDxe binding
  # likely hangs on RK3576 GPIO/CRU mismatch. Re-enable after debug.
  # ROCK4D / RK3576: SD + eMMC re-enabled now that DEBUG_INFO output is
  # visible (PcdDebugPropertyMask=0x2F). Drivers route through:
  #   SD   : RkSdmmcDxe + DwMmcHcDxe → RkSdmmcPlatformLib (RK3576)
  #   eMMC : DwcSdhciDxe + SdMmcPciHcDxe → DwcSdhciPlatformLib (RK3576)
  # PHY/IO mux retained from SPL; UEFI only ungates clocks + reads CD GPIO.
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = FALSE  # ROCK 4D has no onboard eMMC — avoids 3-min Cmd0 timeout
  DEFINE RK_NOR_FLASH_ENABLE     = TRUE
  # FVB / NV-Variable stack enabled.
  # PcdRkAtagsBase=0 prevents the ATAGS bus-fault (BL31 TZASC on RK3576).
  # PcdRkFvbNvStorageSpiOffset=0xFC0000 gives the correct SPI NOR offset
  # (FdBase=0x40600000 makes the legacy formula give wrong 0x1C0000).
  # gRockchipTokenSpaceGuid.FspiBaseAddr points NorFlashDxe at the RK3576
  # FSPI0 MMIO (0x2A340000) so it can actually read/write the SPI flash.
  DEFINE RK_FVB_ENABLE           = TRUE
  DEFINE RK_RTC8563_ENABLE       = FALSE
  DEFINE RK_STATUS_LED_ENABLE    = FALSE
  # GMAC: RK3576 GmacPlatformDxe configures sdgmac_grf (RGMII-ID, RTL8211F)
  DEFINE RK3576_GMAC_ENABLE      = TRUE
  DEFINE RK_GMAC_ENABLE          = FALSE
  DEFINE RK3588_GMAC_ENABLE      = FALSE
  # PCIe: disabled - no RK3576-native PciHostBridgeLib yet.
  # Rk3588PciHostBridgeLib touches RK3588-only MMIO -> crash on RK3576.
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  DEFINE RK3588_PCIE_ENABLE      = FALSE
  # AHCI: RK3576 has no SATA controller
  DEFINE RK_AHCI_ENABLE          = FALSE
  # Display: RK3576 uses the same Synopsys DW HDMI QP (HDMI 2.1) IP as RK3588.
  # Mainline Linux dw-hdmi-qp.c binds against "rockchip,rk3576-dw-hdmi-qp".
  # Enable so DwHdmiQpLib (DXE driver) and the bundled HdptxHdmi PHY get linked.
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  # Non-OSI binaries not present for RK3576
  DEFINE RK_AMD_GOP_ENABLE       = FALSE
  # Secure Boot: enable UEFI image verification and key management UI.
  # All infrastructure (AuthVariableLib, SecureBootConfigDxe, SecureBootDefaultKeysDxe,
  # SecurityStubDxe+DxeImageVerificationLib) is gated on this flag in Rockchip.dsc.inc
  # and FvMainModules.fdf.inc. Default keys (Microsoft UEFI CA + DBX) are enrolled by
  # SecureBootDefaultKeysDxe from ArmPlatformPkg/SecureBootDefaultKeys.fdf.inc.
  # Required for Windows ARM64.
  DEFINE SECURE_BOOT_ENABLE      = TRUE

  DEFINE NETWORK_ENABLE          = TRUE
  DEFINE NETWORK_SNP_ENABLE      = TRUE
  DEFINE NETWORK_IP4_ENABLE      = TRUE
  DEFINE NETWORK_IP6_ENABLE      = TRUE   # Ip6Dxe in FV; runtime toggle via NetworkStackConfigDxe HII
  DEFINE NETWORK_PXE_BOOT_ENABLE = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = TRUE  # DnsDxe+HttpDxe+HttpUtilitiesDxe+HttpBootDxe in FV
  DEFINE NETWORK_TLS_ENABLE      = TRUE   # TlsDxe+TlsAuthConfigDxe — required for HTTPS boot
  DEFINE RK_X86_EMULATOR_ENABLE  = FALSE
  # Static SMBIOS tables (Type 0/1/2/3/4/7/9/11/16/17/19/32) — required so
  # UiApp banner shows "<NNNN> MB RAM" and OS sees system info.
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE
  # USB host: DWC3 DRD1 controller @ 0x23400000 (USB-A, combphy1).
  # DRD0 @ 0x23000000 (USB-C) is power-supply only — excluded to avoid
  # unnecessary combphy0 init and potential PCIe/combphy0 conflicts. (PCDs in
  # RK3576Base.dsc.inc). RK3576 has no EHCI/OHCI -> set count to 0 below.
  DEFINE RK_USB_ENABLE             = TRUE

  # Use RK3588 platform include (well-tested foundation)
  # RK3576-specific PCDs override below
!include Silicon/Rockchip/RK3588/RK3588Platform.dsc.inc

################################################################################
[LibraryClasses.common]
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf
  PciHostBridgeLib|Silicon/Rockchip/RK3576/Library/Rk3576PciHostBridgeLib/Rk3576PciHostBridgeLib.inf
  PciLib|MdePkg/Library/BasePciLibPciExpress/BasePciLibPciExpress.inf
  PciExpressLib|MdePkg/Library/BasePciExpressLib/BasePciExpressLib.inf
  PciSegmentLib|Silicon/Rockchip/RK3576/Library/Rk3576PciSegmentLib/Rk3576PciSegmentLib.inf
  # RK3576-specific SDRAM detection (overrides RK3588 SdramLib)
  SdramLib|Silicon/Rockchip/RK3576/Library/SdramLib/SdramLib.inf
  # RK3576-specific MMC platform glue (CRU clock setup, card-detect GPIO).
  # Default in Rockchip.dsc.inc points to Null stubs.
  RkSdmmcPlatformLib|Silicon/Rockchip/RK3576/Library/RkSdmmcPlatformLib/RkSdmmcPlatformLib.inf
  DwcSdhciPlatformLib|Silicon/Rockchip/RK3576/Library/DwcSdhciPlatformLib/DwcSdhciPlatformLib.inf
  # RK3576 OTP block lives at a different MMIO base than RK3588 (0xFECC0000)
  # and the silicon revision on ROCK 4D firewalls all access -> Data Abort.
  # Use a stub that returns zeros instead of touching the controller.
  OtpLib|Silicon/Rockchip/RK3576/Library/OtpLib/OtpLib.inf
  # RK3576-specific reset: writes BOOT_BROM_DOWNLOAD (0xEF08A53C) to
  # PMU0_GRF_OS_REG16 (0x26024040) before PSCI SYSTEM_RESET so TF-A
  # copies it to the NPOR-persistent PMU1_GRF_OS_REG0 and enters MaskROM.
  ResetSystemLib|Silicon/Rockchip/RK3576/Library/ResetSystemLib/ResetSystemLib.inf
  # RK3576-specific GPIO library with correct IOC_GRF @ 0x26040000 addresses.
  # The inherited RK3588 GpioLib uses IOC offsets 0xFD5F0000+ which are invalid
  # on RK3576, causing a Data Abort in GmacPlatformDxe (GmacIomux).
  GpioLib|Silicon/Rockchip/RK3576/Library/GpioLib/GpioLib.inf

################################################################################
[PcdsFixedAtBuild.common]

  # Debug visibility (override Rockchip.dsc.inc RELEASE defaults which set
  # PcdDebugPropertyMask=0x00 → DebugLib PrintEnabled bit cleared → all
  # DEBUG output suppressed regardless of error level).
  # 0x80000042 = ERROR | INFO | WARN | LOAD
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000042
  # 0x2F = ASSERT|PRINT|CODE|CLEAR_MEM|ASSERT_BREAKPOINT
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F

  # ROCK 4D / RK3576 system memory layout (override RK3588Base 1GB default).
  # PcdSystemMemorySize affects the SEC entry stub's UEFI region calculation:
  #   UefiMemoryBase = SystemMemoryEnd+1 - UefiRegionSize
  # With 0x40000000 (1GB), UefiMemoryBase falls to 0x38000000 which is too
  # close to the FD load (0x40600000) and HOB heap dereferences hang.
  # 0x80000000 (2GB) gives UefiMemoryBase = 0x78000000 (well below FD).
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x00000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  # SMBIOS
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"ROCK 4D"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"Radxa"
  gRockchipTokenSpaceGuid.PcdFamilyName|"ROCK 4"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://radxa.com/products/rock4/4d"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-rock-4d"

  # UART0 base address (RK3576: 0x2AD40000)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000

  # eMMC (sdhci @ 0x2A330000)
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  # DwcSdhciDxe driver uses this PCD (RK3588 default 0xfe2e0000 → MMU fault on RK3576).
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000

  # SD card (sdmmc @ 0x2A310000)
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000
  # RkSdmmcDxe driver uses this PCD (RK3588 default 0xfe2c0000 → MMU fault on RK3576).
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddress|0x2A310000

  # SPI NOR flash (sfc0 @ 0x2A340000)
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000
  # NorFlashDxe reads gRockchipTokenSpaceGuid.FspiBaseAddr (not gRK3576…)
  gRockchipTokenSpaceGuid.FspiBaseAddr|0x2A340000
  # FVB: ATAGS are firewalled on RK3576; set base=0 to skip lookup safely.
  gRockchipTokenSpaceGuid.PcdRkAtagsBase|0
  # FVB: correct SPI NOR byte offset for NV variable store.
  # (FdBase=0x40600000 skews the legacy formula to 0x1C0000; correct=0xFC0000)
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0xFC0000

  # USB DWC3 (0x23000000, 0x23400000)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23400000) }
  # RK3576 has no EHCI/OHCI controllers (RK3588 has 2). Override to 0
  # to skip the legacy USB2 path that would otherwise hit invalid MMIO.
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  # I2C
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # RTC (HYM8563 @ I2C2 0x51) — disabled until RK3576 RTC driver landed.
  # The corresponding INF (Rtc8563PlatformDxe) is gated by RK_RTC8563_ENABLE=FALSE,
  # so the PCD owner DEC is no longer reachable from the FDF and the build fails
  # with "Pcd ... not declared in DEC files referenced in INF files in FDF".
  # gPcf8563RealTimeClockLibTokenSpaceGuid.PcdI2cSlaveAddress|0x51
  # gRockchipTokenSpaceGuid.PcdRtc8563Bus|0x2

  # ComboPHY modes (RK3576Base.dsc.inc is not in include chain; set here explicitly)
  # PHY0 → PCIe (pcie2x1l0, M.2 slot) | PHY1 → USB3 (DRD1 USB-A)
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)
  # Both PHYs are user-switchable via the HII ComboPHY menu
  gRK3576TokenSpaceGuid.PcdComboPhy0Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy1Switchable|TRUE

  # ConfigTable/FDT FixedAtBuild defaults (RK3576Base.dsc.inc not in include chain)
  # CONFIG_TABLE_MODE: ACPI=0x1, FDT=0x2, ACPI_FDT=0x3
  # Default: ACPI-only (Windows ARM64 / ACPI-capable Linux)
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|0x00000001
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault|0
  # FDT_COMPAT_MODE: UNSUPPORTED=0, VENDOR=1, MAINLINE=2
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|0x00000002
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|TRUE

  # GMAC0 — RTL8211F PHY in RGMII-ID mode (PHY provides both TX and RX delays)
  # ROCK 4D wiring: gmac0@0x2A220000, PHY reset GPIO2_PB5 (active-low)
  gRK3576TokenSpaceGuid.PcdGmac0Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0    # 0 = RGMII-ID: PHY handles TX delay
  gRK3576TokenSpaceGuid.PcdGmac0RxDelay|0    # 0 = RGMII-ID: PHY handles RX delay
  gRK3576TokenSpaceGuid.PcdGmac1Supported|FALSE
  gRK3576TokenSpaceGuid.PcdGmac1TxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1RxDelay|0

  # Network stack runtime defaults (enable PXE boot via GMAC or Realtek PCIe/USB NIC)
  gRockchipTokenSpaceGuid.PcdNetworkStackEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv4EnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv6EnabledDefault|FALSE  # Off by default; user enables via NetworkStackConfigDxe HII
  gRockchipTokenSpaceGuid.PcdNetworkStackPxeBootEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackHttpBootEnabledDefault|FALSE  # Off by default; user enables via NetworkStackConfigDxe HII

  # CRU base (0x27200000)
  gRockchipTokenSpaceGuid.CruBaseAddr|0x27200000

  # GIC-400 (GICv2) — from rk3576.dtsi: interrupt-controller@2a701000
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x2A701000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x2A702000

  #
  # Display connectors. ROCK 4D exposes a single HDMI 2.1 TX (HDMI0) wired
  # through HDPTX0 PHY. Listing it here causes RK3588Dxe (shared) to seed
  # PcdDisplayConnectorsMask with VOP_OUTPUT_IF_HDMI0, which in turn drives
  # DwHdmiQpLib registration and the Vop2Dxe HDMI0 IF_CTRL programming.
  #
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_HDMI0
  })}



################################################################################
[PcdsDynamicHii.common.DEFAULT]
  # RK3576 HII-backed PCDs: stored in NVRAM EFI variables, settable via UEFI menu.
  # Default values use FixedAtBuild constants from RK3576.dec [PcdsFixedAtBuild].
  gRK3576TokenSpaceGuid.PcdConfigTableMode|L"ConfigTableMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdConfigTableModeDefault
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatMode|L"AcpiPcieEcamCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtCompatMode|L"FdtCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtForceGop|L"FdtForceGop"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtForceGopDefault
  gRK3576TokenSpaceGuid.PcdFdtSupportOverrides|L"FdtSupportOverrides"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixup|L"FdtOverrideFixup"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideBasePath|L"FdtOverrideBasePath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }
  gRK3576TokenSpaceGuid.PcdFdtOverrideOverlayPath|L"FdtOverrideOverlayPath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }

  # ComboPHY mode selection — HII formid 0x1002 (settable only when PcdComboPhy*Switchable=TRUE)
  gRK3576TokenSpaceGuid.PcdComboPhy0Mode|L"ComboPhy0Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault
  gRK3576TokenSpaceGuid.PcdComboPhy1Mode|L"ComboPhy1Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault

  # Display mode selection — HII formid 0x1000
  # PCDs are in gRK3588TokenSpaceGuid because the display driver libraries read from that namespace.
  # The NVRAM variable GUID is gRK3576DxeFormSetGuid (this formset), NOT gRK3588DxeFormSetGuid.
  gRK3588TokenSpaceGuid.PcdDisplayModePreset|L"DisplayModePreset"|gRK3576DxeFormSetGuid|0x0|{0x0F, 0x00, 0x00, 0x00}
  gRK3588TokenSpaceGuid.PcdDisplayModeCustom|L"DisplayModeCustom"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayConnectorsPriority|L"DisplayConnectorsPriority"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayForceOutput|L"DisplayForceOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayDuplicateOutput|L"DisplayDuplicateOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayRotation|L"DisplayRotation"|gRK3576DxeFormSetGuid|0x0|0
  gRK3588TokenSpaceGuid.PcdHdmiSignalingMode|L"HdmiSignalingMode"|gRK3576DxeFormSetGuid|0x0|0

################################################################################
[BuildOptions]
  # ROCK4D / RK3576: define SOC_RK3576 globally so that the SoC-specific
  # #ifdef SOC_RK3576 blocks in DwHdmiQpLib.h, DwHdmiQpLib.c, Vop2Dxe.c and
  # PhyRockchipSamsungHdptxHdmi.c (RK3576 register windows + runtime stubs)
  # actually compile. Without it, the RK3576_* macros are undefined and the
  # driver falls back to RK3588 register addresses which point at unmapped /
  # firewalled MMIO and bus-abort the SoC.
  GCC:*_*_AARCH64_CC_FLAGS = -DSOC_RK3576

################################################################################
[Components.common]
  # ACPI tables (for Windows ARM64, FreeBSD, and ACPI-capable OS)
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Board-specific Device Tree (Vendor = pre-compiled DTB)
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Mainline Device Tree (built from DTS at compile time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # RK3576 ACPI platform driver (replaces RK3588 AcpiPlatformDxe for this board)
  # Uses RK3576-specific MCFG struct / PCIe addresses and DSDT fixups.
  # The RK3588 AcpiPlatformDxe is still compiled (from RK3588Base.dsc.inc) but
  # exits early because gRK3588TokenSpaceGuid.PcdConfigTableMode = FDT-only (0x02).
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # FDT platform fixups (PCIe/SATA/VOP device tree nodes)
  Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf

!if $(RK3576_PCIE_ENABLE) == TRUE
  # PCI support (requires RK3576_PCIE_ENABLE = TRUE)
  ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
  EmbeddedPkg/Drivers/NonCoherentIoMmuDxe/NonCoherentIoMmuDxe.inf
!endif

  # Simple Framebuffer GOP — installs GOP over the VOP2 framebuffer that
  # U-Boot leaves in place, without re-initialising VOP2 or HDMI.
  Silicon/Rockchip/RK3576/Drivers/RK3576SimpleFbDxe/RK3576SimpleFbDxe.inf

!if $(RK3576_GMAC_ENABLE) == TRUE
  # RK3576 GMAC platform initializer (sdgmac_grf, PHY reset, MDIO PHY init)
  Silicon/Rockchip/RK3576/Drivers/GmacPlatformDxe/GmacPlatformDxe.inf

  # Synopsys DWC EQoS SNP driver (same IP as RK3588, works with our platform protocol)
  Silicon/Synopsys/DesignWare/Drivers/DwcEqosSnpDxe/DwcEqosSnpDxe.inf
!endif
