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

//
// What the CC lines showed, and what the chip was left presenting.
//
typedef enum {
  Rk3576Fusb302NoChip = 0,  // DEVICE_ID not acknowledged
  Rk3576Fusb302I2cError,    // the chip answered, a later transfer failed
  Rk3576Fusb302Open,        // neither line has Rd or Rp: nothing attached
  Rk3576Fusb302Source,      // the far end shows Rp: a PC, a charger, a C-to-A cable
  Rk3576Fusb302Sink,        // Rd on exactly one line: a device to be powered
  Rk3576Fusb302RdBoth       // Rd on both lines: a debug or audio accessory
} RK3576_FUSB302_PARTNER;

typedef struct {
  RK3576_FUSB302_PARTNER    Partner;
  USB_TYPEC_ORIENTATION     Orientation;  // Normal/Reverse only for a sink
  UINT8                     DeviceId;
  UINT8                     BcLvlCc1;     // passive check, Rd on both lines
  UINT8                     BcLvlCc2;
  BOOLEAN                   RpPresented;  // left with Rp on the attached line
} RK3576_FUSB302_STATE;

/**
  Measure the CC lines, as Rk3576Fusb302GetOrientation does, and say what
  was found and what the chip was left presenting.

  The chip ends either reset (Rd on both lines, which a source answers by
  turning its VBUS on) or, for a sink only, with Rp on the attached line.
  The caller owns VBUS and must not have it on unless RpPresented is TRUE.

  @param[in]   Bus    The I2C controller the chip is on.
  @param[out]  State  What was measured.
**/
VOID
EFIAPI
Rk3576Fusb302Probe (
  IN  CONST RK3576_FUSB302_BUS  *Bus,
  OUT RK3576_FUSB302_STATE      *State
  );

/**
  A short name for a partner kind, for the log.
**/
CONST CHAR8 *
EFIAPI
Rk3576Fusb302PartnerName (
  IN RK3576_FUSB302_PARTNER  Partner
  );

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
