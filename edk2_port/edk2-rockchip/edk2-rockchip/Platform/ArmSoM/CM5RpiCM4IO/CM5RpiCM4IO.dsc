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
#    - nRPIBOOT (CM4 pin 93) maps to CM5 SARADC_VIN0_BOOT — not a USB boot
#      trigger on CM5.
#    - CM5 v1.2 has a hardware bug: pin 21 (LED_GREEN_EN / GPIO2_PD0)
#      is non-functional.  Fixed in CM5 v1.3.
#
#  Board:  ArmSoM CM5 + Raspberry Pi CM4 IO Board
#  SoC:    RK3576 (4xA72 + 4xA53)
#  PMIC:   RK806 @ I2C1 0x23 (interrupt GPIO0 PA6)
#  WiFi:   SYN43752 (rtl8852bs) via SDIO on CM5 module
#  BT:     SYN43752 via UART4 on CM5 module
#  Debug:  UART0 @ 0x2AD40000, 1.5 Mbaud
#
#  Differences vs CM5-IO platform:
#    - USB HOST 5V: always-on from carrier (no GPIO, vs CM5-IO GPIO4_PB0)
#    - USB OTG 5V:  always-on from carrier (no GPIO, vs CM5-IO GPIO2_PB6)
#    - No carrier LEDs used (CM4 IO Board uses its own ACT LED logic)
#    - PCIe/GMAC/HDMI GPIOs: identical to CM5-IO
#    - RTC/fan on I2C5 (i2c5m3): EMC2301 fan + PCF85063 RTC (OS-side, not in UEFI)
#
#  GPIO verified from:
#    armbian/linux-rockchip (rk-6.1-rkr4.1):
#      rk3576-armsom-cm5-rpi-cm4-io.dts
#      rk3576-armsom-cm5.dtsi
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ArmSoM CM5 EDK2 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = CM5RpiCM4IO
  PLATFORM_VENDOR                = ArmSoM
  PLATFORM_GUID                  = a1b2c3d4-e5f6-4718-8c9d-0e1f2a3b4c5d
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

  # USB: DWC3 DRD0 (USBDP PHY; HS in UEFI, SS+HS in kernel) + DWC3 DRD1 (combphy1)
  #   Note: RPi CM4 IO connector repurposes USB3 SS lanes for HDMI — no SS on this carrier.
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
  # Platform identification
  gRockchipTokenSpaceGuid.PcdPlatformName|"CM5-RPI-CM4-IO"
  gRockchipTokenSpaceGuid.PcdFamilyName|"CM5"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://www.armsom.org/product-page/cm5"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-armsom-cm5-rpi-cm4-io"

  # UART0 @ 0x2AD40000 at 1.5 Mbaud (same as CM5-IO)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|1500000
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultDataBits|8
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultParity|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultStopBits|1

  # RK3576 specifics (same as CM5-IO)
  gRockchipTokenSpaceGuid.PcdCruBaseAddr|0x27200000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddr|0x2A310000

  # SPI NOR flash (same as ROCK 4D / CM5-IO)
  gRockchipTokenSpaceGuid.PcdSpiFlashBase|0xA0000000
  gRockchipTokenSpaceGuid.PcdSpiFlashSize|0x02000000
  gRockchipTokenSpaceGuid.PcdSfcBaseAddr|0x2A340000

  # GMAC0: RTL8211F in rgmii-rxid mode (same as CM5-IO)
  gRockchipTokenSpaceGuid.PcdGmacBaseAddr|0x28900000
  gRockchipTokenSpaceGuid.PcdGmacClkSel|0x1
  gRockchipTokenSpaceGuid.PcdGmacTxDelay|0x21
  gRockchipTokenSpaceGuid.PcdGmacRxDelay|0x00

  # USB
  gRockchipTokenSpaceGuid.PcdDwc3Usb2Addresses|{0x23000000, 0x23400000}
  gRockchipTokenSpaceGuid.PcdDwc3Usb3Addresses|{0x23400000}

################################################################################
[Components.common]
  $(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf
  # $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf  # Uncomment when vendor DTB available
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
