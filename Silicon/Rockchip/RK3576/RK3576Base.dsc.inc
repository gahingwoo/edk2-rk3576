## @file
#
#  Common platform description for RK3576 boards.
#
#  This replaces the `!include Silicon/Rockchip/RK3588/RK3588Platform.dsc.inc`
#  that both RK3576 boards used to carry.  Under that arrangement the RK3576
#  configuration was defined by subtraction: RK3588Base.dsc.inc set a value,
#  and if the board DSC forgot to override it, the board silently ran with
#  RK3588's.  Five library classes were never overridden at all, so both
#  boards linked RK3588's ArmPlatformLib, PlatformCruLib, MemoryInitPeiLib,
#  SaradcLib and Pcie30PhyLib.
#
#  Everything here is RK3576's own.  Values that both boards had been setting
#  identically (63 of the 73 they each declared) live here now; the boards
#  keep only what genuinely differs between them.
#
#  Copyright (c) 2014-2018, Linaro Limited. All rights reserved.
#  Copyright (c) 2021-2022, Rockchip Limited. All rights reserved.
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  DEFINE CONFIG_NO_DEBUGLIB      = FALSE

  #
  # Shared Defines
  #   - must be kept in sync with their associated headers
  #   - values must be hexadecimal if used in byte array PCDs
  #

  #
  # Silicon/Rockchip/RK3576/Include/VarStoreData.h
  #
  DEFINE COMBO_PHY_MODE_UNCONNECTED   = 0
  DEFINE COMBO_PHY_MODE_PCIE          = 1
  DEFINE COMBO_PHY_MODE_SATA          = 2
  DEFINE COMBO_PHY_MODE_USB3          = 3

  DEFINE CONFIG_TABLE_MODE_ACPI         =  0x00000001
  DEFINE CONFIG_TABLE_MODE_FDT          =  0x00000002
  DEFINE CONFIG_TABLE_MODE_ACPI_FDT     =  0x00000003

  DEFINE ACPI_PCIE_ECAM_COMPAT_MODE_AUTO        = 0x00000000
  DEFINE ACPI_PCIE_ECAM_COMPAT_MODE_SINGLE_DEV  = 0x00000001
  DEFINE ACPI_PCIE_ECAM_COMPAT_MODE_NXPMX6      = 0x00000002
  DEFINE ACPI_PCIE_ECAM_COMPAT_MODE_GRAVITON    = 0x00000004

  DEFINE FDT_COMPAT_MODE_UNSUPPORTED    = 0
  DEFINE FDT_COMPAT_MODE_VENDOR         = 1
  DEFINE FDT_COMPAT_MODE_MAINLINE       = 2

  DEFINE HDMI_SIGNALING_MODE_AUTO  = 0
  DEFINE HDMI_SIGNALING_MODE_DVI   = 1
  DEFINE HDMI_SIGNALING_MODE_HDMI  = 2

  #
  # Default peripheral support flags
  #
!ifndef RK_STATUS_LED_ENABLE
  DEFINE RK_STATUS_LED_ENABLE       = TRUE
!endif
!ifndef RK_NOR_FLASH_ENABLE
  DEFINE RK_NOR_FLASH_ENABLE        = TRUE
!endif
!ifndef RK_860X_REGULATOR_ENABLE
  DEFINE RK_860X_REGULATOR_ENABLE   = TRUE
!endif
!ifndef RK_SD_ENABLE
  DEFINE RK_SD_ENABLE               = TRUE
!endif
!ifndef RK_EMMC_ENABLE
  DEFINE RK_EMMC_ENABLE             = TRUE
!endif
!ifndef RK_ANALOGIX_DP_ENABLE
  DEFINE RK_ANALOGIX_DP_ENABLE      = FALSE
!endif
!ifndef RK_DW_HDMI_QP_ENABLE
  DEFINE RK_DW_HDMI_QP_ENABLE       = TRUE
!endif

  #
  # RK3576 has no SATA controller.
  #
!ifndef RK_AHCI_ENABLE
  DEFINE RK_AHCI_ENABLE             = FALSE
!endif

  #
  # RK3576-specific flags
  #
!ifndef RK3576_PCIE_ENABLE
  DEFINE RK3576_PCIE_ENABLE         = TRUE
!endif
!ifndef RK3576_GMAC_ENABLE
  DEFINE RK3576_GMAC_ENABLE         = TRUE
!endif

  #
  # Base platform description
  #
!include Silicon/Rockchip/Rockchip.dsc.inc

################################################################################
#
# Library Class section - Library Classes needed by all RK3576 platforms.
#
################################################################################

[LibraryClasses.common.SEC]
  SerialPortLib|Silicon/Rockchip/Library/Dw8250SerialPortLib/Dw8250SerialPortLib.inf

[LibraryClasses.common]
  # Custom reset handler
  ResetSystemLib|Silicon/Rockchip/RK3576/Library/ResetSystemLib/ResetSystemLib.inf

  # ACPI helpers
  AcpiLib|EmbeddedPkg/Library/AcpiLib/AcpiLib.inf

  # Debug UART library
  SerialPortLib|Silicon/Rockchip/Library/Dw8250SerialPortLib/DebugDw8250SerialPortLib.inf

  # SoC and memory descriptors.  Rk3576Mem.c is where the DRAM-base-0x40000000
  # map lives; it used to sit in RK3588/Library/PlatformLib/Rk3588Mem.c.
  ArmPlatformLib|Silicon/Rockchip/RK3576/Library/PlatformLib/PlatformLib.inf

  # PCI Support.  RK3576 has no PCIe 3.0 controller, so there is no
  # Pcie30PhyLib here -- the RK3588 chain used to pull one in unconditionally.
!if $(RK3576_PCIE_ENABLE) == TRUE
  PciExpressLib|MdePkg/Library/BasePciExpressLib/BasePciExpressLib.inf
  PciLib|MdePkg/Library/BasePciLibPciExpress/BasePciLibPciExpress.inf
  PciSegmentLib|Silicon/Rockchip/RK3576/Library/Rk3576PciSegmentLib/Rk3576PciSegmentLib.inf
  PciHostBridgeLib|Silicon/Rockchip/RK3576/Library/Rk3576PciHostBridgeLib/Rk3576PciHostBridgeLib.inf
!endif

  # RkSdmmcDxe dependencies
  RkSdmmcPlatformLib|Silicon/Rockchip/RK3576/Library/RkSdmmcPlatformLib/RkSdmmcPlatformLib.inf

  # DwcSdhciDxe dependencies
  DwcSdhciPlatformLib|Silicon/Rockchip/RK3576/Library/DwcSdhciPlatformLib/DwcSdhciPlatformLib.inf

  # DRAM size detection
  SdramLib|Silicon/Rockchip/RK3576/Library/SdramLib/SdramLib.inf

  # Other SoC-specific libraries.
  #
  # PlatformCruLib still carries RK3588's clock tree -- see the warning at the
  # top of Rk3576CruLib.c.  Nothing on RK3576 is known to reach it; it is here
  # to satisfy the library class.  Real RK3576 CRU programming is an open item.
  PlatformCruLib|Silicon/Rockchip/RK3576/Library/Rk3576CruLib/Rk3576CruLib.inf
  OtpLib|Silicon/Rockchip/RK3576/Library/OtpLib/OtpLib.inf
  GpioLib|Silicon/Rockchip/RK3576/Library/GpioLib/GpioLib.inf
  SaradcLib|Silicon/Rockchip/RK3576/Library/SaradcLib/SaradcLib.inf

[LibraryClasses.common.SEC]
  MemoryInitPeiLib|Silicon/Rockchip/RK3576/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf

###################################################################################################
# BuildOptions Section
###################################################################################################

[BuildOptions]
  GCC:*_*_*_PLATFORM_FLAGS = -I$(WORKSPACE)/Silicon/Rockchip/RK3576/Include -I$(WORKSPACE)/Silicon/Rockchip/Include

  # Selects the RK3576 code paths in the shared display drivers.  Every board
  # here is RK3576, so this is unconditional; the matching #else arms have been
  # removed from the sources rather than left as dead code.
  GCC:*_*_AARCH64_CC_FLAGS = -DSOC_RK3576

################################################################################
#
# Pcd Section - PCD entries common to all RK3576 platforms.
#
################################################################################

[PcdsFixedAtBuild.common]
  # System Memory.
  #
  # RK3576 DRAM starts at 0x40000000, not 0.  These two only place the pre-MMU
  # SEC stack and HOB heap: the SEC entry stub derives
  #   UefiMemoryBase = (Base + Size) - PcdSystemMemoryUefiRegionSize (0x08000000)
  # so 0x40000000 + 0x80000000 lands at 0xB8000000, inside the
  # [0x40200000, 0xF0000000) window Rk3576Mem.c actually maps.
  #
  # Base used to be 0 (inherited from RK3588, whose DRAM does start at 0), which
  # only worked because the size cap dragged the derived top back into real DRAM
  # by accident.  That made the cap load-bearing in a way nothing stated.
  #
  # Constraint that remains: Base + Size must stay <= the mapped DRAM top
  # (RK3576_LOW_DRAM_TOP, 0xF0000000).  Real DRAM size is read at runtime by
  # SdramLib when the MMU map is built; it is not these PCDs.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  # RK3576 CPU profile: 4x Cortex-A72 + 4x Cortex-A53
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1

  # SMBIOS platform config
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"Generic RK3576 platform"
  gRockchipTokenSpaceGuid.PcdMemoryVendorName|"TBD"

  # Debug output (override RELEASE defaults to keep INFO visible)
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000042
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F

  # I2C.  The RK3588 chain set 198 MHz; on RK3576 clk_i2cN muxes from
  # mux_200m_100m_50m_24m_p (mainline clk-rk3576.c) and comes out of reset on
  # the 200 MHz parent, which is what the divider maths should use.
  gRockchipTokenSpaceGuid.PcdI2cClockFrequency|200000000
  gRockchipTokenSpaceGuid.PcdI2cBaudRate|100000
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorMinVoltages|{ UINT32(550000), UINT32(550000) }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorMaxVoltages|{ UINT32(1050000), UINT32(1050000) }

  # PMIC on I2C1 @ 0x23, RTC on I2C2 @ 0x51
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # UART0 - Serial Terminal (RK3576: 0x2AD40000, 1.5 Mbaud)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000
  gRockchipTokenSpaceGuid.PcdSerialPortSendDelay|500000
  gRockchipTokenSpaceGuid.PcdUartClkInHz|24000000

  # CRU
  gRockchipTokenSpaceGuid.CruBaseAddr|0x27200000

  # SPI NOR flash (sfc0 @ 0x2A340000)
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000
  gRockchipTokenSpaceGuid.FspiBaseAddr|0x2A340000

  #
  # ARM Generic Interrupt Controller.
  # RK3576 has a GIC-400 (GICv2) -- there are no redistributors, so
  # PcdGicRedistributorsBase is deliberately not set here.
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x2A701000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x2A702000

  # GUID of the UI app
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }

  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE

  gEmbeddedTokenSpaceGuid.PcdMetronomeTickPeriod|1000

  #
  # DesignWare SD Controller (MSHC) - sdmmc @ 0x2A310000
  #
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddress|0x2A310000

  #
  # DesignWare SD/eMMC Controller (SDHCI) - sdhci @ 0x2A330000
  #
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000

  #
  # RK3576 has no EHCI/OHCI companion controllers.
  #
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  #
  # DWC3: DRD0 @ 0x23000000 (USB-C, USBDP PHY), DRD1 @ 0x23400000 (combphy1)
  #
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
  gRockchipTokenSpaceGuid.PcdDwc3Size|0x400000

  #
  # FVB: the ATAGS region is BL31-firewalled (TZASC) on RK3576 and any read
  # bus-faults (EC=0x25 External Abort).  0 tells RkAtagsLib to skip the lookup.
  #
  gRockchipTokenSpaceGuid.PcdRkAtagsBase|0

  #
  # PCIe/SATA/USB Combo PIPE PHY.
  # Both current boards wire combphy0 -> PCIe and combphy1 -> USB3.
  #
  gRK3576TokenSpaceGuid.PcdComboPhy0Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy1Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)

  #
  # ACPI / Device Tree support flags and default values.
  # PcdConfigTableModeDefault is board-level: CM5-IO ships ACPI+FDT for
  # Windows on Arm, ROCK 4D is FDT-only.
  #
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault|$(ACPI_PCIE_ECAM_COMPAT_MODE_AUTO)
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|$(FDT_COMPAT_MODE_MAINLINE)
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|TRUE
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|TRUE
  gRK3576TokenSpaceGuid.PcdFdtOverrideBasePathDefault|L""
  gRK3576TokenSpaceGuid.PcdFdtOverrideOverlayPathDefault|L""

  #
  # GMAC.  GMAC0 is used on both boards; GMAC1 is not brought out.
  # PcdGmac0TxDelay is board-level (PHY and trace lengths differ).
  #
  gRK3576TokenSpaceGuid.PcdGmac0Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGmac0RxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1Supported|FALSE
  gRK3576TokenSpaceGuid.PcdGmac1TxDelay|0
  gRK3576TokenSpaceGuid.PcdGmac1RxDelay|0

  #
  # Display support flags and default values.
  # Both boards drive HDMI0 through the HDPTX0 PHY.
  # PcdDisplayModePresetDefault is board-level.
  #
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_HDMI0
  })}
  gRK3588TokenSpaceGuid.PcdDisplayModeCustomDefault|{CODE({
    .OscFreq     = 25175,
    .HActive     = 640,
    .HFrontPorch = 16,
    .HSync       = 96,
    .HBackPorch  = 48,
    .HSyncActive = 0,
    .VActive     = 480,
    .VFrontPorch = 10,
    .VSync       = 2,
    .VBackPorch  = 33,
    .VSyncActive = 0,
  })}
  gRK3588TokenSpaceGuid.PcdDisplayForceOutputDefault|TRUE
  gRK3588TokenSpaceGuid.PcdDisplayDuplicateOutputDefault|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayRotationDefault|0
  gRK3588TokenSpaceGuid.PcdHdmiSignalingModeDefault|$(HDMI_SIGNALING_MODE_AUTO)

  #
  # Network stack runtime defaults
  #
  gRockchipTokenSpaceGuid.PcdNetworkStackEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv4EnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackIpv6EnabledDefault|FALSE
  gRockchipTokenSpaceGuid.PcdNetworkStackPxeBootEnabledDefault|TRUE
  gRockchipTokenSpaceGuid.PcdNetworkStackHttpBootEnabledDefault|FALSE

[PcdsPatchableInModule]
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|1500000

[PcdsDynamicDefault.common]
  #
  # Display
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|100
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|31
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|100
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|31

[PcdsDynamicHii.common.DEFAULT]
  #
  # Everything here binds to gRK3576DxeFormSetGuid -- the formset RK3576Dxe
  # actually publishes.  The RK3588 chain bound the same settings to
  # gRK3588DxeFormSetGuid, whose formset is not in this firmware at all.
  #

  #
  # ACPI / Device Tree
  #
  gRK3576TokenSpaceGuid.PcdConfigTableMode|L"ConfigTableMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdConfigTableModeDefault
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatMode|L"AcpiPcieEcamCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtCompatMode|L"FdtCompatMode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault
  gRK3576TokenSpaceGuid.PcdFdtForceGop|L"FdtForceGop"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtForceGopDefault
  gRK3576TokenSpaceGuid.PcdFdtSupportOverrides|L"FdtSupportOverrides"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixup|L"FdtOverrideFixup"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault
  gRK3576TokenSpaceGuid.PcdFdtOverrideBasePath|L"FdtOverrideBasePath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }
  gRK3576TokenSpaceGuid.PcdFdtOverrideOverlayPath|L"FdtOverrideOverlayPath"|gRK3576DxeFormSetGuid|0x0|{ 0x0 }

  #
  # PCIe/SATA/USB Combo PIPE PHY
  #
  gRK3576TokenSpaceGuid.PcdComboPhy0Mode|L"ComboPhy0Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault
  gRK3576TokenSpaceGuid.PcdComboPhy1Mode|L"ComboPhy1Mode"|gRK3576DxeFormSetGuid|0x0|gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault

  #
  # Display.
  # ConnectorsMask is written by RK3576Dxe (InitializeDisplayVariables), so it
  # has to be dynamic; |BS keeps it a boot-services-only variable.
  #
  gRK3588TokenSpaceGuid.PcdDisplayConnectorsMask|L"DisplayConnectorsMask"|gRK3576DxeFormSetGuid|0x0|0|BS
  gRK3588TokenSpaceGuid.PcdDisplayModePreset|L"DisplayModePreset"|gRK3576DxeFormSetGuid|0x0|{0x13, 0x00, 0x00, 0x00}
  gRK3588TokenSpaceGuid.PcdDisplayModeCustom|L"DisplayModeCustom"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayConnectorsPriority|L"DisplayConnectorsPriority"|gRK3576DxeFormSetGuid|0x0|{0x0}
  gRK3588TokenSpaceGuid.PcdDisplayForceOutput|L"DisplayForceOutput"|gRK3576DxeFormSetGuid|0x0|TRUE
  gRK3588TokenSpaceGuid.PcdDisplayDuplicateOutput|L"DisplayDuplicateOutput"|gRK3576DxeFormSetGuid|0x0|FALSE
  gRK3588TokenSpaceGuid.PcdDisplayRotation|L"DisplayRotation"|gRK3576DxeFormSetGuid|0x0|0
  gRK3588TokenSpaceGuid.PcdHdmiSignalingMode|L"HdmiSignalingMode"|gRK3576DxeFormSetGuid|0x0|0

################################################################################
#
# Components Section - modules needed by all RK3576 platforms.
#
################################################################################
[Components.common]
  # General platform manager
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # FDT platform fixups (PCIe / VOP device tree nodes)
  Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf

  #
  # PCI Support
  #
!if $(RK3576_PCIE_ENABLE) == TRUE
  ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  EmbeddedPkg/Drivers/NonCoherentIoMmuDxe/NonCoherentIoMmuDxe.inf
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf
!endif

  #
  # Rockchip GMAC Support
  #
!if $(RK3576_GMAC_ENABLE) == TRUE
  Silicon/Rockchip/RK3576/Drivers/GmacPlatformDxe/GmacPlatformDxe.inf
  Silicon/Synopsys/DesignWare/Drivers/DwcEqosSnpDxe/DwcEqosSnpDxe.inf {
    <PcdsFixedAtBuild>
      # 32-bit DMA limit
      gEmbeddedTokenSpaceGuid.PcdDmaDeviceOffset|0x00000000
      gEmbeddedTokenSpaceGuid.PcdDmaDeviceLimit|0xffffffff
  }
!endif
