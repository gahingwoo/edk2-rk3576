/** @file
 *
 *  SAI1, the audio interface of a board's I2S codec.
 *
 *  Driver: rk3xi2sbus and csaudiork3x in github.com/gahingwoo/woa-rk3576.
 *  "rockchip,tplg" = "i2s-jack" makes csaudiork3x publish a headphone
 *  endpoint and a jack microphone endpoint; "rockchip,sai" = 1 makes it run
 *  this block as a SAI (bit-clock and frame master, two 32-bit slots, 24
 *  valid bits, playback and capture on lane 0).  The codec itself is a
 *  separate device on its I2C bus (Es8388.asl).
 *
 *  From mainline rk3576.dtsi:
 *
 *    sai1: sai@2a610000 {
 *      reg = <0x0 0x2a610000 0x0 0x1000>;
 *      interrupts = <GIC_SPI 188 IRQ_TYPE_LEVEL_HIGH>;     GSIV 220
 *      clocks = <&cru MCLK_SAI1_8CH>, <&cru HCLK_SAI1_8CH>;
 *      dmas = <&dmac0 2>, <&dmac0 3>;  dma-names = "tx", "rx";
 *      power-domains = <&power RK3576_PD_AUDIO>;
 *    };
 *
 *  The board sets, before including this file:
 *    BOARD_SAI1_TX_ROUTE  rockchip,sai-tx-route packed one nibble per lane,
 *                         lane 0 in bits 15:12 (the DT's <2 1 0 3> is 0x2103)
 *    BOARD_SAI1_RX_ROUTE  the same for rockchip,sai-rx-route (0 = reset
 *                         routing, SDIn on path n)
 *
 *  Nothing in ACPI sets the clocks.  The board library does, when
 *  PcdAnalogAudioPreset is TRUE: PD_AUDIO on, mclk_sai1 = 12.288 MHz from
 *  clk_audio_frac_1, the SAI1 clocks and MCLK output ungated, the pins
 *  muxed.  _STA reports the device only if CRU_CLKSEL_CON46 holds what the
 *  board library writes there (mclk_sai1_src = clk_audio_frac_1, divide by
 *  1, mclk_sai1 = mclk_sai1_src); its reset value selects the 24 MHz
 *  oscillator, so a boot on which the library found no exact 12.288 MHz
 *  publishes nothing.
 *
 *  NOT RUN WITH ANY OPERATING SYSTEM.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (SAI1) {
  Name (_HID, "RKCP3004")
  Name (_UID, 1)
  Name (_CCA, 0)

  Method (_STA, 0, Serialized) {
    // CRU_CLKSEL_CON46, 0x27200000 + 0x300 + 46 * 4
    OperationRegion (CRUS, SystemMemory, 0x272003B8, 0x4)
    Field (CRUS, DWordAcc, NoLock, Preserve) {
      C046, 32
    }

    If ((C046 & 0xFFF) == 0x200) {
      Return (0xF)
    }
    Return (0x0)
  }

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x2A610000, 0x1000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 220 }
    })
    Return (RBUF)
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package (2) { "rockchip,dma", "DMA0" },
      Package (2) { "rockchip,tx", 2 },
      Package (2) { "rockchip,rx", 3 },
      Package (2) { "rockchip,tplg", "i2s-jack" },
      Package (2) { "rockchip,sai", 1 },
      // mclk_sai1 / fs, as the board library sets it (simple-audio-card
      // mclk-fs in the board's device tree).
      Package (2) { "rockchip,mclk-fs", 256 },
      Package (2) { "rockchip,sai-tx-route", BOARD_SAI1_TX_ROUTE },
      Package (2) { "rockchip,sai-rx-route", BOARD_SAI1_RX_ROUTE },
    }
  })

  Name (_DEP, Package () { \_SB.DMA0 })
}
