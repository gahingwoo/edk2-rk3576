## @file
#
#  UEFI Platform Description for ArmSoM CM5 on Waveshare CM4-IO-BASE-A (RK3576)
#
#  The ArmSoM CM5 is a Raspberry-Pi CM4-form-factor compute module built on
#  the Rockchip RK3576 SoC.  This platform targets the Waveshare CM4-IO-BASE-A
#  carrier board which provides HDMI0, M.2 PCIe, GbE, 40-pin GPIO header,
#  microSD, and USB 2.0 ports via FE1.1S USB 2.0 hub.
#
#  IMPORTANT HARDWARE NOTES:
#    - Only ONE HDMI port works: CM5's single HDMI TX maps to the HDMI0
#      connector.  The HDMI1 connector on this carrier carries CM5's USB3
#      SS signals (repurposed for the USB3/4 adapter connector).
#    - The two Type-A USB 2.0 ports go through the FE1.1S hub — HS only.
#    - USB3 SuperSpeed is available only via the side USB adapter connector
#      (DRD0 lane), not the standard USB-A ports.
#    - No USB-C OTG port on this carrier board.
#    - USB HOST 5V is always-on from the carrier board (no GPIO needed).
#    - CM5 v1.2 has a hardware bug: pin 21 (LED_GREEN_EN / GPIO2_PD0)
#      is non-functional.  Fixed in CM5 v1.3.
#
#  Board:  ArmSoM CM5 + Waveshare CM4-IO-BASE-A
#  SoC:    RK3576 (4xA72 + 4xA53)
#  PMIC:   RK806 @ I2C1 0x23 (interrupt GPIO0 PA6)
#  WiFi:   SYN43752 (rtl8852bs) via SDIO on CM5 module
#  BT:     SYN43752 via UART4 on CM5 module
#  Debug:  UART0 @ 0x2AD40000, 1.5 Mbaud
#
#  Differences vs CM5-IO platform:
#    - USB HOST 5V: always-on from carrier (no GPIO, vs CM5-IO GPIO4_PB0)
#    - USB OTG 5V:  always-on from carrier (no GPIO, vs CM5-IO GPIO2_PB6)
#    - No carrier LEDs on CM5-accessible GPIOs
#    - No I2C5 fan controller or carrier RTC (carrier-specific to RPi CM4 IO)
#    - PCIe/GMAC/HDMI GPIOs: assumed same as CM5-IO (verify with BASE-A schematic)
#
#  GPIO note:
#    PCIe and GbE GPIO assignments are derived from ArmSoM CM5-IO schematics
#    as the Waveshare CM4-IO-BASE-A schematic is not publicly available.
#    Verify against official Waveshare CM4-IO-BASE-A V1.x schematic before
#    deploying in production.
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, Waveshare CM4-IO-BASE-A EDK2 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = CM5WaveshareA
  PLATFORM_VENDOR                = ArmSoM
  PLATFORM_GUID                  = fc9a1540-d0bf-40dd-bb92-688f6b88563a
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/CM5WaveshareA.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  # Storage devices
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = TRUE   # CM5 module has onboard eMMC
  DEFINE RK_NOR_FLASH_ENABLE     = TRUE
  DEFINE RK_FVB_ENABLE           = TRUE
  DEFINE RK_RTC8563_ENABLE       = FALSE

  # Status LEDs (only module work-LED on GPIO0 PB4)
  DEFINE RK_STATUS_LED_ENABLE    = FALSE

  # Ethernet
  DEFINE RK3576_GMAC_ENABLE      = TRUE
  DEFINE RK_GMAC_ENABLE          = FALSE
  DEFINE RK3588_GMAC_ENABLE      = FALSE

  # PCIe (RK3576 native host bridge library)
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  DEFINE RK3588_PCIE_ENABLE      = FALSE

  # AHCI: RK3576 has no SATA controller
  DEFINE RK_AHCI_ENABLE          = FALSE

  # Display: RK3576 DW HDMI QP (HDMI 2.1)
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  DEFINE RK_DISPLAY_ENABLE       = TRUE

  # Non-OSI binaries not present for RK3576
  DEFINE RK_AMD_GOP_ENABLE       = FALSE

  # Secure Boot: UEFI image verification + Microsoft UEFI CA key enrolment
  DEFINE SECURE_BOOT_ENABLE      = TRUE

  # Networking
  DEFINE NETWORK_ENABLE          = TRUE
  DEFINE NETWORK_SNP_ENABLE      = TRUE
  DEFINE NETWORK_IP4_ENABLE      = TRUE
  DEFINE NETWORK_IP6_ENABLE      = TRUE
  DEFINE NETWORK_PXE_BOOT_ENABLE = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = TRUE
  DEFINE NETWORK_TLS_ENABLE      = TRUE
  DEFINE RK_X86_EMULATOR_ENABLE  = FALSE

  # SMBIOS system tables
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE

  # USB: DWC3 DRD0 (USBDP PHY; adapter connector) + DWC3 DRD1 (combphy1; FE1.1S hub HS)
  #   Note: Standard USB-A ports are HS-only via FE1.1S hub.
  #         USB3 SS is available only via the USB3/4 adapter connector (DRD0 lane).
  DEFINE RK_USB_ENABLE           = TRUE

  # Use RK3588 platform include (well-tested foundation); RK3576 PCDs override below
!include Silicon/Rockchip/RK3588/RK3588Platform.dsc.inc

################################################################################
[LibraryClasses.common]
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf
  PciHostBridgeLib|Silicon/Rockchip/RK3576/Library/Rk3576PciHostBridgeLib/Rk3576PciHostBridgeLib.inf
  PciLib|MdePkg/Library/BasePciLibPciExpress/BasePciLibPciExpress.inf
  PciExpressLib|MdePkg/Library/BasePciExpressLib/BasePciExpressLib.inf
  PciSegmentLib|Silicon/Rockchip/RK3576/Library/Rk3576PciSegmentLib/Rk3576PciSegmentLib.inf
  SdramLib|Silicon/Rockchip/RK3576/Library/SdramLib/SdramLib.inf
  RkSdmmcPlatformLib|Silicon/Rockchip/RK3576/Library/RkSdmmcPlatformLib/RkSdmmcPlatformLib.inf
  DwcSdhciPlatformLib|Silicon/Rockchip/RK3576/Library/DwcSdhciPlatformLib/DwcSdhciPlatformLib.inf
  OtpLib|Silicon/Rockchip/RK3576/Library/OtpLib/OtpLib.inf
  ResetSystemLib|Silicon/Rockchip/RK3576/Library/ResetSystemLib/ResetSystemLib.inf
  GpioLib|Silicon/Rockchip/RK3576/Library/GpioLib/GpioLib.inf

################################################################################
[PcdsFixedAtBuild.common]

  # Debug output (override RELEASE defaults to keep INFO visible)
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F

  # RK3576 system memory (2 GB base)
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x00000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  # SMBIOS identification
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"CM5-WAVESHARE-CM4A"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"ArmSoM"
  gRockchipTokenSpaceGuid.PcdFamilyName|"CM5"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://www.waveshare.com/cm4-io-base-a.htm"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-armsom-cm5-waveshare-cm4a"

  # UART0 (RK3576: 0x2AD40000, 1.5 Mbaud)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000

  # eMMC (sdhci @ 0x2A330000) — onboard on CM5 module
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000

  # SD card (sdmmc @ 0x2A310000)
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddress|0x2A310000

  # SPI NOR flash (sfc0 @ 0x2A340000)
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000
  gRockchipTokenSpaceGuid.FspiBaseAddr|0x2A340000

  # FVB: ATAGS firewalled on RK3576; skip lookup
  gRockchipTokenSpaceGuid.PcdRkAtagsBase|0
  # FVB: correct SPI NOR byte offset for NV variable store
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0xFC0000
  # Force SPI NOR as NV storage backend
  gRockchipTokenSpaceGuid.PcdNvStoragePreferSpiFlash|TRUE

  # USB DWC3: 0x23000000 (USBDP PHY, adapter connector), 0x23400000 (combphy1, FE1.1S hub)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
  # RK3576 has no EHCI/OHCI controllers
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  # I2C: PMIC on bus 1 (no RTC on carrier)
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # ComboPHY: PHY0 → PCIe (M.2 slot) | PHY1 → USB3 (DRD1 hub SS)
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)
  gRK3576TokenSpaceGuid.PcdComboPhy0Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy1Switchable|TRUE

  # Config table / FDT defaults: FDT-only (mainline DTS drives all peripherals)
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|0x00000002
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault|0
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|0x00000002
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|TRUE
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|TRUE

  # GMAC0: RTL8211F in rgmii-rxid mode (same as CM5-IO)
  #   tx_delay = 0x21 (MAC-side TX delay; PHY provides RX delay internally)
  #   GPIO note: reset GPIO assumed same as CM5-IO (GPIO2_PB3); verify with BASE-A schematic
  gRK3576TokenSpaceGuid.PcdGmac0Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0x21
  gRK3576TokenSpaceGuid.PcdGmac0RxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1Supported|FALSE
  gRK3576TokenSpaceGuid.PcdGmac1TxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1RxDelay|0

  # Network stack runtime defaults
  gRockchipTokenSpaceGuid.PcdNetworkStackEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv4EnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv6EnabledDefault|FALSE
  gRockchipTokenSpaceGuid.PcdNetworkStackPxeBootEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackHttpBootEnabledDefault|FALSE

  # CRU base (0x27200000)
  gRockchipTokenSpaceGuid.CruBaseAddr|0x27200000

  # GIC-400 (GICv2)
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x2A701000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x2A702000

  # Display: HDMI0 connector via HDPTX0 PHY
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_HDMI0
  })}

  # Default display mode: 2560x1440@60
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x13, 0x00, 0x00, 0x00 }

################################################################################
[PcdsDynamicHii.common.DEFAULT]
  gRK3576TokenSpaceGuid.PcdConfigTableMode|L"ConfigTableMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdConfigTableModeDefault
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatMode|L"AcpiPcieEcamCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtCompatMode|L"FdtCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtForceGop|L"FdtForceGop"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtForceGopDefault
  gRK3576TokenSpaceGuid.PcdFdtSupportOverrides|L"FdtSupportOverrides"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixup|L"FdtOverrideFixup"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideBasePath|L"FdtOverrideBasePath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }
  gRK3576TokenSpaceGuid.PcdFdtOverrideOverlayPath|L"FdtOverrideOverlayPath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }

  gRK3576TokenSpaceGuid.PcdComboPhy0Mode|L"ComboPhy0Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault
  gRK3576TokenSpaceGuid.PcdComboPhy1Mode|L"ComboPhy1Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault

  gRK3588TokenSpaceGuid.PcdDisplayModePreset|L"DisplayModePreset"|gRK3576DxeFormSetGuid|0x0|{0x13, 0x00, 0x00, 0x00}
  gRK3588TokenSpaceGuid.PcdDisplayModeCustom|L"DisplayModeCustom"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayConnectorsPriority|L"DisplayConnectorsPriority"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayForceOutput|L"DisplayForceOutput"|gRK3576DxeFormSetGuid|0x0|TRUE
  gRK3588TokenSpaceGuid.PcdDisplayDuplicateOutput|L"DisplayDuplicateOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayRotation|L"DisplayRotation"|gRK3576DxeFormSetGuid|0x0|0
  gRK3588TokenSpaceGuid.PcdHdmiSignalingMode|L"HdmiSignalingMode"|gRK3576DxeFormSetGuid|0x0|0

################################################################################
[BuildOptions]
  # SOC_RK3576 enables RK3576-specific code paths in DW HDMI QP, VOP2, and
  # Samsung USBDP PHY libraries (register windows differ from RK3588).
  GCC:*_*_AARCH64_CC_FLAGS = -DSOC_RK3576

################################################################################
[Components.common]
  # Board-specific Device Tree (Mainline — compiled from DTS at build time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor Device Tree (pre-compiled DTB from ArmSoM BSP kernel)
  # Once the DTB is present, uncomment the line below:
  # $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # FDT platform fixups (PCIe / SATA / VOP device tree nodes)
  Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf

!if $(RK3576_PCIE_ENABLE) == TRUE
  ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
  EmbeddedPkg/Drivers/NonCoherentIoMmuDxe/NonCoherentIoMmuDxe.inf
!endif

!if $(RK3576_GMAC_ENABLE) == TRUE
  Silicon/Rockchip/RK3576/Drivers/GmacPlatformDxe/GmacPlatformDxe.inf
  Silicon/Synopsys/DesignWare/Drivers/DwcEqosSnpDxe/DwcEqosSnpDxe.inf
!endif

