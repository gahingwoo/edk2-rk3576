/** @file
 *
 *  Differentiated System Description Table (DSDT)
 *  Radxa ROCK 4D (RK3576)
 *
 *  Written from the CM5-IO DSDT (Platform/ArmSoM/CM5IO/AcpiTables/Dsdt.asl),
 *  which is the RK3576 DSDT that has booted Windows 11 23H2, the same way the
 *  TaishanPi 3M's was.  Every device the CM5-IO declares was checked against
 *  this board's mainline device tree, devicetree/mainline/rk3576-rock-4d.dts,
 *  cited below as "dts:<line>", and against the Radxa ROCK 4D schematic
 *  V1.12, cited by page name.  SoC addresses and interrupts are not repeated
 *  here; they are in the shared files under Silicon/Rockchip/RK3576/AcpiTables
 *  and were taken from rk3576.dtsi.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM ON THIS BOARD.  It compiles; that is all
 *  that is known.
 *
 *  Kept from the CM5-IO, because the device is the SoC's and the board does
 *  not change it: CPUs, PCIe root complexes, SD, GPIO, DMA, UART2,
 *  I2C0..I2C9, SPI0..SPI4 (SFC0 declared disabled), both USB controllers,
 *  GMAC0, the display and the Mali-G52, the RK806 PMIC and the HYM8563 RTC.
 *  Added for parity with the TaishanPi 3M: thermal zones and the ES8388.
 *
 *  Left out, on purpose:
 *
 *    - eMMC (EMM0).  This board has no eMMC.  The BTB connector J3 ("EMMC/SPI/
 *      UFS" page) can take an eMMC or a UFS module, and the eMMC signals share
 *      their pins with FSPI0, which carries the 16 MB SPI NOR
 *      (MX25U12832F) the firmware boots from and keeps its variables in.
 *      Mainline leaves the sdhci node disabled and enables sfc0 (dts:792-805);
 *      the firmware builds with RK_EMMC_ENABLE = FALSE.  Declaring the
 *      controller would point Windows at a block whose pins belong to the
 *      flash.
 *    - UFS.  ufshc is enabled in the device tree (dts:830-832), and a UFS
 *      module can sit in J3, but Windows has no driver for this controller
 *      here.  There is no Ufs.asl; nothing describes it.
 *    - XHC2 and ATA0..ATA2.  The CM5-IO includes them as _STA = 0 stubs whose
 *      addresses are RK3588's; a disabled device with a wrong address adds
 *      nothing.  The TaishanPi 3M left them out for the same reason.
 *    - The BL24C16F EEPROM on I2C6 at 0x50 (dts:685-697): no Windows driver,
 *      and the CM5-IO declares no I2C client beyond the PMIC and the RTC.
 *    - WiFi.  It is a USB device on the hub ("USB WIFI" page), powered by
 *      the firmware (vcc_3v3_wifi, GPIO2_C7, and WIFI_CHIP_EN, GPIO2_D1;
 *      dts:40-46, 170-181).  Windows finds it on USB or not at all; it needs
 *      no ACPI device.
 *    - The speaker amplifiers ("Audio SPK" page): not fitted.  The ES8388
 *      headphone codec is declared (below), only when PcdAnalogAudioPreset
 *      is TRUE, without a jack-detect line: the jack's detect is an analog
 *      SARADC input here, not a GPIO.
 *    - The LEDs (dts:48-66) and HDMI itself: not declared on the CM5-IO
 *      either.
 *
 *  The fan header (J16, "40PIN/FAN/LED/KEY" page) is in Fan.asl, a separate
 *  SSDT installed only when fan control by thermal zone is enabled in
 *  setup.
 *
 *  Copyright (c) 2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2018-2020, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2026, gahingwoo <huhuvmb88@outlook.com>
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

    // PCIe root complexes.  pcie0 sits on combphy0 (combphy0_ps,
    // dts:232-234), which goes to the 16-pin PCIe FPC connector ("TF Card/
    // EEPROM/PCIE" page).  An NVMe drive needs an M.2 adapter on that
    // connector.  PERST# is PCIE0_PERSTn, GPIO2_B4 (dts:714-720), and the
    // slot's 3.3 V switch is PCIE0_PWREN_H, GPIO2_D3 (vcc_3v3_pcie,
    // dts:127-138); the firmware drives both and neither is described here,
    // as on the CM5-IO.  pcie1 would need combphy1, which is USB3 for DRD1
    // here (combphy1_psu, dts:236-238; USB3_OTG1 on the "PCIe/SATA/USB3"
    // page).  Same SoC wiring as the CM5-IO.
    //
    // Both _STA values are patched at boot by RK3576AcpiPlatformDxe: a
    // segment is hidden when its ComboPHY is not in PCIe mode, and, on this
    // board (PcdAcpiPcieRequireRootBridge = TRUE in ROCK4D.dsc), also when
    // UEFI did not produce a root bridge for it, which is the case when no
    // adapter or no drive is on the FPC connector.  An untrained link leaves
    // the controller held in reset (PciHostBridgeInit.c), and its DBI is
    // what _CRS, RES0 and the MCFG point the OS at.
    include ("Pcie.asl")

    // SD card, sdmmc at 0x2a310000 (dts:777-789).  Card detect is
    // SDMMC0_DET_L on GPIO0_A7 ("RK3576-OSC/PLL/PMUIO" and "TF Card/EEPROM/
    // PCIE" pages), which is the pin Sdhc.asl declares, with a pull-up.  The
    // device tree has no cd-gpios: Linux uses the controller's own detect
    // input on that pin.  vmmc is vcc_3v3_s3, always on (dts:786): no
    // power-enable GPIO, and Sdhc.asl declares none.
    //
    // The device tree allows sd-uhs-sdr104 with vqmmc on RK806 PLDO5
    // (dts:785-787); Sdhc.asl runs the card at 3.3 V without UHS, for the
    // reason given there.
    include ("Sdhc.asl")

    // GPIO banks GPIO0..GPIO4 and the pin controller
    include ("Gpio.asl")

    // DMA controllers DMA0..DMA2
    include ("Dma.asl")

    // UART2 at 0x2ad50000.  Not used by this board's device tree, as it is
    // not by the CM5-IO's; kept so the SoC set matches the one that has been
    // booted.  UART0 is the SPCR/DBG2 console (dts:825-828, 1500000 8N1).
    include ("Uart.asl")

    // I2C0..I2C9 (SoC-level)
    include ("I2c.asl")

    // RK806 PMIC on I2C1 at 0x23 (dts:315-320, pmic@23, reg = <0x23>).  Same
    // bus, address and part as the CM5-IO.  Its interrupt (GPIO0_A6,
    // dts:323-324) is not declared, as on the CM5-IO.
    //
    // The slave address is in an I2cSerialBusV2 descriptor in _CRS, not in
    // _ADR: _ADR is for a device the OS finds by enumerating its parent bus,
    // and a device that names itself with _HID must describe its resources
    // instead.  100 kHz because &i2c1 sets no clock-frequency (dts:315-316),
    // which is the Linux default.
    Scope (I2C1) {
      Device (PMC0) {
        // RKCP0806 so that a Windows INF can name this device (woa-rk3576
        // drivers/null/rk3576-null.inf: the firmware manages the PMIC and
        // Windows has no driver for it), as on the other two boards.
        // PRP0001 as the _CID keeps the "compatible" match for an ACPI
        // Linux.
        Name (_HID, "RKCP0806")
        Name (_CID, "PRP0001")
        Name (_UID, 0)
        Name (_CRS, ResourceTemplate () {
          I2cSerialBusV2 (0x0023, ControllerInitiated, 100000,
                          AddressingMode7Bit, "\\_SB.I2C1",
                          0x00, ResourceConsumer, , Exclusive,)
        })
        Name (_DSD, Package () {
          ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
          Package () {
            // dts:319
            Package () { "compatible", Package () { "rockchip,rk806" } },
          }
        })
        Method (_STA) { Return (0xf) }
      }
    }

    // HYM8563 RTC on I2C2 at 0x51 (dts:669-674, rtc@51, reg = <0x51>).
    // Linux rtc-hym8563.c binds HID HYMB0001.  100 kHz because &i2c2 sets no
    // clock-frequency (dts:669-670).  Its interrupt is GPIO0_A0 (dts:677-678),
    // the same pin as on the CM5-IO, and it is not declared, as there.
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

    // SPI0..SPI4 (SoC-level).  SFC0, the FSPI that holds this board's
    // firmware and UEFI variable store, is declared with _STA = 0 in Spi.asl,
    // so Windows does not bind anything to it.
    include ("Spi.asl")

    // USB.  The USB-C connector on this board is power input only (CC1/CC2
    // and VBUS on the "Power-DC IN/RTC" page, no data lines), so both
    // controllers go to USB-A ports ("USB HUB/USB2/USB3" page):
    //
    //   DRD0 at 0x23000000, host (dts:838-841): the upper USB 3.0 port,
    //   J26 TOP.  USB 2.0 from u2phy0, SuperSpeed from the USBDP PHY's lanes
    //   0 and 1 (USB3_OTG0_SSRX1/SSTX1), which is the unflipped lane mapping,
    //   the one UsbHcd tries first at ExitBootServices since this board does
    //   not report an orientation.  VBUS is 5V_USB_OTG0 from an SY6280
    //   enabled by USB_OTG0_PWREN_H, GPIO2_D2 (vcc_5v0_otg, dts:207-219).
    //
    //   DRD1 at 0x23400000, host (dts:843-846): USB 2.0 from u2phy1 into the
    //   FE1.1s hub (U24), whose four ports are the lower USB 3.0 port's USB
    //   2.0 pair, the two USB 2.0 ports (J27) and the USB WiFi.  The lower
    //   port's SuperSpeed comes from DRD1 through combphy1.  VBUS is
    //   5V_USB_HOST, enabled by USB_HOST_PWREN_H, GPIO0_D3 (vcc_5v0_host,
    //   dts:193-205).
    //
    // The firmware turns both VBUS switches on and leaves them on; neither is
    // described here.  The CM5-IO's XHC0 took a synchronous external abort
    // at WinPE shutdown until the firmware brought up the USBDP PHY's USB3
    // half at ExitBootServices; that code is shared and runs on this board
    // too, and has not run here.
    include ("Usb3Host0.asl")
    include ("Usb3Host1.asl")

    // GMAC0 at 0x2a220000, the only Ethernet port (dts:272-284).
    //
    // The PHY is a Realtek RTL8211F-CG at MDIO address 1 ("ETHERNET" page;
    // ethernet-phy-id001c.c916, dts:699-712), clocked with 25 MHz from the
    // SoC (REFCLKO25M_GMAC0_OUT, which the firmware muxes out on
    // ethm0_clk0_25m_out).  The device tree says phy-mode "rgmii-id": both
    // delays in the PHY, none in the MAC, and the firmware sets the MAC TX
    // delay to 0 (PcdGmac0TxDelay in ROCK4D.dsc).  Gmac0.asl says
    // "rgmii-rxid", as on the CM5-IO; Windows' dwc_eqos does not read
    // phy-mode, and inherits the delays the PHY's straps and the firmware
    // leave.  The firmware writes only the LED register of the RTL8211F
    // (RealtekPhy.c), not its delays.  Its reset (GMAC0_RSTn, GPIO2_B5,
    // dts:710) is not declared, as on the CM5-IO; the firmware leaves it
    // deasserted.
    include ("Gmac0.asl")

    // Display (DSP0, RKCP7403) and Mali-G52 (GPU0, RKCP7402), as on the
    // CM5-IO and in the same order, for the wddm-mali-g52 drivers (malidod
    // binds DSP0, maliwddm binds GPU0).  Both are the SoC's: DSP0 claims
    // the VOP2's registers and no interrupt, GPU0 the Mali's registers and
    // its job, MMU and GPU interrupts; see the two files.  RK3576Dxe powers
    // PD_GPU on every board before boot, which needs vdd_gpu_s0 on; here it
    // is RK806 DCDC5 with regulator-boot-on (dts:478-489), as on the CM5-IO.
    // The HDMI output is VP0 to HDMI0 (dts:291-313, 856-861), the path the
    // CM5-IO's GOP uses.  Without either driver Windows runs on the
    // firmware's framebuffer with Basic Display.
    include ("Display.asl")
    include ("Gpu.asl")

    // SoC temperature: TSADC channels 0-2 as thermal zones TZ00..TZ02, with
    // a 115 C _CRT below the 120 C hardware TSHUT the firmware arms
    // (PcdTsadcEnable in ROCK4D.dsc).  The fan and its trip points on TZ01
    // are in Fan.asl, a separate SSDT installed only when fan control is
    // enabled in setup.
    include ("Thermal.asl")

    // SARADC (adc@2ae00000; vref vcca_1v8_s0 = 1.8 V, vendor tree &saradc),
    // for the woa-rk3576 rk3xsaradc driver.  The mainline tree has no
    // adc-keys and no &saradc for this board; the wiring is the
    // schematic's (V1.12 page 11 "RK3576-SARADC", page 16 "Audio-CODEC"):
    //
    //   channel 0  SARADC_VIN0_BOOT: R6 10K to VCCA1V8_PLDO2_S0, R45 6.8K to
    //              ground (729 mV, raw 1658, the "FSPI0->UFS->USB" boot
    //              strap), and the MASKROM key S1/S2 to ground.  Declared as
    //              an adc-keys button: pressed ~0 V, key-up 729 mV, code
    //              KEY_VENDOR (0x168), reported as vendor-page button 1.
    //              The BootROM samples this pin only at reset.
    //   channel 1  SARADC_VIN1_KEY/RECOVERY: R52 10K pull-up to test point
    //              TP20 only, no button.  Not declared.
    //   channel 3  SARADC_VIN3_HP_HOOK: J18's microphone contact
    //              (MIC_INP_PHONE), biased from VCCA3V3_CODEC through R293
    //              100R + R291 2.2K, divided by R294/R296 100K/100K.  With
    //              nothing plugged ~1.63 V; a 3-pole plug shorts the contact
    //              to the sleeve, ~0 V; a headset's capsule pulls it to about
    //              0.5-1.2 V; its button to ~0 V.  Plugged below 1.45 V,
    //              unplugged above 1.52 V, 150 ms (computed, not measured:
    //              the first values to check with adctest).
    //
    // RK3576Dxe sets the converter clock (PcdRk3576SaradcClockInit in
    // ROCK4D.dsc).
#define BOARD_SARADC_KEYS_CHANNEL     0
#define BOARD_SARADC_KEYS_KEYUP_UV    729000
#define BOARD_SARADC_KEYS_PRESS_UV    0
#define BOARD_SARADC_KEYS_CODE        0x168
#define BOARD_SARADC_JACK_CHANNEL     3
#define BOARD_SARADC_JACK_BELOW_UV    1450000
#define BOARD_SARADC_JACK_ABOVE_UV    1520000
    include ("Saradc.asl")

#if FixedPcdGetBool (PcdHdmiAudioPreset)
    // SAI6, the HDMI audio interface (hdmi_sound and sai6, dts:307-309,
    // 773-775).  Only with the firmware's HDMI audio preset, which sets its
    // clocks and the HDMI controller's audio path; see Sai.asl.
    include ("Sai.asl")
#endif

#if FixedPcdGetBool (PcdAnalogAudioPreset)
    // The ES8388 headphone codec.  Not in the mainline device tree; the
    // wiring is the schematic's (V1.12 page 16 "Audio-CODEC") and the vendor
    // kernel tree's (rk3576-rock-4d.dts):
    //
    //   es8388: es8388@10 on &i2c3 (i2c3m0), MCLK from mclkout_sai1 at
    //     12.288 MHz on sai1m0_mclk (:519-535).
    //   &sai1: sai1m0_lrck, _sclk, _sdi0, _sdo0 (:457-464), no
    //     rockchip,sai-tx-route: lane 0 out on SDO0, in on SDI0.
    //   es8388_sound: mclk-fs 256, cpu sai1 (:157-184); LOUT1/ROUT1 to the
    //     headphone jack J18 directly, LINPUT1 the headset microphone.
    //
    // No amplifier enable: LOUT1/ROUT1 drive the jack without one, and the
    // speaker amplifiers on LOUT2/ROUT2 are not fitted ("Audio SPK" page).
    // No detect line: the jack is read on SARADC_VIN3_HP_HOOK, an analog
    // input (vendor tree io-channels = <&saradc 3>), by the woa-rk3576
    // rk3xsaradc driver (\_SB.ADC0 above).  So BOARD_CODEC_NO_HPDET (no
    // detect line in _CRS; es8323 creates its interrupt only when _CRS has
    // a GpioInt, woa-rk3576 es83-optional-irq, 111de2c) together with
    // BOARD_CODEC_HPDET_SARADC: _DSD says rockchip,hp-det-saradc = 1 and
    // es8323 follows the SARADC driver's jack state.  Without that driver
    // the headphone endpoint stays plugged, as before.
    //
    // Routing (schematic V1.12 page 16): LOUT1/ROUT1 -> HPOUT_L/R -> J18;
    // LOUT2/ROUT2 (AUD_LINEOUT_L/R) only reach the speaker amplifier on
    // page 24, which is not fitted.  The headset microphone MIC_INP_PHONE
    // goes into LIN1, with RIN1 on HP_GND; LIN2/RIN2 are not connected.
    // So BOARD_CODEC_OUTPUT_ROUTE 1 (OUT1, ES8388 reg 0x04 = 0x30) and
    // BOARD_CODEC_INPUT_SELECT 1 (LIN1/RIN1, reg 0x0A = 0x00): the
    // driver's defaults, written out so the wiring is in the table.
    //
    // The board library (Rock4dAnalogAudioInit) powers PD_AUDIO, sets
    // mclk_sai1 and the MCLK output, muxes the SAI1 and I2C3 pins and
    // ungates the I2C3 clocks.  The route is written out as identity
    // (<0 1 2 3>, lane 0 in bits 15:12) rather than left at 0, which would
    // keep whatever SAI_PATH_SEL holds.
#define BOARD_SAI1_TX_ROUTE       0x0123
#define BOARD_SAI1_RX_ROUTE       0x0123
    include ("Sai1.asl")

#define BOARD_CODEC_I2C           "\\_SB.I2C3"
#define BOARD_CODEC_I2C_ADDR      0x10
#define BOARD_CODEC_NO_HPDET
#define BOARD_CODEC_HPDET_SARADC
#define BOARD_CODEC_OUTPUT_ROUTE  1
#define BOARD_CODEC_INPUT_SELECT  1
    Scope (I2C3) {
      include ("Es8388.asl")
    }
#endif
  }
}
