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
#  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
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
  # PCIe: enabled via Rk3576PciHostBridgeLib (native RK3576 implementation).
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  # AHCI: RK3576 has no SATA controller
  DEFINE RK_AHCI_ENABLE          = FALSE
  # Display: RK3576 uses the same Synopsys DW HDMI QP (HDMI 2.1) IP as RK3588.
  # Mainline Linux dw-hdmi-qp.c binds against "rockchip,rk3576-dw-hdmi-qp".
  # Enable so DwHdmiQpLib (DXE driver) and the bundled HdptxHdmi PHY get linked.
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  # Enable the full display stack: Vop2Dxe + DwHdmiQpLib + LcdGraphicsOutputDxe.
  # Without this, FvMainModules.fdf.inc !if-gates those drivers out of the FV and
  # the physical HDMI link never comes up (GOP is installed by RK3576SimpleFbDxe
  # but VOP2 hw / HDPTX PHY are never initialized).
  DEFINE RK_DISPLAY_ENABLE       = TRUE
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
  # USB hosts:
  #   DWC3 DRD0 @ 0x23000000 (USB-C, u2phy0 + Samsung USBDP combo PHY)
  #     UEFI phase: HS-only (UsbDpPhyDxe not ported to RK3576).
  #     Kernel phase: SS+HS (phy-rockchip-usbdp.c in mainline Linux inits USBDP PHY).
  #   DWC3 DRD1 @ 0x23400000 (USB-A, u2phy1 + combphy1) - SS + HS.
  # PCDs (DwcXhci base addresses) are set further down. RK3576 has no
  # EHCI/OHCI -> set count to 0 below.
  DEFINE RK_USB_ENABLE             = TRUE

!include Silicon/Rockchip/RK3576/RK3576Base.dsc.inc

################################################################################
[LibraryClasses.common]
  # The only library this board overrides.  The RK3576 replacements for
  # SdramLib / RkSdmmcPlatformLib / DwcSdhciPlatformLib / OtpLib /
  # ResetSystemLib / GpioLib / PCI used to be listed here on both boards --
  # they are the SoC's, not the board's, and now live in RK3576Base.dsc.inc.
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf

################################################################################
[PcdsFixedAtBuild.common]
  #
  # Everything the SoC decides -- memory map, UART, GIC, CRU, SD/eMMC/FSPI
  # bases, I2C, ComboPHY, FDT defaults, network defaults -- is in
  # Silicon/Rockchip/RK3576/RK3576Base.dsc.inc.  What follows is only what
  # actually differs between this board and the ArmSoM CM5-IO.
  #

  # SMBIOS identification
  gRockchipTokenSpaceGuid.PcdPlatformName|"ROCK 4D"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"Radxa"
  gRockchipTokenSpaceGuid.PcdFamilyName|"ROCK 4"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://radxa.com/products/rock4/4d"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-rock-4d"

  # NV variable store lives in this board's SPI NOR (16 MB), at the offset the
  # SPI layout reserves for it.  CM5-IO has only a 64 KB carrier SPI and has to
  # keep its store on the boot medium instead.
  gRockchipTokenSpaceGuid.PcdNvStoragePreferSpiFlash|TRUE
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0xFC0000

  # GMAC0 runs rgmii-id: the PHY applies both delays, so the MAC adds none.
  # CM5-IO uses rgmii-rxid with a 0x21 MAC-side TX delay.
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0

  # FDT only -- no ACPI tables are built into this image.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_FDT)

  # Default display mode: 0x13 = 2560x1440@60.
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x13, 0x00, 0x00, 0x00 }

################################################################################
[Components.common]
  # ACPI tables — disabled: FDT-only build.
  # $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Board-specific Device Tree (Vendor = pre-compiled DTB)
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Mainline Device Tree (built from DTS at compile time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # This board builds FDT-only, so RK3576AcpiPlatformDxe and AcpiTables.inf
  # are deliberately absent -- CM5-IO declares them, this one does not.
  #
  # RK3576Dxe, FdtPlatformDxe, the PCIe stack and the GMAC stack are common to
  # every RK3576 board and are declared in RK3576Base.dsc.inc.
