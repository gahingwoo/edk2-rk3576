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
  # The battery-backed RTC: TCS8563 (U20) at 0x51 on I2C2 (ROCK 4D
  # schematic V1.12 p27, battery J6).
  # GetTime/SetTime use it through I2cDxe; the virtual clock stays as the
  # fallback when it does not answer.  See Hym8563RealTimeClockLib.c.
  DEFINE RK_RTC8563_ENABLE       = TRUE
  DEFINE RK_I2C_ENABLE           = TRUE
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

  # FDT + ACPI both installed (0x3), as on the CM5-IO and the TaishanPi 3M.
  # Linux keeps booting from the mainline device tree: with a real DTB
  # present, arm64 Linux only uses ACPI when told to (acpi=force), so its
  # behaviour does not change.  An ACPI OS (Windows on Arm) gets AcpiTables/.
  # Switchable at runtime in the front-page menu (ACPI / Device Tree).  The
  # ACPI tables have NOT been run with any operating system on this board.
  #
  # A variable store written by an older image can still hold
  # ConfigTableMode = Devicetree if that form was ever saved; the setting
  # shown in the menu is the one in force.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_ACPI_FDT)

  # Hide \_SB.PCI0 from ACPI when the PCIe link did not train in UEFI.  The
  # FPC connector needs an M.2 adapter, and with no adapter or no drive on it
  # PciHostBridgeInit.c leaves pcie0 held in reset; without this the DSDT
  # would still point the OS at that controller's DBI.  As on the
  # TaishanPi 3M; off on the CM5-IO, whose M.2 slot is the case the
  # PCI0/PCI1 rules were measured with.
  gRK3576TokenSpaceGuid.PcdAcpiPcieRequireRootBridge|TRUE

  # TSADC sampling and the hardware thermal shutdown: a CRU (chip) reset at
  # 120 C plus trim, armed only after the readings check out (Rk3576TsadcLib.c).
  # The same limit and mode rk3576.dtsi gives Linux; an OS without a TSADC
  # driver had no over-temperature protection at all before this.
  gRK3576TokenSpaceGuid.PcdTsadcEnable|TRUE

  #
  # I2C.  The base file lists only the RTC; this board's are:
  #   I2C1 0x23  the RK806 PMIC (&i2c1 pmic@23 in rk3576-rock-4d.dts).
  #              Listed so that I2cDxe sets that bus up (pins, clock
  #              divider) and maps it for runtime; Rk806I2cPowerOffDxe
  #              switches the PMIC off through it when an OS asks for a
  #              shutdown, and RK3576Dxe reads the CPU, NPU and GPU rails
  #              through it.
  #   I2C2 0x51  the TCS8563 RTC (&i2c2 rtc@51), for
  #              Hym8563RealTimeClockLib; mapped for runtime because Windows
  #              gets its time from it.
  # The ES8388 on I2C3 is not listed: nothing in the firmware talks to it,
  # and Rock4dAudio.c sets up its pins and clocks for the OS.
  #
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ TRUE, TRUE }

  # The RK806 on I2C1 at 0x23, as on the TaishanPi 3M: same part, bus,
  # address and rails (rk3576-rock-4d.dts pmic@23: vdd_cpu_big_s0 dcdc-reg1,
  # vdd_npu_s0 dcdc-reg2, vdd_cpu_lit_s0 dcdc-reg3, vdd_gpu_s0 dcdc-reg5),
  # and the same PWR_CTRL1 wiring (GPIO0_A3 net PMIC_PWR_CTRL1 to the RK806
  # PWRCTRL1 pin, schematic V1.12 pages 5 and 25).  PSCI SYSTEM_OFF only
  # raises PWR_CTRL1, which the RK806 ignores unless told otherwise; see
  # Rk806I2cPowerOffDxe.  Setting the base also makes the NPU option
  # available (RK3576Dxe Npu.c sets vdd_npu_s0 through it).  None of it has
  # run on a ROCK 4D.
  gRK3576TokenSpaceGuid.PcdRk806I2cBase|0x2AC40000
  gRK3576TokenSpaceGuid.PcdRk806I2cAddress|0x23
  gRK3576TokenSpaceGuid.PcdRk806I2cBus|1

  # "CPU Maximum Frequency" (RK3576Dxe CpuMaxFreq.c): offered, as on the
  # TaishanPi 3M, because the rails are the same BUCK1/BUCK3 of the same
  # RK806 (above).  Default 1608 MHz: Max has not been run on this board.
  gRK3576TokenSpaceGuid.PcdCpuMaxFreqSupported|TRUE
  gRK3576TokenSpaceGuid.PcdCpuMaxFreqDefault|0

  # Log vdd_gpu_s0 (BUCK5) and the GPU OTP bytes at boot, and hand them to
  # the Windows GPU driver as the GpuRail variable (RK3576Dxe GpuRail.c).
  # Reads only.  PcdGpuRailRaise775 stays FALSE: raising the rail was
  # approved for the TaishanPi 3M only.
  gRK3576TokenSpaceGuid.PcdGpuRailReport|TRUE

  # The fan header J16 (PWM2 ch5, GPIO4_C5; PwmFanIoSetup in this board's
  # RockchipPlatformLib.c): started at full speed and left running into the
  # OS, which has no fan driver.  On or off only, never a partial duty: a
  # pulsed fan whined on the TaishanPi 3M.  60000 ns is the vendor tree's
  # period (rk3576-rock-4d.dts pwm-fan); at 100% the pin is held high and the
  # period changes nothing.
  gRK3576TokenSpaceGuid.PcdPwmFanSpeedDefault|100
  gRK3576TokenSpaceGuid.PcdPwmFanPeriodNs|60000

  # Fan control by ACPI thermal zone (AcpiTables/Fan.asl, all levels 100%):
  # the setup option exists on this board and is off, as on the other two.
  gRK3576TokenSpaceGuid.PcdAcpiFanControlSupported|TRUE
  gRK3576TokenSpaceGuid.PcdAcpiFanControlDefault|0

  #
  # HDMI audio: at ExitBootServices, mclk_sai6 = 12.288 MHz and the HDMI
  # controller set up for 48 kHz 2-channel L-PCM (DwHdmiQpAudio.c), and
  # \_SB.SAI6 published for the woa-rk3576 audio drivers (Sai.asl), as on
  # the other two boards.  Not run on this board.
  #
  gRK3588TokenSpaceGuid.PcdHdmiAudioPreset|TRUE

  #
  # Analog audio: the ES8388 on I2C3 and SAI1 (the headphone jack J18 and
  # its microphone).  Rock4dAudio.c powers PD_AUDIO, sets mclk_sai1 =
  # 12.288 MHz and the pins; the DSDT publishes \_SB.SAI1 and \_SB.I2C3.ES83
  # for the woa-rk3576 audio drivers.  Not run on this board.
  #
  gRK3576TokenSpaceGuid.PcdAnalogAudioPreset|TRUE

  # Default display mode.  0x80000000 = NATIVE: follow the sink's EDID.
  #
  # This was 0x13, the predefined 2560x1440@60 entry, fixed regardless of what
  # is plugged in.  That dated from when ROCK 4D produced no HDMI signal at all
  # (HPD read low) and a mode had to be asserted blind.  The board drives HDMI
  # now, and a fixed 2K is wrong on any sink that cannot take it: on a 1080p
  # monitor it reports the resolution as too large and shows nothing.
  #
  # NATIVE degrades safely.  If the EDID cannot be read, LcdGraphicsOutputDxe
  # finds no usable mode and falls back to predefined mode 0, 640x480, which
  # every monitor displays -- a worse picture, not a blank one.
  #
  # CM5-IO went through the same thing for a different reason and is already
  # NATIVE; see the matching block in CM5IO.dsc.
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x00, 0x00, 0x00, 0x80 }

################################################################################
[Components.common]
  # ACPI tables, installed alongside the DTB (PcdConfigTableModeDefault above)
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Board-specific Device Tree (Vendor = pre-compiled DTB)
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Mainline Device Tree (built from DTS at compile time)
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # Powers the board off through the RK806 at a shutdown (PcdRk806I2c* above)
  Silicon/Rockchip/RK3576/Drivers/Rk806I2cPowerOffDxe/Rk806I2cPowerOffDxe.inf

  # Patches the DSDT and MCFG at boot: PCI0/PCI1 _STA from the ComboPHY mode
  # and the root bridges UEFI produced, ECAM layout per OS, SD removability.
  # Does nothing unless ConfigTableMode has the ACPI bit.  Listed per board
  # because only the boards with ACPI tables carry it.
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # RK3576Dxe, FdtPlatformDxe, the PCIe stack and the GMAC stack are common to
  # every RK3576 board and are declared in RK3576Base.dsc.inc.
