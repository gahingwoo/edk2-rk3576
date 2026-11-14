/** @file
 *
 *  SAI6, the audio interface that feeds the HDMI controller.
 *
 *  Driver: rk3xi2sbus and csaudiork3x in github.com/gahingwoo/woa-rk3576,
 *  with SAI support (the RK3588 drivers they come from only know the I2S/TDM
 *  block).  The bus driver reads the _DSD below; "rockchip,tplg" = "hdmi"
 *  makes csaudiork3x publish one fixed-format HDMI render endpoint and no
 *  capture.
 *
 *  From mainline rk3576.dtsi:
 *
 *    sai6: sai@27d50000 {
 *      reg = <0x0 0x27d50000 0x0 0x1000>;
 *      interrupts = <GIC_SPI 193 IRQ_TYPE_LEVEL_HIGH>;     GSIV 225
 *      clocks = <&cru MCLK_SAI6_8CH>, <&cru HCLK_SAI6_8CH>;
 *      dmas = <&dmac2 4>, <&dmac2 5>;  dma-names = "tx", "rx";
 *      power-domains = <&power RK3576_PD_VO0>;
 *      rockchip,sai-tx-route = <0 1 2 3>;                  the reset default
 *    };
 *    hdmi_sound: simple-audio-card, format "i2s", mclk-fs = <256>,
 *      cpu = <&sai6>, codec = <&hdmi>.
 *
 *  Nothing in ACPI sets the clocks.  The firmware does, at ExitBootServices
 *  (DwHdmiQpAudio.c, PcdHdmiAudioPreset): mclk_sai6 = 12.288 MHz, the SAI6
 *  clocks ungated, and the HDMI controller set up for 48 kHz 2-channel
 *  L-PCM.  _STA reports the device only if CRU_CLKSEL_CON155 holds what the
 *  firmware writes there (mclk_sai6_src = clk_audio_frac_0, divide by 1,
 *  mclk_sai6 = mclk_sai6_src).  Its reset value selects the 24 MHz
 *  oscillator divided by 2, so a boot on which the firmware found no exact
 *  12.288 MHz, or did not run the preset at all, publishes nothing.  SAI6
 *  sits in PD_VO0 with the HDMI controller, so the domain is on whenever
 *  the firmware's HDMI output is.
 *
 *  "rockchip,rx" is listed because the RK3588 _DSD has it; the HDMI
 *  topology installs no capture endpoint, so it is never used.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (SAI6) {
  Name (_HID, "RKCP3004")
  Name (_UID, 6)
  Name (_CCA, 0)

  Method (_STA, 0, Serialized) {
    OperationRegion (CRUS, SystemMemory, 0x2720056C, 0x4)
    Field (CRUS, DWordAcc, NoLock, Preserve) {
      C155, 32
    }

    If ((C155 & 0xFFF) == 0x100) {
      Return (0xF)
    }
    Return (0x0)
  }

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x27D50000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 225 }
    })
    Return (RBUF)
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "rockchip,dma", "DMA2" },
      Package (2) { "rockchip,tx", 4 },
      Package (2) { "rockchip,rx", 5 },
      Package (2) { "rockchip,tplg", "hdmi" },
      Package (2) { "rockchip,sai", 1 },
      // mclk_sai6 / fs, as the firmware sets it (hdmi_sound mclk-fs).  The
      // driver derives the bit-clock divider from it.
      Package (2) { "rockchip,mclk-fs", 256 },
    }
  })

  Name (_DEP, Package () { \_SB.DMA2 })
}
