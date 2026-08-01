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
  DEFINE RK_GMAC_ENABLE          = FALSE
  DEFINE RK3588_GMAC_ENABLE      = FALSE

  # PCIe (RK3576 native host bridge library)
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  DEFINE RK3588_PCIE_ENABLE      = FALSE

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

  # RK3576 DRAM starts at 0x40000000, not 0. These two only place the pre-MMU
  # SEC stack and HOB heap: the SEC entry stub derives
  #   UefiMemoryBase = (Base + Size) - PcdSystemMemoryUefiRegionSize (0x08000000)
  # so with 0x40000000 + 0x80000000 that lands at 0xB8000000, inside the
  # [0x40200000, 0xF0000000) window Rk3588Mem.c actually maps.
  #
  # The base used to be 0, which only worked because the size cap dragged the
  # derived top back into real DRAM by accident. That made the cap load-bearing
  # in a way nothing stated: raising it toward the true 4 GB would have put the
  # UEFI region above the mapped top and hung the board before the first print.
  # It is also why an earlier 1 GB size hung — 0x00000000 + 0x40000000 put the
  # region at 0x38000000, below DRAM entirely.
  #
  # Constraint that remains: Base + Size must stay <= the mapped DRAM top
  # (RK3576_LOW_DRAM_TOP, 0xF0000000). The real DRAM size is read at runtime by
  # SdramLib when the MMU map is built; it is not these PCDs.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  # I2C source clock. The RK3588 chain sets 198 MHz; on RK3576 clk_i2cN muxes
  # from mux_200m_100m_50m_24m_p (mainline clk-rk3576.c) and comes out of reset
  # on the 200 MHz parent, which is what the divider maths should use.
  gRockchipTokenSpaceGuid.PcdI2cClockFrequency|200000000

  # SMBIOS identification
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"CM5-IO"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"ArmSoM"
  gRockchipTokenSpaceGuid.PcdFamilyName|"CM5"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://www.armsom.org/product-page/cm5-io"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-armsom-cm5-io"

  # UART0 (RK3576: 0x2AD40000, 1.5 Mbaud)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x2AD40000

  # eMMC: sdhci @ 0x2A330000 on CM5 module.
  # HS200 and HS400 both require SDR104/DDR clock at 200 MHz which depends on
  # the DWC DLL and a CRU-frequency hand-off that the UEFI SDHCI stack cannot
  # synchronise correctly.  Even plain HighSpeed (~50 MHz, non-DLL sampling)
  # turned out marginal when the controller is brought up cold from an SD/SPI
  # boot (no from-eMMC stage tuned its phase): the command line works but 8-bit
  # data reads CRC and enumeration stalls.  ForceDefaultSpeed caps at 26 MHz
  # legacy SDR — widest sampling window, no DLL, no tuning, reliable.  eMMC is
  # secondary storage here; this trades throughput for a board that always boots.
  gRK3576TokenSpaceGuid.PcdSdhciBaseAddr|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciBaseAddress|0x2A330000
  gRockchipTokenSpaceGuid.PcdDwcSdhciForceDefaultSpeed|TRUE
  gRockchipTokenSpaceGuid.PcdDwcSdhciNonDllStrbinDelay|0xa   # RK3576: per U-Boot rk3576_data.ddr50_strbin_delay_num

  # SD card (sdmmc @ 0x2A310000) — slot on CM5-IO carrier board
  gRK3576TokenSpaceGuid.PcdSdmmcBaseAddr|0x2A310000
  gRockchipTokenSpaceGuid.PcdRkSdmmcBaseAddress|0x2A310000

  # SPI NOR flash (sfc0 @ 0x2A340000)
  gRK3576TokenSpaceGuid.PcdFspiBaseAddr|0x2A340000
  gRockchipTokenSpaceGuid.FspiBaseAddr|0x2A340000

  # FVB: ATAGS firewalled on RK3576; skip lookup
  gRockchipTokenSpaceGuid.PcdRkAtagsBase|0
  # FVB / NV variable store.
  #
  # This carrier's SPI NOR is 64 KB — too small for the 192 KB NV store, and in
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

  # USB DWC3: 0x23000000 (USB-C, USBDP PHY), 0x23400000 (USB-A, combphy1)
  gRockchipTokenSpaceGuid.PcdDwc3BaseAddresses|{ UINT32(0x23000000), UINT32(0x23400000) }
  # RK3576 has no EHCI/OHCI controllers
  gRockchipTokenSpaceGuid.PcdNumEhciController|0

  # I2C: PMIC on bus 1, RTC on bus 2
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorAddresses|{ 0x23 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorBuses|{ 0x1 }
  gRockchipTokenSpaceGuid.PcdRk860xRegulatorTags|{ 0 }

  # RTC PCD owner is gated by RK_RTC8563_ENABLE=FALSE — keep commented out
  # gPcf8563RealTimeClockLibTokenSpaceGuid.PcdI2cSlaveAddress|0x51
  # gRockchipTokenSpaceGuid.PcdRtc8563Bus|0x2

  # ComboPHY: PHY0 → PCIe (M.2 slot) | PHY1 → USB3 (DRD1 USB-A)
  gRK3576TokenSpaceGuid.PcdComboPhy0ModeDefault|$(COMBO_PHY_MODE_PCIE)
  gRK3576TokenSpaceGuid.PcdComboPhy1ModeDefault|$(COMBO_PHY_MODE_USB3)
  gRK3576TokenSpaceGuid.PcdComboPhy0Switchable|TRUE
  gRK3576TokenSpaceGuid.PcdComboPhy1Switchable|TRUE

  # Config table / FDT defaults: FDT + ACPI both installed (0x3).
  # Linux keeps using the mainline DTS; ACPI-only OSes (Windows on ARM,
  # FreeBSD) get the RK3576 tables. Switchable in the front-page menu.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|0x00000003
  gRK3576TokenSpaceGuid.PcdAcpiPcieEcamCompatModeDefault|0
  gRK3576TokenSpaceGuid.PcdFdtCompatModeDefault|0x00000002
  gRK3576TokenSpaceGuid.PcdFdtForceGopDefault|TRUE
  gRK3576TokenSpaceGuid.PcdFdtSupportOverridesDefault|FALSE
  gRK3576TokenSpaceGuid.PcdFdtOverrideFixupDefault|TRUE

  # GMAC0: RTL8211F in rgmii-rxid mode
  #   tx_delay = 0x21 (MAC-side TX delay; PHY provides RX delay internally)
  #   rx_delay = 0    (PHY handles RX delay)
  #   reset GPIO: GPIO2_PB3 (active-low)
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

  # Display: HDMI0 connector via HDPTX0 PHY (same as ROCK 4D)
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_HDMI0
  })}

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

  # RK3576 SoC DXE driver
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

  # RK3576 ACPI platform driver — patches MCFG/IORT and the PCI0/PCI1 _STA
  # from the live ComboPHY mode; runs only when ConfigTableMode has the ACPI bit.
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

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
