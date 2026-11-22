/** @file
 *
 *  Differentiated System Description Table (DSDT)
 *  LCKFB TaishanPi 3M (RK3576)
 *
 *  Written from the CM5-IO DSDT (Platform/ArmSoM/CM5IO/AcpiTables/Dsdt.asl),
 *  which is the RK3576 DSDT that has booted Windows 11 23H2.  Every device the
 *  CM5-IO declares was checked against this board's mainline device tree,
 *  devicetree/mainline/rk3576-lckfb-tspi-3m.dts (Linux 7.3), cited below as
 *  "dts:<line>".  SoC addresses and interrupts are not repeated here; they
 *  are in the shared files under Silicon/Rockchip/RK3576/AcpiTables and were
 *  taken from rk3576.dtsi.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM ON THIS BOARD.  It compiles; that is all
 *  that is known.
 *
 *  Kept from the CM5-IO, because the device is the SoC's and the board does
 *  not change it: CPUs, PCIe root complexes, eMMC, SD, GPIO, DMA, UART2,
 *  I2C0..I2C9, SPI0..SPI4, GMAC0, the RK806 PMIC and the HYM8563 RTC.
 *
 *  Left out, on purpose:
 *
 *    - XHC2 and ATA0..ATA2.  The CM5-IO includes them as _STA = 0 stubs whose
 *      addresses are RK3588's; a disabled device with a wrong address adds
 *      nothing.
 *    - FUSB302 (I2C2 0x22, dts:636-673): no Windows driver, and nothing here
 *      runs a Type-C port manager.  The firmware reads it once at
 *      ExitBootServices for the USBDP lane mapping and the VBUS switch,
 *      then leaves Rp on the attached CC line with VBUS on (a sink found)
 *      or the chip reset with VBUS off (anything else).
 *    - WiFi on SDIO (dts:789-814): the CM5-IO does not declare it, there is
 *      no Windows driver for an SDIO BCM43456, and its power, reset and
 *      32 kHz clock are sequenced by Linux (sdio_pwrseq, dts:167-175).
 *      Bluetooth on UART6 (dts:853-873) is declared, see Bluetooth.asl.
 *    - Audio, unless asked for.  The mainline tree has HDMI audio on SAI6
 *      only (dts:315-323, 774-777); SAI6 is declared only when
 *      PcdHdmiAudioPreset is TRUE.  LCKFB's vendor tree adds an ES8388 on
 *      I2C3 and SAI1 (as Armbian's board patch carries it); SAI1 and the
 *      codec are declared only when PcdAnalogAudioPreset is TRUE.
 *    - The LED (GPIO0_A0, dts:47-52), the ADC key (dts:30-42) and HDMI: not
 *      declared on the CM5-IO either.
 *
 *  GPIO0_B6 is the USB-C VBUS switch on this board, not SDMMC0_PWREN
 *  (dts:825).  Nothing in this table, including the shared Sdhc.asl,
 *  declares or drives it.
 *
 *  Copyright (c) 2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2018-2020, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

DefinitionBlock ("Dsdt.aml", "DSDT", 2, "RKCP  ", "RK3576  ", 2)
{
  Scope (\_SB_)
  {
    // Declares nothing: records why RK3576 has no ACPI SCMI device.
    include ("DsdtCommon.asl")

    // CPU topology (4x Cortex-A53 + 4x Cortex-A72)
    include ("Cpu.asl")

    // PCIe root complexes.  pcie0 sits on combphy0, which this board routes
    // to the mini-PCIe slot (vcc_3v3_minipcie, dts:131-143; combphy0_ps,
    // dts:241-243).  The slot usually carries a USB modem, with nothing on
    // PCIe.  pcie1 would need combphy1, which is USB3 for DRD1 here
    // (combphy1_psu, dts:245-247).
    //
    // Both _STA values are patched at boot by RK3576AcpiPlatformDxe: a
    // segment is hidden when its ComboPHY is not in PCIe mode, and, on this
    // board (PcdAcpiPcieRequireRootBridge = TRUE in TaishanPi3M.dsc), also
    // when UEFI did not produce a root bridge for it.  The second rule is
    // there because an untrained link leaves the controller held in reset
    // (PciHostBridgeInit.c), and its DBI is what _CRS, RES0 and the MCFG
    // point the OS at.
    include ("Pcie.asl")

    // eMMC, sdhci at 0x2a330000.  dts:832-843: bus-width 8, HS400 at 1.8 V
    // with enhanced strobe, non-removable, no SD, no SDIO; the same
    // properties Emmc.asl carries.  No _CID PNP0D40 (see Emmc.asl).
    include ("Emmc.asl")

    // SD card, sdmmc at 0x2a310000.  Card detect is GPIO0_A7 with a pull-up
    // (cd-gpios, dts:819; sdmmc0_det_l, dts:748-749), which is the pin and
    // pull Sdhc.asl declares.  No power-enable GPIO: none exists on this
    // board (dts:825), and Sdhc.asl declares none.
    //
    // The device tree caps the card at max-frequency = 50 MHz (dts:821);
    // Sdhc.asl says 200 MHz.  With UHS and 1.8 V signalling both off in
    // Sdhc.asl, the card cannot run faster than high speed (50 MHz) anyway.
    include ("Sdhc.asl")

    // GPIO banks GPIO0..GPIO4 and the pin controller
    include ("Gpio.asl")

    // DMA controllers DMA0..DMA2
    include ("Dma.asl")

    // UART2 at 0x2ad50000.  Not used by this board's device tree, as it is
    // not by the CM5-IO's; kept so the SoC set matches the one that has been
    // booted.  UART0 is the SPCR/DBG2 console (dts:849-851, 1500000 8N1).
    include ("Uart.asl")

    // I2C0..I2C9 (SoC-level)
    include ("I2c.asl")

    // RK806 PMIC on I2C1 at 0x23 (dts:329-334, pmic@23, reg = <0x23>).  Same
    // bus, address and part as the CM5-IO.  Its interrupt (GPIO0_A6,
    // dts:337-338) is not declared, as on the CM5-IO.
    //
    // The slave address is in an I2cSerialBusV2 descriptor in _CRS, not in
    // _ADR: _ADR is for a device the OS finds by enumerating its parent bus,
    // and a device that names itself with _HID must describe its resources
    // instead.  100 kHz because &i2c1 sets no clock-frequency (dts:329-331),
    // which is the Linux default.
    Scope (I2C1) {
      Device (PMC0) {
        Name (_HID, "PRP0001")
        Name (_UID, 0)
        Name (_CRS, ResourceTemplate () {
          I2cSerialBusV2 (0x0023, ControllerInitiated, 100000,
                          AddressingMode7Bit, "\\_SB.I2C1",
                          0x00, ResourceConsumer, , Exclusive,)
        })
        Name (_DSD, Package () {
          ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
          Package () {
            // dts:333
            Package () { "compatible", Package () { "rockchip,rk806" } },
          }
        })
        Method (_STA) { Return (0xf) }
      }
    }

    // HYM8563 RTC on I2C2 at 0x51 (dts:675-677, rtc@51, reg = <0x51>).
    // Linux rtc-hym8563.c binds HID HYMB0001.  100 kHz because &i2c2 sets no
    // clock-frequency (dts:633-634).
    //
    // Its interrupt is GPIO0_A5 here (dts:680-681), where the CM5-IO's is
    // GPIO0_A0.  It is not declared, as on the CM5-IO; GPIO0_A0 is this
    // board's LED (dts:50), so the CM5-IO pin must not be copied in if an
    // interrupt is ever added.
    Scope (I2C2) {
      Device (RTC0) {
        Name (_HID, "HYMB0001")
        Name (_UID, 0)
        Name (_CRS, ResourceTemplate () {
          I2cSerialBusV2 (0x0051, ControllerInitiated, 100000,
                          AddressingMode7Bit, "\\_SB.I2C2",
                          0x00, ResourceConsumer, , Exclusive,)
        })
        Method (_STA) { Return (0xf) }
      }
    }

    // Goodix GT9271, the touch controller of LCKFB's 800x1280 MIPI DSI
    // screen, on I2C0 at 0x5d (rk3576-lckfb-tspi-3m-uefi.dts, touchscreen@5d,
    // which is also where the 400 kHz comes from).  For the woa-rk3576
    // gt9271 driver, which binds RKCP9271.
    //
    // The screen is optional and its controller does not answer without it,
    // so the device is hidden unless TCHP is 1.  TouchKeysDxe sets it at
    // ReadyToBoot when the controller answered its product ID read.  It must
    // stay "Name (TCHP, 0)": the patch turns that one ZeroOp into a OneOp.
    //
    // Interrupt: GPIO0_C5 (pin 21 of GPI0), active low.  The controller's
    // factory configuration pulses it on a falling edge, so it is declared
    // Edge here, where the device tree says IRQ_TYPE_LEVEL_LOW; an edge is
    // latched by the GPIO block and a short pulse cannot be missed.  The
    // board has no pull-up on it, and rk3xgpio does not configure pulls, so
    // TouchKeysDxe enables the SoC pull-up (dts touch_int, pcfg_pull_up);
    // PullUp below says the same.  Reset (GPIO0_D0) is not declared: the
    // GT9xx reset sequence also selects the I2C address from the level of
    // the interrupt line, and nothing on the Windows side resets the chip.
    Scope (I2C0) {
      Device (TCH0) {
        Name (_HID, "RKCP9271")
        Name (_UID, 0)
        Name (TCHP, 0)
        Name (_CRS, ResourceTemplate () {
          I2cSerialBusV2 (0x005d, ControllerInitiated, 400000,
                          AddressingMode7Bit, "\\_SB.I2C0",
                          0x00, ResourceConsumer, , Exclusive,)
          GpioInt (Edge, ActiveLow, Exclusive, PullUp, 0, "\\_SB.GPI0") { GPIO_PIN_PC5 }
        })
        Method (_STA) {
          If (TCHP) {
            Return (0xf)
          }
          Return (0x0)
        }
      }
    }

    // SPI0..SPI4 (SoC-level; SFC0 is declared disabled)
    include ("Spi.asl")

    // Bluetooth: the AP6256's BCM4345C5 on UART6, with BT_REG_ON, BT_WAKE
    // and HOST_WAKE (dts:853-873).  The device claims UART6 itself; there is
    // no separate UART device.  Bluetooth.asl says why.
    include ("Bluetooth.asl")

    // USB: DRD1 at 0x23400000, host only (dr_mode = "host", dts:921-926).
    // The onboard hub (dts:928-968) carries all four USB-A ports, and the
    // USB 3.0 port's SuperSpeed lanes go to DRD1 through combphy1
    // (usb_drd1_ss, dts:970-981).  The ports' 5 V switches (GPIO2_B5 and
    // GPIO0_C3, dts:66-88) are turned on by the firmware and are not
    // described here.
    include ("Usb3Host1.asl")

    // USB: DRD0 at 0x23000000, the USB-C port (usb_drd0_dwc3, dts:903-919),
    // as the CM5-IO declares it.  UsbHcd brings up the USBDP PHY's USB3 half
    // at ExitBootServices with the lanes for the plug orientation it read
    // from the FUSB302, and enables DRD0's U3 port only once the PHY's PLL
    // locks (the fix for the synchronous external abort usbxhci took on the
    // CM5-IO reading port 2's PORTSC with no PHY behind it).  If the PLL does
    // not lock, the U3 port stays disabled and Windows fails XHC0 with code
    // 10 instead.
    //
    // The port's VBUS switch (vcc_5v0_typec, GPIO0_B6, dts:90-100) is set
    // by the firmware from the FUSB302 at ExitBootServices: on when a sink
    // is attached, off otherwise.  Windows has no port manager, so VBUS
    // keeps that state: plug a bus-powered device in before booting, and
    // reboot before connecting a PC or a charger to this port.
    include ("Usb3Host0.asl")

    // GMAC0 at 0x2a220000, the only Ethernet port (dts:281-295).
    //
    // Gmac0.asl says phy-mode "rgmii-rxid", which is what this board's device
    // tree says (dts:285): the PHY adds the RX delay, the MAC the TX delay.
    // The MAC TX delay itself is not in ACPI: the firmware writes
    // tx_delay = <0x24> (dts:293) from PcdGmac0TxDelay into the IOC delay
    // line (GmacPlatformDxe.c), and an OS that does not reprogram it
    // inherits it.  The PHY (Maxio MAE0621A,
    // dts:689-697) keeps its own 25 MHz crystal and its strap delays, so
    // there is no SoC reference clock to describe, unlike the CM5-IO's
    // YT8531C.  Its reset (GPIO2_B3, dts:696) is not declared, as on the
    // CM5-IO; the firmware leaves it deasserted.
    include ("Gmac0.asl")

    // Display (DSP0, RKCP7403) and Mali-G52 (GPU0, RKCP7402), as on the
    // CM5-IO and in the same order, for the wddm-mali-g52 drivers (malidod
    // binds DSP0, maliwddm binds GPU0).  Both are the SoC's: DSP0 claims
    // the VOP2's registers (0x27d00000, 0x27d05000) and no interrupt, GPU0
    // the Mali's registers and its job, MMU and GPU interrupts; see the two
    // files.  RK3576Dxe powers PD_GPU on every board before boot, which
    // needs vdd_gpu_s0 on; here it is RK806 DCDC5 with regulator-boot-on
    // (dts:429-431), as on the CM5-IO.  The display VOP2 and HDMI path is
    // the same one the CM5-IO's GOP uses.
    //
    // With no driver installed, WinPE on the CM5-IO listed both as code 28
    // (no driver) and the display stayed on Basic Display.  Neither driver
    // has been run on this board.
    //
    // The DSI screen's backlight is PWM2 channel 2 (pwm@2ade2000,
    // Tspi3mDsiPanel.c).  Gpu.asl hands its registers to the display driver
    // when RK3576AcpiPlatformDxe says the screen is lit and is the primary
    // output; with no screen, or with HDMI as the screen, GPU0 is what it is
    // on the CM5-IO.  Not the fan's channel (3, 0x2ade3000, Fan.asl): each
    // PWM2 channel is its own 4 KiB block, so the two never share a register.
    //
#define GPU0_BACKLIGHT_PWM_BASE  0x2ADE2000
    include ("Display.asl")
    include ("Gpu.asl")

    // SoC temperature: TSADC channels 0-2 as thermal zones TZ00..TZ02, with
    // a 115 C _CRT below the 120 C hardware TSHUT the firmware arms
    // (PcdTsadcEnable in TaishanPi3M.dsc).  The fan and its trip points on
    // TZ01 are in Fan.asl, a separate SSDT installed only when fan control is
    // enabled in setup.
    include ("Thermal.asl")

#if FixedPcdGetBool (PcdHdmiAudioPreset)
    // SAI6, the HDMI audio interface (hdmi_sound and sai6, dts:315-323,
    // 774-777).  Only with the firmware's HDMI audio preset, which sets its
    // clocks and the HDMI controller's audio path; see Sai.asl.
    include ("Sai.asl")
#endif

#if FixedPcdGetBool (PcdAnalogAudioPreset)
    // The ES8388 headphone codec.  Not in the mainline device tree; the
    // wiring is LCKFB's vendor tree as Armbian's board patch carries it
    // (rockchip64-7.3/board-lckfb-tspi-3m-01-onboard-hardware.patch):
    //
    //   es8388: audio-codec@11 on &i2c3 (i2c3m0_xfer), MCLK from
    //     CLK_SAI1_MCLKOUT_TO_IO at 12.288 MHz on sai1m0_mclk.
    //   &sai1: sai1m0_lrck, _sclk, _sdi0, _sdo2,
    //     rockchip,sai-tx-route = <2 1 0 3> (lane 0 out on SDO2).
    //   es8388_sound: simple-audio-card, i2s, mclk-fs 256, cpu sai1,
    //     hp-det-gpios = <&gpio4 RK_PB0 GPIO_ACTIVE_HIGH> (pull-up),
    //     aux hp_amp: enable-gpios = <&gpio4 RK_PB2 GPIO_ACTIVE_HIGH>,
    //     LOUT1/ROUT1 -> amplifier -> headphones; LINPUT1/2 onboard mic,
    //     RINPUT1/2 headset mic.
    //
    // The board library (Tspi3mAnalogAudioInit) powers PD_AUDIO, sets
    // mclk_sai1 and the MCLK output, muxes the SAI1 and I2C3 pins, ungates
    // the I2C3 and GPIO4 clocks and puts the pull-up on the jack-detect line.
#define BOARD_SAI1_TX_ROUTE       0x2103
#define BOARD_SAI1_RX_ROUTE       0
    include ("Sai1.asl")

#define BOARD_CODEC_I2C           "\\_SB.I2C3"
#define BOARD_CODEC_I2C_ADDR      0x11
#define BOARD_CODEC_GPIO          "\\_SB.GPI4"
#define BOARD_CODEC_GPIO_PIN      GPIO_PIN_PB0
#define BOARD_CODEC_AMP_GPIO      "\\_SB.GPI4"
#define BOARD_CODEC_AMP_GPIO_PIN  GPIO_PIN_PB2
    Scope (I2C3) {
      include ("Es8388.asl")
    }
#endif
  }
}
