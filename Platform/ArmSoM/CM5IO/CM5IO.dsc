## @file
#
#  UEFI Platform Description for ArmSoM CM5-IO (RK3576)
#
#  The ArmSoM CM5 is a Raspberry-Pi CM4-form-factor compute module built on
#  the Rockchip RK3576 SoC.  The CM5-IO is the official carrier board that
#  provides full-size HDMI 2.1, GbE, USB-C PD (FUSB302), 2x USB-A 3.0/2.0,
#  a 40-pin RPi-compatible GPIO header, M.2 PCIe slot, and a MicroSD slot.
#
#  Board:  ArmSoM CM5-IO (CM5 module + CM5-IO carrier)
#  SoC:    RK3576 (4xA72 + 4xA53, same silicon as Radxa ROCK 4D)
#  PMIC:   RK806 @ I2C1 0x23 (interrupt GPIO0 PA6)
#  RTC:    HYM8563 @ I2C2 0x51 (interrupt GPIO0 PA0)
#  WiFi:   SYN43752 (reported as rtl8852bs) via SDIO (sdio/sdmmc1)
#  BT:     SYN43752 via UART4
#  Debug:  UART0 @ 0x2AD40000, 1.5 Mbaud
#
#  Key differences vs Radxa ROCK 4D (same RK3576):
#    - eMMC: onboard (CM5 module, sdhci @ 0x2A330000)  ← ENABLED here
#    - WiFi: onboard (CM5 module, SDIO sdmmc1)         ← powered in EarlyInit
#    - USB HOST 5V: GPIO4_PB0  (ROCK 4D: GPIO0_PD3)
#    - USB OTG 5V:  GPIO2_PB6  (ROCK 4D: GPIO2_PD2)
#    - PCIe reset:  GPIO2_PB1  (ROCK 4D: GPIO2_PB4)
#    - PCIe power:  GPIO0_PC3  (ROCK 4D: GPIO2_PD3)
#    - GMAC0 reset: GPIO2_PB3  (ROCK 4D: GPIO2_PB5)
#    - GMAC0 mode:  rgmii-rxid, tx_delay=0x21 (ROCK 4D: rgmii-id, 0/0)
#    - HDMI 5V:     always-on from carrier board (ROCK 4D: GPIO2_PB0)
#
#  GPIO verified from: ArmSoM rockchip-kernel (linux-6.1-stan-rkr6.1)
#    rk3576-armsom-cm5.dtsi + rk3576-armsom-cm5-io.dts + rk3576-rk806.dtsi
#
#  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
#  Copyright (c) 2025, ArmSoM CM5-IO EDK2 Port
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
[Defines]
  PLATFORM_NAME                  = CM5IO
  PLATFORM_VENDOR                = ArmSoM
  PLATFORM_GUID                  = c2d3e4f5-a6b7-4c8d-9e0f-1a2b3c4d5e6f
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  VENDOR_DIRECTORY               = Platform/$(PLATFORM_VENDOR)
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/CM5IO.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

  # Storage devices
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = TRUE   # HS400ES not implemented in UEFI SDHCI; HS400 disabled via PCD below
  DEFINE RK_NOR_FLASH_ENABLE     = TRUE
  DEFINE RK_FVB_ENABLE           = TRUE
  DEFINE RK_RTC8563_ENABLE       = FALSE

  # Status LEDs
  DEFINE RK_STATUS_LED_ENABLE    = FALSE

  # Ethernet
  DEFINE RK3576_GMAC_ENABLE      = TRUE

  # PCIe (RK3576 native host bridge library)
  DEFINE RK3576_PCIE_ENABLE      = TRUE

  # AHCI: RK3576 has no SATA controller
  DEFINE RK_AHCI_ENABLE          = FALSE

  # Display: RK3576 DW HDMI QP (HDMI 2.1) — same IP as ROCK 4D
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
  #
  # HTTP boot + TLS off: FVMAIN was at 95% full, and this board installs from
  # USB/eMMC rather than over the network.  HttpDxe was also one of the two
  # drivers seen requesting ConvertPages at a bogus 0x180000000.  Flip back on
  # if HTTPS boot is ever wanted -- it costs roughly a third of the remaining
  # FV headroom.
  #
  DEFINE NETWORK_HTTP_BOOT_ENABLE = FALSE
  DEFINE NETWORK_TLS_ENABLE      = FALSE
  #
  # No Realtek NIC on CM5-IO: GMAC0 goes to an on-module MotorComm YT8531.
  # (The DT and the EDK2 port both used to claim RTL8211F; the schematic says
  # otherwise -- U6601 = YT8531C-CA.)
  #
  DEFINE RK_REALTEK_UNDI_ENABLE  = FALSE
  DEFINE RK_X86_EMULATOR_ENABLE  = FALSE

  # SMBIOS system tables
  DEFINE RK_PLATFORM_SMBIOS_ENABLE = TRUE

  # USB: DWC3 DRD0 @ 0x23000000 (USB-C, USBDP PHY SS+HS)
  #       DWC3 DRD1 @ 0x23400000 (USB-A, combphy1 SS+HS)
  DEFINE RK_USB_ENABLE           = TRUE

!include Silicon/Rockchip/RK3576/RK3576Base.dsc.inc

################################################################################
[LibraryClasses.common]
  # The only library this board overrides.  Everything else -- PCI, SD/eMMC,
  # OTP, GPIO, reset, CRU, SDRAM -- is the SoC's and lives in RK3576Base.
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf

################################################################################
[PcdsFixedAtBuild.common]
  #
  # Everything the SoC decides -- memory map, UART, GIC, CRU, SD/eMMC/FSPI
  # bases, I2C, ComboPHY, FDT defaults, network defaults -- is in
  # Silicon/Rockchip/RK3576/RK3576Base.dsc.inc.  What follows is only what
  # actually differs between this carrier and ROCK 4D.
  #

  # SMBIOS identification
  gRockchipTokenSpaceGuid.PcdPlatformName|"CM5-IO"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"ArmSoM"
  gRockchipTokenSpaceGuid.PcdFamilyName|"CM5"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://www.armsom.org/product-page/cm5-io"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-armsom-cm5-io"

  # eMMC is onboard on the CM5 module (ROCK 4D has none).
  #
  # HS200 and HS400 both require SDR104/DDR clock at 200 MHz which depends on
  # the DWC DLL and a CRU-frequency hand-off that the UEFI SDHCI stack cannot
  # synchronise correctly.  Even plain HighSpeed (~50 MHz, non-DLL sampling)
  # turned out marginal when the controller is brought up cold from an SD/SPI
  # boot (no from-eMMC stage tuned its phase): the command line works but 8-bit
  # data reads CRC and enumeration stalls.  ForceDefaultSpeed caps at 26 MHz
  # legacy SDR -- widest sampling window, no DLL, no tuning, reliable.  eMMC is
  # secondary storage here; this trades throughput for a board that always boots.
  gRockchipTokenSpaceGuid.PcdDwcSdhciForceDefaultSpeed|TRUE
  gRockchipTokenSpaceGuid.PcdDwcSdhciNonDllStrbinDelay|0xa   # per U-Boot rk3576_data.ddr50_strbin_delay_num

  # FVB / NV variable store.
  #
  # This carrier's SPI NOR is 64 KB -- too small for the 192 KB NV store, and in
  # practice not even detected (SPL reports "unrecognized JEDEC id 00 00 00").
  # So NV has to live on the boot medium (SD/eMMC), which means:
  #
  #   - PreferSpiFlash must be FALSE, or RkFvbDxe keeps the SPI path and never
  #     falls back to the disk dump.
  #   - PcdFitImageFlashAddress must point at the FIT on the boot medium, or
  #     FvbCheckBootDiskDeviceHasFirmware reads offset 0, gets FDT_ERR_BADMAGIC,
  #     and the boot disk is never adopted ("Variable store changes will NOT
  #     persist!").  SD/eMMC layout puts the FIT at sector 16384 = 0x800000.
  #   - The NV offset is reused as the byte offset on that medium. It cannot stay
  #     at 0xFC0000, because on the 32 MB SD/eMMC layout that address falls
  #     *inside* the FIT payload (0x800000..0x1550000). 0x1600000 sits past the
  #     payload with room for the 192 KB store, still inside the 32 MB image.
  #
  gRockchipTokenSpaceGuid.PcdRkFvbNvStorageSpiOffset|0x1600000
  gRockchipTokenSpaceGuid.PcdNvStoragePreferSpiFlash|FALSE
  gRockchipTokenSpaceGuid.PcdFitImageFlashAddress|0x800000

  # GMAC0 goes to an on-module MotorComm YT8531 in rgmii-rxid mode:
  #   tx_delay = 0x21 (MAC-side TX delay; PHY provides RX delay internally)
  # ROCK 4D uses rgmii-id with 0/0.
  gRK3576TokenSpaceGuid.PcdGmac0TxDelay|0x21

  # FDT + ACPI both installed (0x3).  Linux keeps using the mainline DTS;
  # ACPI-only OSes (Windows on Arm, FreeBSD) get the RK3576 tables.
  # ROCK 4D is FDT-only.  Switchable at runtime in the front-page menu.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_ACPI_FDT)

  # Default display mode.
  #
  # BRING-UP OVERRIDE, not a considered default: 0x0F is 1920x1080@60, forced
  # because the HDMI capture card used as the objective instrument for the
  # intermittent-no-signal work only accepts 1080p. The comment here used to
  # claim 2560x1440@60 while the value said otherwise.
  #
  # Restore to 0x13 (2560x1440@60, matching ROCK 4D) or 0x80000000 (NATIVE,
  # follow the sink's EDID) once display bring-up stops depending on the
  # capture card. A 2K monitor accepts a 1080p input fine, so this is only
  # about the capture path, not about what the board can drive.
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x0F, 0x00, 0x00, 0x00 }

################################################################################
[Components.common]
  # ACPI tables (Windows ARM64, FreeBSD, ACPI-capable OS)
  # Installed alongside the DTB — see PcdConfigTableModeDefault (0x3) above.
  # ConfigTableMode is switchable at runtime in the UEFI front-page menu.
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Board-specific Device Tree (Mainline — compiled from DTS at build time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Vendor Device Tree (pre-compiled DTB from ArmSoM BSP kernel)
  # NOTE: Obtain rk3576-armsom-cm5-io.dtb from ArmSoM rockchip-kernel
  #   (branch: linux-6.1-stan-rkr6.1) and place at:
  #   devicetree/vendor/rk3576-armsom-cm5-io.dtb
  # Once the DTB is present, uncomment the line below:
  # $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # RK3576 ACPI platform driver — patches MCFG/IORT and the PCI0/PCI1 _STA
  # from the live ComboPHY mode; runs only when ConfigTableMode has the ACPI bit.
  # Board-level because only this carrier enables ACPI.
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # RK3576Dxe, FdtPlatformDxe, the PCIe stack and the GMAC stack are common to
  # every RK3576 board and are declared in RK3576Base.dsc.inc.
