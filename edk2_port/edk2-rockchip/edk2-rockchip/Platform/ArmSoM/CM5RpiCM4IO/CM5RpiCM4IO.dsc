## @file
#
#  UEFI Platform Description for ArmSoM CM5 on RPi CM4 IO Board (RK3576)
#
#  The ArmSoM CM5 is a Raspberry-Pi CM4-form-factor compute module built on
#  the Rockchip RK3576 SoC.  This platform targets the Raspberry Pi CM4 IO
#  Board carrier (RP-008172-DS-1) which provides 2x HDMI, M.2 PCIe, GbE,
#  40-pin GPIO header, microSD, and USB 2.0 ports via USB hub.
#
#  IMPORTANT HARDWARE NOTES:
#    - Only ONE HDMI port works: CM5's single HDMI TX maps to the CM4 IO
#      Board HDMI0 connector.  HDMI1 on the carrier is non-functional with
#      CM5 (those CM4 pins carry CM5's USB3 SS signals).
#    - USB 3.0 SuperSpeed is not available on the RPi CM4 IO Board with CM5
#      (USB3 SS pins are repurposed for HDMI on the CM4 pinout).
#    - USB 2.0 HS works normally through the onboard USB hub.
#    - nRPIBOOT (CM4 pin 93) maps to CM5 SARADC_VIN0_BOOT -- not a USB boot
#      trigger on CM5.
#    - CM5 v1.2 has a hardware bug: pin 21 (LED_GREEN_EN / GPIO2_PD0)
#      is non-functional.  Fixed in CM5 v1.3.
#
#  Board:  ArmSoM CM5 + Raspberry Pi CM4 IO Board
#  SoC:    RK3576 (4xA72 + 4xA53, same silicon as Radxa ROCK 4D)
#  PMIC:   RK806 @ I2C1 0x23 (interrupt GPIO0 PA6)
#  RTC:    HYM8563 @ I2C2 0x51 (on CM5 module; interrupt GPIO0 PA0)
#  WiFi:   SYN43752 (rtl8852bs) via SDIO on CM5 module
#  BT:     SYN43752 via UART4 on CM5 module
#  Debug:  UART0 @ 0x2AD40000, 1.5 Mbaud
#
#  Differences vs CM5-IO platform:
#    - USB HOST 5V: always-on from carrier (no GPIO, vs CM5-IO GPIO4_PB0)
#    - USB OTG 5V:  always-on from carrier (no GPIO, vs CM5-IO GPIO2_PB6)
#    - No carrier LEDs used (CM4 IO Board uses its own ACT LED logic)
#    - PCIe/GMAC/HDMI GPIOs: identical to CM5-IO
#    - I2C5 on carrier: EMC2301 fan + PCF85063 RTC (OS-side only, not in UEFI)
#    - combphy0: PCIe (M.2 M-key slot) -- same as CM5-IO
#    - combphy1: USB3 (DRD1 USB-A) -- HS-only on RPi CM4 IO Board due to CM4
#      pin mapping repurposing SS lines for HDMI
#
#  GPIO verified from:
#    armbian/linux-rockchip (rk-6.1-rkr4.1):
#      rk3576-armsom-cm5-rpi-cm4-io.dts
#      rk3576-armsom-cm5.dtsi
#
#  Copyright (c) 2023-2025, Mario Balaniica <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ArmSoM CM5 EDK2 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = CM5RpiCM4IO
  PLATFORM_VENDOR                = ArmSoM
  PLATFORM_GUID                  = 0d286f96-1ad4-47e3-9d3f-170f9e8c911c
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/CM5RpiCM4IO.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  # Storage devices
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = TRUE   # CM5 module has onboard eMMC
  DEFINE RK_NOR_FLASH_ENABLE     = TRUE
  DEFINE RK_FVB_ENABLE           = TRUE
  DEFINE RK_RTC8563_ENABLE       = FALSE

  # Status LEDs (CM4 IO Board has its own LED logic; we drive module LED only)
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

  # Display: RK3576 DW HDMI QP (HDMI 2.1) -- only HDMI0 functional on CM4 IO Board
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

  # USB: DWC3 DRD0 @ 0x23000000 (USB-C, USBDP PHY SS+HS)
  #       DWC3 DRD1 @ 0x23400000 (USB-A, combphy1; HS-only on CM4 IO Board)
  #   Note: RPi CM4 IO connector repurposes USB3 SS lanes for HDMI -- no SS in practice.
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
  gRockchipTokenSpaceGuid.PcdPlatformName|"CM5-RPI-CM4-IO"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"ArmSoM"
  gRockchipTokenSpaceGuid.PcdFamilyName|"CM5"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://www.armsom.org/product-page/cm5"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-armsom-cm5-rpi-cm4-io"

  # UART0 (RK3576: 0x2AD40000, 1.5 Mbaud)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000

  # eMMC (sdhci @ 0x2A330000) -- onboard on CM5 module
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000

  # SD card (sdmmc @ 0x2A310000) -- microSD slot on CM4 IO Board carrier
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

  # USB DWC3: 0x23000000 (USB-C, USBDP PHY), 0x23400000 (USB-A, combphy1)
  # USB HOST/OTG 5V: both always-on from the CM4 IO Board carrier (no GPIO control needed)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
  # RK3576 has no EHCI/OHCI controllers
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  # I2C: PMIC on bus 1, RTC on bus 2
  # (carrier I2C5 devices EMC2301/PCF85063 are OS-side, not configured here)
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # RTC PCD owner is gated by RK_RTC8563_ENABLE=FALSE -- keep commented out
  # gPcf8563RealTimeClockLibTokenSpaceGuid.PcdI2cSlaveAddress|0x51
  # gRockchipTokenSpaceGuid.PcdRtc8563Bus|0x2

  # ComboPHY: PHY0 -> PCIe (M.2 slot) | PHY1 -> USB3 (DRD1 USB-A; HS-only on CM4 IO Board)
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
  #   rx_delay = 0    (PHY handles RX delay)
  #   reset GPIO: GPIO2_PB3 (active-low) -- same as CM5-IO
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
  # NOTE: Only HDMI0 is functional on the RPi CM4 IO Board with CM5
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
  # ACPI tables (Windows ARM64, FreeBSD, ACPI-capable OS)
  # Default: disabled -- FDT-only build.
  # To enable: uncomment both lines below, rebuild, and set ConfigTableMode to ACPI
  # in the UEFI front-page menu (or set PcdConfigTableModeDefault to 0x1 or 0x3).
  # $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Board-specific Device Tree (Mainline -- compiled from DTS at build time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor Device Tree (pre-compiled DTB from ArmSoM BSP kernel)
  # NOTE: Obtain rk3576-armsom-cm5-rpi-cm4-io.dtb from ArmSoM rockchip-kernel
  #   (branch: linux-6.1-stan-rkr6.1) and place at:
  #   devicetree/vendor/rk3576-armsom-cm5-rpi-cm4-io.dtb
  # Once the DTB is present, uncomment the line below:
  # $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # RK3576 ACPI platform driver -- disabled: FDT-only build.
  # Uncomment together with AcpiTables.inf above to enable ACPI mode.
  # Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # FDT platform fixups (PCIe / SATA / VOP device tree nodes)
  Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf

!if $(RK3576_PCIE_ENABLE) == TRUE
  ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
!endif

  $(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf
