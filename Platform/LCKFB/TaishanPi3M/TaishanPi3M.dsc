## @file
#
#  UEFI platform description for the LCKFB TaishanPi 3M (RK3576)
#
#  Early support.  Boots to the UEFI front page (2026-11-12); has not booted an
#  operating system.  The board was provided by LCKFB.
#
#  Board:  LCKFB TaishanPi 3M
#  SoC:    RK3576 (4x A72 + 4x A53)
#  PMIC:   RK806 on I2C1, 0x23
#  RTC:    HYM8563 on I2C2, 0x51
#  Debug:  UART0 at 0x2AD40000, 1500000 8N1
#
#  Everything the SoC decides is in Silicon/Rockchip/RK3576/RK3576Base.dsc.inc.
#  The values below start from the ArmSoM CM5-IO, the other board here that
#  boots from eMMC or SD, and each one says whether it has been checked for
#  this board.
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = TaishanPi3M
  PLATFORM_VENDOR                = LCKFB
  PLATFORM_GUID                  = 6e0c9b54-1f7a-4d32-8c5e-b2a94d7f1e03
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/TaishanPi3M.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  # Same feature set as the CM5-IO, which is the combination that is known to
  # build and run for a board without a usable SPI NOR.  RK_NOR_FLASH_ENABLE
  # stays TRUE for that reason only: this board has no SPI NOR, and the NV
  # store is on the boot medium (see the FVB PCDs below).  Turning it off is a
  # separate change with its own build to check.
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = TRUE
  DEFINE RK_NOR_FLASH_ENABLE     = TRUE
  DEFINE RK_FVB_ENABLE           = TRUE
  DEFINE RK_RTC8563_ENABLE       = FALSE
  DEFINE RK_STATUS_LED_ENABLE    = FALSE
  DEFINE RK3576_GMAC_ENABLE      = TRUE
  # pcie0 is the mini-PCIe slot.  With the EC20 modem in it there is no PCIe
  # device and the link will not train; that is expected.
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  # The slot can carry SATA too, but the AHCI path has never been built for
  # RK3576 here.
  DEFINE RK_AHCI_ENABLE          = FALSE
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  DEFINE RK_DISPLAY_ENABLE       = TRUE
  DEFINE RK_AMD_GOP_ENABLE       = FALSE
  DEFINE SECURE_BOOT_ENABLE      = TRUE

  DEFINE NETWORK_ENABLE          = TRUE
  DEFINE NETWORK_SNP_ENABLE      = TRUE
  DEFINE NETWORK_IP4_ENABLE      = TRUE
  DEFINE NETWORK_IP6_ENABLE      = TRUE
  DEFINE NETWORK_PXE_BOOT_ENABLE = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = FALSE
  DEFINE NETWORK_TLS_ENABLE      = FALSE
  DEFINE RK_REALTEK_UNDI_ENABLE  = FALSE
  DEFINE RK_X86_EMULATOR_ENABLE  = FALSE
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE
  DEFINE RK_USB_ENABLE           = TRUE

!include Silicon/Rockchip/RK3576/RK3576Base.dsc.inc

################################################################################
[LibraryClasses.common]
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf

################################################################################
[PcdsFixedAtBuild.common]
  # SMBIOS identification
  gRockchipTokenSpaceGuid.PcdPlatformName|"TaishanPi 3M"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"LCKFB"
  gRockchipTokenSpaceGuid.PcdFamilyName|"TaishanPi"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://wiki.lckfb.com/"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-lckfb-tspi-3m"

  # eMMC speed.  These four are the CM5-IO's values, arrived at by measurement
  # on that board's eMMC (see CM5IO.dsc for the history).  The controller is
  # the same; the eMMC part and its routing are not.  NOT CHECKED on this board.
  gRockchipTokenSpaceGuid.PcdDwcSdhciForceDefaultSpeed|FALSE
  gRockchipTokenSpaceGuid.PcdDwcSdhciForceHighSpeed|TRUE
  gRockchipTokenSpaceGuid.PcdDwcSdhciNonDllStrbinDelay|0xa
  gRockchipTokenSpaceGuid.PcdDwcSdhciDisableSdma|FALSE

  # NV variable store on the boot medium.  These follow from the 32 MB SD/eMMC
  # image layout package.sh writes, not from the board: the FIT is at sector
  # 16384 (0x800000) and the store sits past it at 0x1600000.
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0x1600000
  gRockchipTokenSpaceGuid.PcdNvStoragePreferSpiFlash|FALSE
  gRockchipTokenSpaceGuid.PcdFitImageFlashAddress|0x800000

  # GMAC0 is rgmii-rxid with tx_delay = <0x24> in the device tree: the PHY
  # delays RX, the MAC delays TX.  The CM5-IO and the ROCK 4D both use 0 here
  # because their PHYs add both delays.  Here the MAE0621A's straps (PCB v1.4:
  # RXD0 pulled up = RXDLY on, RXD1 pulled down = TXDLY off) match that split.
  # NOT CHECKED on the board.
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0x24

  # FDT only.  No ACPI tables are built for this board.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_FDT)

  # 0x80000000 = NATIVE: follow the sink's EDID, as on the other two boards.
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x00, 0x00, 0x00, 0x80 }

################################################################################
[Components.common]
  # Mainline device tree, built from DTS at compile time
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576Dxe, FdtPlatformDxe, the PCIe stack and the GMAC stack are common to
  # every RK3576 board and are declared in RK3576Base.dsc.inc.
