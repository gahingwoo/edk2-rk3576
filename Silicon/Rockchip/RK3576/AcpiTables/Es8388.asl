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
 *    BOARD_CODEC_AMP_GPIO      optional: GPIO bank of the amplifier enable
 *    BOARD_CODEC_AMP_GPIO_PIN  its pin
 *
 *  _HID RKCP8388 makes the driver run the RK3576 SAI sequence (24-bit I2S,
 *  codec slave, MCLK = 256 fs from the SoC), not the 16-bit one it runs for
 *  ESSX8388 on RK3588 boards.  The first GpioIo is the amplifier enable,
 *  driven high in D0.  The GpioInt is the jack-detect line; the driver
 *  connects it and does not yet report jack state to Windows.
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
        GpioInt (Edge, ActiveHigh, Exclusive, PullUp, 0x0000,
          BOARD_CODEC_GPIO, 0x00, ResourceConsumer) { BOARD_CODEC_GPIO_PIN }
      })
      Return (RBUF)
    }

    Method (_STA) { Return (0xf) }
  }
