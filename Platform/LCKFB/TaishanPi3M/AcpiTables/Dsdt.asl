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
 *    - XHC0 (DRD0, the USB-C port).  On this board its VBUS switch
 *      (vcc_5v0_typec, GPIO0_B6, dts:90-100) is only turned on by Linux after
 *      the FUSB302 sees a sink, and the firmware leaves it off, so a host
 *      port there has no power to give.  UsbHcd now brings up the USBDP
 *      PHY's USB3 half at ExitBootServices and enables DRD0's U3 port only
 *      once its PLL locks, the CM5-IO's fix for the synchronous external
 *      abort usbxhci took reading XHC0's port-2 PORTSC (WHEA_INTERNAL_ERROR
 *      at WinPE shutdown).  This board's library does not read its FUSB302
 *      (UsbTypeCGetOrientation returns Unknown), so the lanes are a guess,
 *      and on the CM5-IO SuperSpeed has not trained on that port even with
 *      the orientation known.  Adding XHC0 here would need VBUS, and so a
 *      port manager, first.
 *    - XHC2 and ATA0..ATA2.  The CM5-IO includes them as _STA = 0 stubs whose
 *      addresses are RK3588's; a disabled device with a wrong address adds
 *      nothing.
 *    - FUSB302 (I2C2 0x22, dts:636-673): no Windows driver, and nothing here
 *      runs a Type-C port manager.
 *    - WiFi on SDIO and Bluetooth on UART6 (dts:789-814, 853-873): the CM5-IO
 *      declares neither, and their power, reset and 32 kHz clock are
 *      sequenced by Linux (sdio_pwrseq, dts:167-175).
 *    - Audio.  The mainline tree has HDMI audio on SAI6 only (dts:315-323,
 *      774-777); LCKFB's vendor tree adds an ES8388 on I2C3.  The CM5-IO does
 *      not include Es8388.asl or I2s.asl either, and RK3576's audio block is
 *      SAI, which no driver here supports.
 *    - The LED (GPIO0_A0, dts:47-52), the ADC key (dts:30-42) and HDMI: not
 *      declared on the CM5-IO either.
 *    - DSP0 and GPU0, the display and Mali-G52 devices the CM5-IO declares
 *      for the wddm-mali-g52 drivers.  No driver has been run on this board.
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

    // SPI0..SPI4 (SoC-level; SFC0 is declared disabled)
    include ("Spi.asl")

    // USB: DRD1 at 0x23400000, host only (dr_mode = "host", dts:921-926).
    // The onboard hub (dts:928-968) carries all four USB-A ports, and the
    // USB 3.0 port's SuperSpeed lanes go to DRD1 through combphy1
    // (usb_drd1_ss, dts:970-981).  The ports' 5 V switches (GPIO2_B5 and
    // GPIO0_C3, dts:66-88) are turned on by the firmware and are not
    // described here.
    include ("Usb3Host1.asl")

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
  }
}
