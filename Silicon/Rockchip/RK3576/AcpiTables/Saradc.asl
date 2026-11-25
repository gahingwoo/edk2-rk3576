/** @file
 *
 *  RK3576 SARADC, for the woa-rk3576 rk3xsaradc driver (ACPI\RKCP0ADC).
 *
 *  Include inside Scope (\_SB), on a board that wires something the OS
 *  should see to the ADC.  The board defines, before including this file,
 *  any of:
 *
 *    BOARD_SARADC_KEYS_CHANNEL       adc-keys: io-channels = <&saradc N>
 *    BOARD_SARADC_KEYS_KEYUP_UV      keyup-threshold-microvolt
 *    BOARD_SARADC_KEYS_PRESS_UV      press-threshold-microvolt of each
 *                                    button, comma separated
 *    BOARD_SARADC_KEYS_CODE          linux,code of each button, in the
 *                                    same order
 *    BOARD_SARADC_KEYS_HID_USAGE     optional: HID usage per button,
 *                                    page << 16 | id (0 = from the code)
 *
 *    BOARD_SARADC_JACK_CHANNEL       a headphone jack read on this channel
 *    BOARD_SARADC_JACK_BELOW_UV      plugged below this
 *    BOARD_SARADC_JACK_ABOVE_UV      unplugged above this
 *    BOARD_SARADC_JACK_DEBOUNCE_MS   optional, 150
 *
 *    BOARD_SARADC_VREF_UV            optional, 1800000 (vref-supply is
 *                                    vcca_1v8 on every RK3576 board here)
 *
 *  The names in _DSD are the Linux adc-keys and rockchip,saradc ones,
 *  flattened into device properties because a device property cannot hold
 *  a child node; the driver's README lists them.
 *
 *  Registers and interrupt: rk3576.dtsi adc@2ae00000 (reg 0x2ae00000
 *  0x10000, GIC_SPI 124 level high = GSIV 156), RK3576 TRM V1.2 Part 1
 *  chapter 18.  The driver polls the end-of-conversion status rather than
 *  taking the interrupt; it is declared because it is the device's.
 *
 *  Clocks: PCLK_SARADC and CLK_SARADC are CRU GATE_CON13 bits 6 and 7,
 *  open at reset; RK3576Dxe sets CLK_SARADC to xin24m / 24 = 1 MHz, the rate
 *  Linux sets (PcdRk3576SaradcClockInit, which a board including this file
 *  must set).
 *
 *  RSTC: the controller reset Linux and U-Boot do before every conversion
 *  of this "v2" SARADC (U-Boot quotes Rockchip's kernel: "If read other chn
 *  at anytime, then chn1 will error, assert controller as a workaround").
 *  Windows has no reset framework for an ACPI device, so the method does
 *  it: SRST_P_SARADC, which is CRU SOFTRST_CON13 bit 6 (0x27200A34; TRM
 *  "presetn_saradc"; mainline rst-rk3576.c RK3576_CRU_RESET_OFFSET
 *  (SRST_P_SARADC, 13, 6)), asserted and released with its write-enable
 *  bit, so no other bit of the register moves.  NOT RockchipIpRegs.h's
 *  SRST_P_SARADC (190): that is RK3588's numbering and, decoded on RK3576,
 *  SOFTRST_CON11 bit 14, hresetn_bus_biu, the reset of the whole BUS
 *  interconnect.  SRST_SARADC (bit 7) is not touched; neither OS resets it.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef BOARD_SARADC_VREF_UV
#define BOARD_SARADC_VREF_UV          1800000
#endif
#ifndef BOARD_SARADC_JACK_DEBOUNCE_MS
#define BOARD_SARADC_JACK_DEBOUNCE_MS 150
#endif

  Device (ADC0) {
    Name (_HID, "RKCP0ADC")
    Name (_UID, 0)
    Name (_CCA, 0)

    Name (_CRS, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0x2AE00000, 0x10000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 156 }
    })

    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "rockchip,vref-microvolt", BOARD_SARADC_VREF_UV },
        Package () { "rockchip,poll-interval-ms", 100 },
#ifdef BOARD_SARADC_KEYS_CHANNEL
        Package () { "rockchip,adc-keys-channel", BOARD_SARADC_KEYS_CHANNEL },
        Package () { "rockchip,adc-keys-keyup-microvolt", BOARD_SARADC_KEYS_KEYUP_UV },
        Package () { "rockchip,adc-keys-press-microvolt", Package () { BOARD_SARADC_KEYS_PRESS_UV } },
        Package () { "rockchip,adc-keys-code", Package () { BOARD_SARADC_KEYS_CODE } },
#ifdef BOARD_SARADC_KEYS_HID_USAGE
        Package () { "rockchip,adc-keys-hid-usage", Package () { BOARD_SARADC_KEYS_HID_USAGE } },
#endif
#endif
#ifdef BOARD_SARADC_JACK_CHANNEL
        Package () { "rockchip,jack-channel", BOARD_SARADC_JACK_CHANNEL },
        Package () { "rockchip,jack-plugged-below-microvolt", BOARD_SARADC_JACK_BELOW_UV },
        Package () { "rockchip,jack-unplugged-above-microvolt", BOARD_SARADC_JACK_ABOVE_UV },
        Package () { "rockchip,jack-debounce-ms", BOARD_SARADC_JACK_DEBOUNCE_MS },
#endif
      }
    })

    // CRU SOFTRST_CON13 only: one register, nothing else of the CRU.
    OperationRegion (ADRS, SystemMemory, 0x27200A34, 4)
    Field (ADRS, DWordAcc, NoLock, WriteAsZeros) {
      SRST, 32,     // [31:16] write enable, [6] presetn_saradc, [7] resetn_saradc
    }

    // Assert and release SRST_P_SARADC.  Linux holds it 10-20 us.
    Method (RSTC, 0, Serialized) {
      SRST = 0x00400040
      Stall (20)
      SRST = 0x00400000
      Stall (5)
    }

    Method (_STA) { Return (0xf) }
  }
