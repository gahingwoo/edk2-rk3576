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
  DEFINE RK_SD_ENABLE            = FALSE
  DEFINE RK_EMMC_ENABLE          = FALSE
  DEFINE RK_NOR_FLASH_ENABLE     = FALSE
  # FVB / NV-Variable stack: depends on RkAtagsLib reading SPL atags at the
  # hardcoded address 0x1FE000. On RK3576 that physical range is firewalled
  # by BL31 → external abort. Disable the entire FVB+VariableRuntimeDxe+FTW
  # stack until atags porting is done. Boot uses emulated variables instead.
  DEFINE RK_FVB_ENABLE           = FALSE
  DEFINE RK_RTC8563_ENABLE       = FALSE
  DEFINE RK_STATUS_LED_ENABLE    = FALSE
  # GMAC: disabled — RK3576 GMAC uses different IP, RK3588 driver not compatible
  DEFINE RK3576_GMAC_ENABLE      = FALSE
  DEFINE RK_GMAC_ENABLE          = FALSE
  DEFINE RK3588_GMAC_ENABLE      = FALSE
  # PCIe: disabled - no RK3576-native PciHostBridgeLib yet.
  # Rk3588PciHostBridgeLib touches RK3588-only MMIO -> crash on RK3576.
  DEFINE RK3576_PCIE_ENABLE      = FALSE
  DEFINE RK3588_PCIE_ENABLE      = FALSE
  # AHCI: RK3576 has no SATA controller
  DEFINE RK_AHCI_ENABLE          = FALSE
  # Display: RK3576 uses the same Synopsys DW HDMI QP (HDMI 2.1) IP as RK3588.
  # Mainline Linux dw-hdmi-qp.c binds against "rockchip,rk3576-dw-hdmi-qp".
  # Enable so DwHdmiQpLib (DXE driver) and the bundled HdptxHdmi PHY get linked.
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  # Non-OSI binaries not present for RK3576
  DEFINE RK_AMD_GOP_ENABLE       = FALSE
  DEFINE NETWORK_ENABLE          = FALSE
  DEFINE RK_X86_EMULATOR_ENABLE  = FALSE
  # Static SMBIOS tables (Type 0/1/2/3/4/7/9/11/16/17/19/32) — required so
  # UiApp banner shows "<NNNN> MB RAM" and OS sees system info.
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE
  # USB host: DWC3 controllers @ 0x23000000 + 0x23400000 (PCDs in
  # RK3576Base.dsc.inc). RK3576 has no EHCI/OHCI -> set count to 0 below.
  DEFINE RK_USB_ENABLE             = TRUE

  # Use RK3588 platform include (well-tested foundation)
  # RK3576-specific PCDs override below
!include Silicon/Rockchip/RK3588/RK3588Platform.dsc.inc

################################################################################
[LibraryClasses.common]
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf
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

  # USB DWC3 (0x23000000, 0x23400000)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
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
  # Board-specific Device Tree (Vendor = pre-compiled DTB)
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf
