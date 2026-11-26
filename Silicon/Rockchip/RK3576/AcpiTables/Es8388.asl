/** @file
 *
 *  Everest ES8388 codec on I2C, for the woa-rk3576 es8323 driver.
 *
 *  Include inside the Scope of the codec's I2C controller.  The board sets,
 *  before including this file:
 *
 *    BOARD_CODEC_I2C           the controller, e.g. "\\_SB.I2C3"
 *    BOARD_CODEC_I2C_ADDR      7-bit address
 *    BOARD_CODEC_GPIO          GPIO bank of the jack-detect line ("\\_SB.GPIn")
 *    BOARD_CODEC_GPIO_PIN      its pin in that bank
 *    BOARD_CODEC_NO_HPDET      optional: define it instead of the two above
 *                              on a board with no jack-detect line (the
 *                              output goes to a pin header).  _CRS then
 *                              has no detect GpioIo/GpioInt and there is
 *                              no _DSD, so the driver reports no jack state
 *                              and Windows keeps the endpoint plugged.
 *    BOARD_CODEC_AMP_GPIO      optional: GPIO bank of the amplifier enable
 *    BOARD_CODEC_AMP_GPIO_PIN  its pin
 *    BOARD_CODEC_HPDET_ACTIVE_HIGH
 *                              optional: 1 if the jack-detect line is high
 *                              with a plug in (the default), 0 if low
 *    BOARD_CODEC_OUTPUT_ROUTE  optional: which outputs the board wires,
 *                              bit 0 LOUT1/ROUT1, bit 1 LOUT2/ROUT2
 *                              (driver default 1)
 *    BOARD_CODEC_INPUT_SELECT  optional: the capture input, 1 LIN1/RIN1
 *                              (driver default), 2 LIN2/RIN2, 3 LIN1-RIN1
 *                              differential, 4 LIN2-RIN2 differential
 *    BOARD_CODEC_HPDET_SARADC  optional, with BOARD_CODEC_NO_HPDET: the
 *                              jack is read on a SARADC channel instead
 *                              (Saradc.asl BOARD_SARADC_JACK_*); _DSD gets
 *                              rockchip,hp-det-saradc = 1 and es8323 follows
 *                              the woa-rk3576 rk3xsaradc driver's state
 *
 *  _HID RKCP8388 makes the driver run the RK3576 SAI sequence (24-bit I2S,
 *  codec slave, MCLK = 256 fs from the SoC), not the 16-bit one it runs for
 *  ESSX8388 on RK3588 boards.  The first GpioIo is the amplifier enable,
 *  driven high in D0.
 *
 *  Jack detection.  The detect line is in _CRS twice, as on the SD card
 *  detect (Sdhc.asl): a GpioInt on both edges, which tells the driver that
 *  the line moved, and a GpioIo input, through which it reads the level
 *  once the line has settled.  Both are Shared, so GpioClx lets the one pin
 *  be opened for both.  The pull is up, as the board's device tree has it
 *  (the firmware also sets it in the IOC at boot; Windows' GPIO driver does
 *  not program pulls).
 *
 *  The GpioInt debounce field is 0 on purpose.  The rk3xgpio driver has no
 *  hardware debounce and does not ask GpioClx to emulate it, so a value
 *  there would be dropped; the driver debounces in software instead, for
 *  rockchip,hp-det-debounce-ms after the last edge (150 ms, the debounce
 *  Linux's simple-audio-card uses for hp-det-gpios).
 *
 *  _DSD, read by es8323.sys:
 *    rockchip,hp-det-gpio-io     index of the detect GpioIo among the GpioIo
 *                                descriptors of _CRS (1 after the amplifier
 *                                enable, 0 without one)
 *    rockchip,hp-det-active-high 1: high = plugged; 0: low = plugged
 *    rockchip,hp-det-debounce-ms settle time after the last edge
 *  Without these (older firmware) the driver reports no jack state, and
 *  Windows keeps the headphone endpoint plugged, as before.
 *    rockchip,output-route       BOARD_CODEC_OUTPUT_ROUTE, when defined
 *    rockchip,input-select       BOARD_CODEC_INPUT_SELECT, when defined
 *  Without these the driver runs the TaishanPi 3M's wiring (OUT1, LIN1).
 *  A board with BOARD_CODEC_NO_HPDET and either of them gets a _DSD with
 *  only these; it has no GpioInt, and es8323 then creates no interrupt
 *  (woa-rk3576 es83-optional-irq).
 *
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

  Device (ES83) {
    Name (_HID, "RKCP8388")
    Name (_UID, 0)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        I2cSerialBusV2 (BOARD_CODEC_I2C_ADDR, ControllerInitiated, 100000,
          AddressingMode7Bit, BOARD_CODEC_I2C,
          0x00, ResourceConsumer, , Exclusive)
#ifdef BOARD_CODEC_AMP_GPIO
        GpioIo (Exclusive, PullNone, 0, 0, IoRestrictionOutputOnly,
          BOARD_CODEC_AMP_GPIO, 0x00, ResourceConsumer) { BOARD_CODEC_AMP_GPIO_PIN }
#endif
#ifndef BOARD_CODEC_NO_HPDET
        GpioIo (Shared, PullUp, 0, 0, IoRestrictionInputOnly,
          BOARD_CODEC_GPIO, 0x00, ResourceConsumer) { BOARD_CODEC_GPIO_PIN }
        GpioInt (Edge, ActiveBoth, Shared, PullUp, 0x0000,
          BOARD_CODEC_GPIO, 0x00, ResourceConsumer) { BOARD_CODEC_GPIO_PIN }
#endif
      })
      Return (RBUF)
    }

#if !defined (BOARD_CODEC_NO_HPDET) || defined (BOARD_CODEC_OUTPUT_ROUTE) || defined (BOARD_CODEC_INPUT_SELECT) || defined (BOARD_CODEC_HPDET_SARADC)
    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
#ifndef BOARD_CODEC_NO_HPDET
#ifdef BOARD_CODEC_AMP_GPIO
        Package () { "rockchip,hp-det-gpio-io", 1 },
#else
        Package () { "rockchip,hp-det-gpio-io", 0 },
#endif
#ifdef BOARD_CODEC_HPDET_ACTIVE_HIGH
        Package () { "rockchip,hp-det-active-high", BOARD_CODEC_HPDET_ACTIVE_HIGH },
#else
        Package () { "rockchip,hp-det-active-high", 1 },
#endif
        Package () { "rockchip,hp-det-debounce-ms", 150 },
#endif
#ifdef BOARD_CODEC_OUTPUT_ROUTE
        Package () { "rockchip,output-route", BOARD_CODEC_OUTPUT_ROUTE },
#endif
#ifdef BOARD_CODEC_INPUT_SELECT
        Package () { "rockchip,input-select", BOARD_CODEC_INPUT_SELECT },
#endif
#if defined (BOARD_CODEC_NO_HPDET) && defined (BOARD_CODEC_HPDET_SARADC)
        Package () { "rockchip,hp-det-saradc", 1 },
#endif
      }
    })
#endif

    Method (_STA) { Return (0xf) }
  }
