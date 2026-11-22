/** @file
 *
 *  Plug orientation of a USB-C port from an FCS FUSB302, read once by the
 *  board library for the USBDP PHY's lane mapping.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_FUSB302_LIB_H_
#define RK3576_FUSB302_LIB_H_

#include <Library/RockchipPlatformLib.h>

//
// The I2C controller the FUSB302 (7-bit address 0x22) sits on.  The board
// muxes the pins and opens the controller's clock gates before the call;
// the library switches the controller's functional clock to xin24m for the
// transfers and puts the mux and the divider back afterwards.
//
typedef struct {
  UINTN          I2cBase;       // RK I2C controller registers
  UINTN          ClkSelReg;     // CRU CLKSEL register with the clk_i2cN mux (hiword mask)
  UINT8          ClkSelShift;   // first bit of the 2-bit mux field
  UINT8          ClkSelXin24m;  // mux value that selects xin24m
  CONST CHAR8    *Name;         // board and bus, for the log
} RK3576_FUSB302_BUS;

/**
  Read which way round the USB-C plug is, measuring as the source this port
  is (data role host, power role source on all our boards).

  Software-resets the FUSB302 at the end.  When a sink was found on one CC
  line, Rp (default USB current) is then presented on that line, as a host
  port should; otherwise the chip stays in its power-on default state.
  Never touches VBUS, PD, toggling, VCONN or the interrupt masks.

  @param[in]  Bus   The I2C controller the chip is on.

  @retval UsbTypeCOrientationNormal   A sink's Rd on CC1.
  @retval UsbTypeCOrientationReverse  A sink's Rd on CC2.
  @retval UsbTypeCOrientationUnknown  No chip, nothing attached, a source on
                                      the other end, Rd on both or neither
                                      line, or an I2C error.
**/
USB_TYPEC_ORIENTATION
EFIAPI
Rk3576Fusb302GetOrientation (
  IN CONST RK3576_FUSB302_BUS  *Bus
  );

#endif // RK3576_FUSB302_LIB_H_
