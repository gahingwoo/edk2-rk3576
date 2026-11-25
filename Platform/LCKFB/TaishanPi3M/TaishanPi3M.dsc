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
  # The battery-backed RTC: HYM8563S (U71) at 0x51 on I2C2, schematic page
  # "RTC和FAN", battery CN2.  GetTime/SetTime use it through I2cDxe (on
  # below); the virtual clock stays as the fallback when it does not answer.
  DEFINE RK_RTC8563_ENABLE       = TRUE
  DEFINE RK_STATUS_LED_ENABLE    = FALSE
  DEFINE RK3576_GMAC_ENABLE      = TRUE
  # pcie0 is the mini-PCIe slot.  With the EC20 modem in it there is no PCIe
  # device and the link will not train; that is expected.
  DEFINE RK3576_PCIE_ENABLE      = TRUE
  # The slot can carry SATA too, but the AHCI path has never been built for
  # RK3576 here.
  DEFINE RK_AHCI_ENABLE          = FALSE
  DEFINE RK_DW_HDMI_QP_ENABLE    = TRUE
  DEFINE RK_DW_MIPI_DSI2_ENABLE  = TRUE   # the official 800x1280 MIPI DSI screen
  DEFINE RK_I2C_ENABLE           = TRUE   # touch (I2C0), PMIC (I2C1), RTC (I2C2)
  DEFINE RK_860X_REGULATOR_ENABLE = FALSE # no RK860x here; the PMIC is an RK806 on I2C1
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
  # Boot option "Armbian (U-Boot)".  Needs the SPL from scripts/build-spl.sh.
  DEFINE RK_ARMBIAN_ONCE_ENABLE  = TRUE

!include Silicon/Rockchip/RK3576/RK3576Base.dsc.inc

################################################################################
[LibraryClasses.common]
  RockchipPlatformLib|$(PLATFORM_DIRECTORY)/Library/RockchipPlatformLib/RockchipPlatformLib.inf

################################################################################
[PcdsFixedAtBuild.common]
  # The firmware leaves the USB-C port's VBUS off, so nothing can appear on
  # it: do not spend five seconds of every boot waiting for a device there.
  gRK3576TokenSpaceGuid.PcdUsbCDeviceWaitMs|0

  # Keep USB-A powered under Linux even with a device tree from the boot
  # partition that lacks regulator-always-on (older Armbian and plain
  # mainline trees for this board).
  gRK3576TokenSpaceGuid.PcdFdtUsbAVbusAlwaysOn|TRUE

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

  # FDT + ACPI both installed (0x3), as on the CM5-IO.  Linux keeps using the
  # mainline device tree; an ACPI OS gets AcpiTables/.  Switchable at runtime
  # in the front-page menu.  The ACPI tables have NOT been run with any
  # operating system on this board.
  gRK3576TokenSpaceGuid.PcdConfigTableModeDefault|$(CONFIG_TABLE_MODE_ACPI_FDT)

  # Hide \_SB.PCI0 from ACPI when the mini-PCIe link did not train in UEFI.
  # The slot normally carries a USB modem with nothing on PCIe, and then
  # PciHostBridgeInit.c leaves pcie0 held in reset; without this the DSDT
  # would still point the OS at that controller's DBI.  Off on the CM5-IO,
  # whose M.2 slot is the case the PCI0/PCI1 rules were measured with.
  gRK3576TokenSpaceGuid.PcdAcpiPcieRequireRootBridge|TRUE

  # 0x80000000 = NATIVE: follow the sink's EDID, as on the other two boards.
  gRK3588TokenSpaceGuid.PcdDisplayModePresetDefault|{ 0x00, 0x00, 0x00, 0x80 }

  #
  # The MIPI DSI screen first, then HDMI.  The screen is detected through its
  # touch controller (Tspi3mDsiPanel.c), so this order means: the screen when
  # it is plugged in, HDMI otherwise, and HDMI behaves as on a board that has
  # nothing else, forced output for a sink without hot-plug included.
  # "Display Connectors Priority" in setup changes the order.
  #
  gRK3588TokenSpaceGuid.PcdDisplayConnectors|{CODE({
    VOP_OUTPUT_IF_MIPI0,
    VOP_OUTPUT_IF_HDMI0
  })}

  #
  # HDMI audio: at ExitBootServices, mclk_sai6 = 12.288 MHz and the HDMI
  # controller set up for 48 kHz 2-channel L-PCM (DwHdmiQpAudio.c), and
  # \_SB.SAI6 published for the woa-rk3576 audio drivers (Sai.asl).
  # Nothing is written for a DVI sink or a sink whose EDID has no basic
  # audio.  Not run on hardware.
  #
  gRK3588TokenSpaceGuid.PcdHdmiAudioPreset|TRUE

  #
  # Analog audio: the ES8388 on I2C3 and SAI1 (headphone jack through an
  # amplifier, onboard microphone).  Tspi3mAudio.c powers PD_AUDIO, sets
  # mclk_sai1 = 12.288 MHz and the pins; the DSDT publishes \_SB.SAI1 and
  # \_SB.I2C3.ES83 for the woa-rk3576 audio drivers.  Not run on hardware.
  #
  gRK3576TokenSpaceGuid.PcdAnalogAudioPreset|TRUE

  #
  # I2C.  The base file lists the CM5-IO's devices; this board's are:
  #   I2C0 0x5d  the Goodix touch controller of the MIPI DSI screen
  #              (rk3576-lckfb-tspi-3m-dsi-c41409463.dtsi, &i2c0)
  #   I2C1 0x23  the RK806 PMIC (&i2c1 in rk3576-lckfb-tspi-3m.dts).  Listed
  #              so that I2cDxe sets that bus up (pins, clock divider) and
  #              maps it for runtime; Rk806I2cPowerOffDxe switches the PMIC
  #              off through it when an OS asks for a shutdown.
  #   I2C2 0x51  the HYM8563S RTC (&i2c2 rtc@51), for
  #              Hym8563RealTimeClockLib; mapped for runtime because Windows
  #              gets its time from it.  The FUSB302 at 0x22 shares this bus;
  #              nothing in the firmware talks to it, and the RTC library
  #              keeps off the bus at runtime unless the OS is Windows.
  #
  gRockchipTokenSpaceGuid.PcdI2cSlaveAddresses|{ 0x5d, 0x23, 0x51 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBuses|{ 0x0, 0x1, 0x2 }
  gRockchipTokenSpaceGuid.PcdI2cSlaveBusesRuntimeSupport|{ FALSE, TRUE, TRUE }

  # The AP6256's 32 kHz LPO is the HYM8563S CLKOUT (through R454/R198 to
  # module pin 24; &sdio_pwrseq ext_clock and the Bluetooth node's lpo clock
  # are both <&hym8563> in the DTS).  CLKOUT is battery-backed: if an OS ever
  # switched it off, WiFi and Bluetooth would stay dead.  Turn it back on at
  # boot (Hym8563RealTimeClockLib, register 0x0D only).
  gRockchipTokenSpaceGuid.PcdRtc8563ClkoutEnable|TRUE

  # PSCI SYSTEM_OFF only raises PWR_CTRL1, which the RK806 ignores unless
  # told otherwise; see Rk806I2cPowerOffDxe.
  gRK3576TokenSpaceGuid.PcdRk806I2cBase|0x2AC40000
  gRK3576TokenSpaceGuid.PcdRk806I2cAddress|0x23
  gRK3576TokenSpaceGuid.PcdRk806I2cBus|1

  # "CPU Maximum Frequency" (RK3576Dxe CpuMaxFreq.c) is offered here because
  # this is the one board whose PMIC the firmware has been seen to reach:
  # Rk806I2cPowerOffDxe reads CHIP_NAME 0x80 VER 0x6A over I2C1 at 0x23 on
  # every boot and its power-off works (2026-11-21).  vdd_cpu_big_s0 is
  # dcdc-reg1 (BUCK1) and vdd_cpu_lit_s0 dcdc-reg3 (BUCK3) under that pmic@23
  # in rk3576-lckfb-tspi-3m.dts, and U-Boot reports "SoC: RK3576", a plain
  # one.  Default stays 1608 MHz: Max has not been run on the board, and the
  # fan (PcdPwmFanSpeedDefault 100 below) is the only cooling Windows has.
  gRK3576TokenSpaceGuid.PcdCpuMaxFreqSupported|TRUE
  gRK3576TokenSpaceGuid.PcdCpuMaxFreqDefault|0

  # Log vdd_gpu_s0 (BUCK5 under the same pmic@23, dcdc-reg5 in
  # rk3576-lckfb-tspi-3m.dts) and the GPU OTP bytes at boot, and hand them
  # to the Windows GPU driver as the GpuRail variable (RK3576Dxe GpuRail.c).
  # Reads only.
  gRK3576TokenSpaceGuid.PcdGpuRailReport|TRUE

  # BUCK5 read 0x28 (750 mV) on this board on 2026-11-23; 702 MHz (SPLL / 1)
  # needs mainline's 775 mV for the 800 MHz operating point.  The setup
  # option "GPU voltage for 702 MHz (775 mV)" raises it to 775 mV and no
  # higher.  On by default on this board, as 537ea09 did unconditionally:
  # on 2026-11-24 FurMark at 702 MHz scored 334-336 against 288-290 at
  # 594 MHz and peaked at 49.9 C with the fan on.  Turn it off to keep the
  # PMIC's 750 mV (the Windows driver then stays at 594 MHz).
  gRK3576TokenSpaceGuid.PcdGpuRail775Supported|TRUE
  gRK3576TokenSpaceGuid.PcdGpuRail775Default|1

  # Start the fan on CN2 (PWM2 ch3, see PwmFanIoSetup) at full speed and
  # leave it running into the OS.  Windows has no driver for it and nothing
  # turns it down again.  60% was tried first: the fan ran, but the 2 kHz
  # switching was audible and louder than the fan at 100%, where the pin is
  # simply held high.
  gRK3576TokenSpaceGuid.PcdPwmFanSpeedDefault|100

  # 500000 ns = 2 kHz, what the device trees use.  This was 40000 (25 kHz),
  # never run on the board; measured since under the vendor kernel
  # (2026-11-16), 25 kHz stalls this fan at partial duty (59% sometimes
  # stopped, 39% never started), and 2 kHz runs at every duty.  Both the
  # firmware default and every ACPI fan level are 100%, where the pin is
  # held high and the period changes nothing; it only matters if a partial
  # level is ever brought back.
  gRK3576TokenSpaceGuid.PcdPwmFanPeriodNs|500000

  # TSADC read-out for the DSDT's thermal zones (Thermal.asl) and the
  # hardware thermal shutdown: a CRU (chip) reset at 120 C plus trim, armed
  # only after the readings check out (Rk3576TsadcLib.c).  No interrupt, no
  # reset write.
  gRK3576TokenSpaceGuid.PcdTsadcEnable|TRUE

  # Fan control by ACPI thermal zone (AcpiTables/Fan.asl): the setup option
  # exists on this board, and is off until the temperature read-out has been
  # checked on the board.
  gRK3576TokenSpaceGuid.PcdAcpiFanControlSupported|TRUE
  gRK3576TokenSpaceGuid.PcdAcpiFanControlDefault|0

  # SDIO Wi-Fi (AP6256, BCM43456) for an ACPI OS: the setup option exists on
  # this board and is off by default.  When on, Tspi3mWifi.c powers and probes
  # the card and the Wi-Fi SSDT (AcpiTables/Wifi.asl) is installed.  See
  # woa-rk3576 docs/WIFI.md.
  gRK3576TokenSpaceGuid.PcdAcpiSdioWifiSupported|TRUE
  # The 40-pin header: Tspi3mPin40.c describes it, AcpiTables has its UART
  # and RhProxy SSDTs.  Both setup options stay off by default.
  gRK3576TokenSpaceGuid.PcdPin40Supported|TRUE

################################################################################
[Components.common]
  # ACPI tables, installed alongside the DTB (PcdConfigTableModeDefault above)
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Patches the DSDT and MCFG at boot: PCI0/PCI1 _STA from the ComboPHY mode
  # and the root bridges UEFI produced, ECAM layout per OS, SD removability.
  # Does nothing unless ConfigTableMode has the ACPI bit.  Listed per board
  # because only the boards with ACPI tables carry it.
  Silicon/Rockchip/RK3576/Drivers/RK3576AcpiPlatformDxe/RK3576AcpiPlatformDxe.inf

  # Mainline device tree, built from DTS at compile time
  $(PLATFORM_DIRECTORY)/DeviceTree/Mainline.inf

  # Splash screen logo
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
  $(VENDOR_DIRECTORY)/Drivers/TouchKeysDxe/TouchKeysDxe.inf
  $(VENDOR_DIRECTORY)/Drivers/TouchKeysDxe/TouchTest.inf
  Silicon/Rockchip/RK3576/Drivers/Rk806I2cPowerOffDxe/Rk806I2cPowerOffDxe.inf

  # RK3576Dxe, FdtPlatformDxe, the PCIe stack and the GMAC stack are common to
  # every RK3576 board and are declared in RK3576Base.dsc.inc.
