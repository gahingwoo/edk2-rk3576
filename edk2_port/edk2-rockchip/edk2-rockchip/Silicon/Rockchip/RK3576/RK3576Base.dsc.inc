## @file
#
#  RK3576 Base Platform Description File
#  Shared definitions for all RK3576-based boards.
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ROCK 4D RK3576 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]
  DEFINE CONFIG_NO_DEBUGLIB     = FALSE

  # ComboPHY modes (same semantics as RK3588)
  DEFINE COMBO_PHY_MODE_UNCONNECTED   = 0
  DEFINE COMBO_PHY_MODE_PCIE          = 1
  DEFINE COMBO_PHY_MODE_SATA          = 2
  DEFINE COMBO_PHY_MODE_USB3          = 3

  # Config table modes
  DEFINE CONFIG_TABLE_MODE_ACPI       = 0x00000001
  DEFINE CONFIG_TABLE_MODE_FDT        = 0x00000002
  DEFINE CONFIG_TABLE_MODE_ACPI_FDT   = 0x00000003

  # FDT compatibility modes
  DEFINE FDT_COMPAT_MODE_UNSUPPORTED  = 0
  DEFINE FDT_COMPAT_MODE_VENDOR       = 1
  DEFINE FDT_COMPAT_MODE_MAINLINE     = 2

  # Default peripheral support flags
!ifndef RK_STATUS_LED_ENABLE
  DEFINE RK_STATUS_LED_ENABLE     = TRUE
!endif
!ifndef RK_NOR_FLASH_ENABLE
  DEFINE RK_NOR_FLASH_ENABLE      = TRUE
!endif
!ifndef RK_GMAC_ENABLE
  DEFINE RK_GMAC_ENABLE           = TRUE
!endif
!ifndef RK3576_GMAC_ENABLE
  DEFINE RK3576_GMAC_ENABLE       = TRUE
!endif
!ifndef RK_RTC8563_ENABLE
  DEFINE RK_RTC8563_ENABLE        = TRUE
!endif
!ifndef RK_SD_ENABLE
  DEFINE RK_SD_ENABLE             = TRUE
!endif
!ifndef RK_EMMC_ENABLE
  DEFINE RK_EMMC_ENABLE           = TRUE
!endif

  # SCMI clock IDs for RK3576 (from TF-A plat/rockchip/rk3576/include/platform_def.h)
  DEFINE SCMI_CLK_CPUL   = 0x0
  DEFINE SCMI_CLK_CPUB   = 0x1

# Include common Rockchip platform definitions
!include Silicon/Rockchip/Rockchip.dsc.inc

################################################################################
#
# Library Classes
#
################################################################################
[LibraryClasses.common]
  # Override SdramLib with RK3576-specific version
  # (Rockchip.dsc.inc maps it to the common lib which uses RK3588 register addr)
  SdramLib|Silicon/Rockchip/RK3576/Library/SdramLib/SdramLib.inf

  # PCI segment library for RK3576 PCIe controllers
  PciLib|MdePkg/Library/BasePciLibPciExpress/BasePciLibPciExpress.inf
  PciSegmentLib|Silicon/Rockchip/RK3576/Library/Rk3576PciSegmentLib/Rk3576PciSegmentLib.inf

################################################################################
#
# PCDs — RK3576-specific values
# Addresses from rk3576.dtsi (verified against mainline Linux + U-Boot)
#
################################################################################
[PcdsFixedAtBuild.common]

  # Processor name for SMBIOS
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"

  # UART — uart0: serial@2ad40000.
  # SPL & BL31 emit at 1.5Mbaud (rkbin compiled). The Dw8250SerialPortLib
  # SerialPortInitialize is a no-op (assumes prior bringup) and SetAttributes
  # returns UNSUPPORTED — so this PCD only seeds the UART_DEVICE_PATH used by
  # TerminalDxe binding. It MUST match the actual hardware divisor SPL set,
  # otherwise TerminalDxe's SimpleTextInput producer will not bind to ConIn.
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|1500000
  # Default lowered to 115200 so cheap CH340/CP2102 dongles can RX (F1/F2/Enter).
  # SPL & BL31 still emit at 1.5Mbaud, so flip terminal baud after the BL31
  # banner. Override in board DSC if you have FTDI/CP2104 capable of 1.5M RX.
  gArmPlatformTokenSpaceGuid.PL011UartClkInHz|24000000
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gArmPlatformTokenSpaceGuid.PL011UartInterrupt|0

  # DRAM base & size — capped at 2GB at build-time. Constraints:
  #   * mSystemMemoryEnd (= Base + Size - 1 = 0x7FFFFFFF) must be >= FdTop
  #     (= 0x40DFFFFF) so SecMain's IS_XIP()/range assert treats the FD as
  #     in-DRAM (matching RK3588 boot model).
  #   * The early SEC stack is placed at SystemMemoryBase + Size (= 0x80000000)
  #     before the MMU is up; it must land in real DRAM, well below the
  #     0xF0000000-0x100000000 MMIO hole.
  # Actual DRAM size is read dynamically by SdramLib in the MMU map.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x00000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000    # 2GB build-time cap

  # FD/FV layout — EDK2 is loaded as BL33 by TF-A directly into DRAM at
  # 0x40800000 (matches BL33_BASE in TF-A and Load Address in u-boot.itb).
  # All FV-internal absolute symbol references (e.g. _ModuleEntryPoint ->
  # CEntryPoint in PeilessSec) are resolved at link time using
  # PcdFvBaseAddress, so it MUST equal the actual runtime load address;
  # otherwise SEC silently jumps into an invalid 0x002xxxxx range.
  # FdBase is offset back by 0x200000 to preserve the FDF FV-at-FdBase+0x200000
  # layout (FV occupies bytes [0x200000, 0x700000) of the FD region).
  gArmTokenSpaceGuid.PcdFdBaseAddress|0x40600000
  gArmTokenSpaceGuid.PcdFdSize|0x00800000              # 8MB FD region in DRAM
  gArmTokenSpaceGuid.PcdFvBaseAddress|0x40800000       # = TF-A BL33_BASE
  gArmTokenSpaceGuid.PcdFvSize|0x00500000

  # CPU cluster count: 4×A72 (big) + 4×A53 (little)
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  # USB DWC3 controllers — usb_drd0@23000000, usb_drd1@23400000
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
  gRockchipTokenSpaceGuid.PcdDwc3Size|0x400000

  # eMMC — sdhci: mmc@2a330000
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000

  # SD card — sdmmc: mmc@2a310000
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000

  # SPI NOR Flash — sfc0: spi@2a340000
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000

  # I2C base addresses (I2C_BASE macro from Soc.h)
  gRockchipTokenSpaceGuid.PcdI2cClockFrequency|24000000
  gRockchipTokenSpaceGuid.PcdI2cBaudRate|100000

  # GMAC — gmac0 (RTL8211F PHY on ROCK 4D)
  gRockchipTokenSpaceGuid.PcdGmacBaseAddr|0x2BD40000

  # Config table: default to FDT (Device Tree) mode for Linux compatibility
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_FDT)
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|$(FDT_COMPAT_MODE_VENDOR)
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|0
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|1
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|1

  # RK3588 *Default PCDs: FixedAtBuild constants used as HII form factory defaults.
  gRK3588TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_FDT)
  gRK3588TokenSpaceGuid.PcdFdtCompatModeDefault|$(FDT_COMPAT_MODE_VENDOR)
  gRK3588TokenSpaceGuid.PcdFdtForceGopDefault|0
  gRK3588TokenSpaceGuid.PcdFdtSupportOverridesDefault|0
  gRK3588TokenSpaceGuid.PcdFdtOverrideFixupDefault|0

  # ComboPHY defaults (board DSC overrides these)
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)

[PcdsDynamicDefault.common]
  # RK3588 runtime PCDs read by FdtPlatformDxe via LibPcdGet32 (Dynamic access).
  # FdtPlatformDxe returns EFI_UNSUPPORTED when (PcdConfigTableMode & FDT) == 0.
  # These must be seeded in the Dynamic PCD database; FixedAtBuild has no effect.
  gRK3588TokenSpaceGuid.PcdConfigTableMode|$(CONFIG_TABLE_MODE_FDT)
  gRK3588TokenSpaceGuid.PcdFdtCompatMode|$(FDT_COMPAT_MODE_VENDOR)
  gRK3588TokenSpaceGuid.PcdFdtForceGop|0
  gRK3588TokenSpaceGuid.PcdFdtSupportOverrides|0
  gRK3588TokenSpaceGuid.PcdFdtOverrideFixup|0

################################################################################
#
# Components — ALL platform components (Silicon + Board level)
# NOTE: Must be in the !included file, not the top-level board DSC.
#       EDK2 only processes the FIRST [Components.common] encountered per DSC scope.
#
################################################################################
[Components.common]
  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # RK3576 SDRAM detection library
  Silicon/Rockchip/RK3576/Library/SdramLib/SdramLib.inf

  # RK3588 drivers reused by RK3576
  # Silicon/Rockchip/RK3588/Drivers/UsbDpPhyDxe/UsbDpPhyDxe.inf  # DISABLED: RK3588-specific
  Silicon/Rockchip/RK3588/Drivers/GmacPlatformDxe/GmacPlatformDxe.inf
  Silicon/Rockchip/RK3588/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  Silicon/Rockchip/RK3588/Drivers/FdtPlatformDxe/FdtPlatformDxe.inf
