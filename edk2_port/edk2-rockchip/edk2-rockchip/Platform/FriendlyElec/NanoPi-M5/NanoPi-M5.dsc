## @file
#
#  UEFI Platform Description for FriendlyElec NanoPi M5 (RK3576)
#
#  Board:  FriendlyElec NanoPi M5
#  SoC:    RK3576 (4×A72 + 4×A53)
#  PMIC:   RK806 @ I2C1 0x23
#  RTC:    HYM8563 @ I2C2 0x51
#  Debug:  UART0 @ 0x2AD40000, 1.5Mbaud
#
#  Key differences from ROCK 4D (same SoC, RK3576):
#    - 2x 1Gbps Ethernet (GMAC0 + GMAC1, both RTL8211F RGMII-ID)
#    - 3x GPIO LEDs (SYS / LED1 / LED2)
#    - Optional UFS 2.0 storage (managed by SPL; not directly accessible via UEFI)
#    - M.2 E-Key slot for SDIO WiFi (no M.2 PCIe WiFi)
#    - USB-C PD power input (6V–20V); 2x USB-A Type-A host ports
#    - PCB: 90×62mm
#
#  MAINLINE STATUS (as of May 2026):
#    - rk3576-nanopi-m5.dts not yet in mainline Linux
#    - FriendlyElec vendor kernel branch: nanopi6-v6.1.y
#    - U-Boot: FriendlyElec fork, branch nanopi5-v2017.09
#    - Device Tree compatible: "friendlyelec,nanopi-m5", "rockchip,rk3576"
#    - GPIO pinout verified from FriendlyElec schematic + U-Boot DTS
#    - TODO: submit upstream DTS when RK3576 baseline stabilizes
#
#  Copyright (c) 2014-2018, Linaro Limited. All rights reserved.
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, FriendlyElec NanoPi M5 EDK2 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = NanoPi-M5
  PLATFORM_VENDOR                = FriendlyElec
  PLATFORM_GUID                  = d4e5f6a7-b8c9-4d0e-1f2a-3b4c5d6e7f80
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/NanoPi-M5.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  DEFINE RK_SD_ENABLE              = TRUE
  DEFINE RK_EMMC_ENABLE            = FALSE   # NanoPi M5 has optional UFS 2.0, no onboard eMMC
  DEFINE RK_NOR_FLASH_ENABLE       = TRUE
  DEFINE RK_FVB_ENABLE             = TRUE
  DEFINE RK_RTC8563_ENABLE         = FALSE   # TODO: enable once RK3576 I2C+RTC combination is tested
  DEFINE RK_STATUS_LED_ENABLE      = FALSE
  DEFINE RK3576_GMAC_ENABLE        = TRUE
  DEFINE RK_GMAC_ENABLE            = FALSE
  DEFINE RK3588_GMAC_ENABLE        = FALSE
  DEFINE RK3576_PCIE_ENABLE        = TRUE
  DEFINE RK3588_PCIE_ENABLE        = FALSE
  DEFINE RK_AHCI_ENABLE            = FALSE   # RK3576 has no SATA controller
  DEFINE RK_DW_HDMI_QP_ENABLE      = TRUE    # DW HDMI QP (shared with RK3588, supports RK3576 via SOC_RK3576)
  DEFINE RK_AMD_GOP_ENABLE         = FALSE
  DEFINE SECURE_BOOT_ENABLE        = TRUE
  DEFINE NETWORK_ENABLE            = TRUE
  DEFINE NETWORK_SNP_ENABLE        = TRUE
  DEFINE NETWORK_IP4_ENABLE        = TRUE
  DEFINE NETWORK_IP6_ENABLE        = TRUE
  DEFINE NETWORK_PXE_BOOT_ENABLE   = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE  = TRUE
  DEFINE NETWORK_TLS_ENABLE        = TRUE
  DEFINE RK_X86_EMULATOR_ENABLE    = FALSE
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE
  DEFINE RK_USB_ENABLE             = TRUE

  # Use RK3588 platform include as foundation (same as ROCK4D approach)
  # RK3576-specific PCDs override below.
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

  # Debug output visibility (same as ROCK4D: INFO + ERROR + WARN + LOAD)
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F

  # System memory layout (override RK3588Base 1GB default with 2GB)
  # NanoPi M5 comes in 4GB/8GB/16GB variants; 2GB is safe boot minimum.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x00000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  # SMBIOS
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"NanoPi M5"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"FriendlyElec"
  gRockchipTokenSpaceGuid.PcdFamilyName|"NanoPi"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://wiki.friendlyelec.com/wiki/index.php/NanoPi_M5"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-nanopi-m5"

  # UART0 base address (RK3576: 0x2AD40000)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000

  # eMMC controller (SDHCI @ 0x2A330000 — disabled on NanoPi M5, but keep addr)
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000

  # SD card controller (SDMMC @ 0x2A310000)
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddress|0x2A310000

  # SPI NOR flash (SFC0 @ 0x2A340000 — 16MB)
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000
  gRockchipTokenSpaceGuid.FspiBaseAddr|0x2A340000
  gRockchipTokenSpaceGuid.PcdRkAtagsBase|0
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0xFC0000

  # USB DWC3 — same as ROCK4D: DRD1 @ 0x23400000 (USB-A host pair)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23400000) }
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  # I2C — RK806 PMIC @ I2C bus 1, addr 0x23; HYM8563 RTC @ I2C bus 2, addr 0x51
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # ComboPHY: PHY0 → PCIe (M.2 M-Key slot); PHY1 → USB3 (USB-A port)
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)
  gRK3576TokenSpaceGuid.PcdComboPhy0Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy1Switchable|TRUE

  # ConfigTable / FDT defaults (RK3576Base.dsc.inc not in include chain)
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|0x00000001
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault|0
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|0x00000001
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|TRUE

  #
  # GMAC — NanoPi M5 has 2x 1Gbps Ethernet
  # Both use RTL8211F in RGMII-ID mode (PHY supplies both TX and RX delays)
  # → set SoC-side delay to 0 (RTL8211F enables RGMII-ID internally via PHYAD strapping)
  #
  gRK3576TokenSpaceGuid.PcdGmac0Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac0RxDelay|0
  # GMAC1: second Ethernet — KEY difference from ROCK4D (which has GMAC1 disabled)
  gRK3576TokenSpaceGuid.PcdGmac1Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGmac1TxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1RxDelay|0

  # Network stack runtime defaults
  gRockchipTokenSpaceGuid.PcdNetworkStackEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv4EnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv6EnabledDefault|FALSE
  gRockchipTokenSpaceGuid.PcdNetworkStackPxeBootEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackHttpBootEnabledDefault|FALSE

  # CRU base (RK3576: 0x27200000)
  gRockchipTokenSpaceGuid.CruBaseAddr|0x27200000

  # GIC-400 (GICv2) — from rk3576.dtsi: interrupt-controller@2a701000
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x2A701000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x2A702000

  # Display: single HDMI output via DW HDMI QP (HDMI0 connector)
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_HDMI0
  })}

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

  # ComboPHY mode selection — HII formid 0x1002
  gRK3576TokenSpaceGuid.PcdComboPhy0Mode|L"ComboPhy0Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault
  gRK3576TokenSpaceGuid.PcdComboPhy1Mode|L"ComboPhy1Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault

  # Display mode selection — HII formid 0x1000
  gRK3588TokenSpaceGuid.PcdDisplayModePreset|L"DisplayModePreset"|gRK3576DxeFormSetGuid|0x0|{0x0F, 0x00, 0x00, 0x00}
  gRK3588TokenSpaceGuid.PcdDisplayModeCustom|L"DisplayModeCustom"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayConnectorsPriority|L"DisplayConnectorsPriority"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayForceOutput|L"DisplayForceOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayDuplicateOutput|L"DisplayDuplicateOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayRotation|L"DisplayRotation"|gRK3576DxeFormSetGuid|0x0|0
  gRK3588TokenSpaceGuid.PcdHdmiSignalingMode|L"HdmiSignalingMode"|gRK3576DxeFormSetGuid|0x0|0

################################################################################
[BuildOptions]
  # SOC_RK3576 required for DW HDMI QP, VOP2, and HDPTX PHY driver #ifdefs
  GCC:*_*_AARCH64_CC_FLAGS = -DSOC_RK3576

################################################################################
[Components.common]
  # ACPI tables (Windows ARM64, FreeBSD, ACPI-capable Linux)
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Vendor Device Tree (pre-compiled from FriendlyElec BSP kernel)
  # NOTE: rk3576-nanopi-m5.dtb not yet available — Vendor.inf is a placeholder.
  # Build DTB from: https://github.com/friendlyarm/kernel-rockchip
  # Branch: nanopi6-v6.1.y  →  arch/arm64/boot/dts/rockchip/rk3576-nanopi-m5.dts
  # Place binary at: Platform/FriendlyElec/NanoPi-M5/devicetree/vendor/rk3576-nanopi-m5.dtb
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo (shared FriendlyElec LogoDxe)
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # RK3576 ACPI platform driver
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # FDT platform fixups (PCIe/SATA/VOP device tree nodes)
  Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf

!if $(RK3576_PCIE_ENABLE) == TRUE
  ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
  EmbeddedPkg/Drivers/NonCoherentIoMmuDxe/NonCoherentIoMmuDxe.inf
!endif

  # Simple Framebuffer GOP (uses VOP2 FB set up by U-Boot / SPL)
  Silicon/Rockchip/RK3576/Drivers/RK3576SimpleFbDxe/RK3576SimpleFbDxe.inf

!if $(RK3576_GMAC_ENABLE) == TRUE
  # GMAC0 + GMAC1 platform initializer (sdgmac_grf, PHY reset, MDIO + RGMII)
  Silicon/Rockchip/RK3576/Drivers/GmacPlatformDxe/GmacPlatformDxe.inf

  # DWC EQoS SNP driver (same IP block on RK3576 and RK3588)
  Silicon/Synopsys/DesignWare/Drivers/DwcEqosSnpDxe/DwcEqosSnpDxe.inf
!endif
