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
  //
  // For Rk3576Fusb302DpAltMode only (zero in a board that never calls it):
  // the current the port's VBUS switch may give, offered in the one 5 V
  // PDO, and whether the chip's VCONN pin has a supply (both boards: the
  // TaishanPi 3M's VCONN pins are on VCCA_3V3_S0, the CM5-IO's on
  // VCC5V0_DEVICE_S0).
  //
  UINT16         VbusMilliAmps;
  BOOLEAN        VconnSupply;
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

//
// DisplayPort Alt Mode, entered by the firmware (Rk3576Fusb302Pd.c).
//
// How far Rk3576Fusb302DpAltMode got: each step is reached only after the
// one before it succeeded, so the last one names what failed next.
//
typedef enum {
  Rk3576DpStepNone = 0,     // not a sink, or the chip did not answer
  Rk3576DpStepPdInit,       // PD receiver on, no explicit contract
  Rk3576DpStepContract,     // Source_Capabilities, Request, Accept, PS_RDY
  Rk3576DpStepIdentity,     // Discover Identity ACKed, modal operation
  Rk3576DpStepSvid,         // 0xFF01 among the partner's SVIDs
  Rk3576DpStepModes,        // a DisplayPort mode with pins C, D or E
  Rk3576DpStepEntered,      // Enter Mode ACKed
  Rk3576DpStepStatus,       // DP Status Update ACKed
  Rk3576DpStepConfigured    // DP Configure ACKed: the partner's mux is set
} RK3576_DP_STEP;

typedef VOID (EFIAPI *RK3576_FUSB302_VBUS_SET)(
  IN BOOLEAN  On
  );

typedef struct {
  RK3576_DP_STEP    Step;
  BOOLEAN           Configured;   // Step == Rk3576DpStepConfigured
  CHAR8             PinName;      // 'C', 'D', 'E', or '-'
  BOOLEAN           Hpd;          // HPD_State from DP Status or an Attention
  BOOLEAN           Vconn;        // VCONN turned on (Ra on the other line)
  BOOLEAN           VbusCycled;   // VBUS was switched off and on again
  UINT32            CapsSent;     // Source_Capabilities transmissions
  UINT32            IdHeader;     // Discover Identity's ID header VDO
  UINT32            ModeVdo;      // the DisplayPort capabilities VDO used
  UINT32            StatusVdo;    // the partner's last DP Status
  UINT32            ConfigVdo;    // what DP Configure asked for
} RK3576_FUSB302_DP;

/**
  Negotiate a PD contract as a 5 V source and DFP and enter DisplayPort Alt
  Mode, polled, once.

  Only for a sink found by Rk3576Fusb302Probe with Rp presented (State) and
  with the port's VBUS already on.  Takes up to about four seconds when the
  partner does not answer.  VbusSet, when given, may be called to switch
  VBUS off for a second and on again, once, when the partner never sends a
  Request (a sink that gave up on PD before the firmware got here).

  What is left running for the OS: the PD receiver with automatic GoodCRC,
  so the partner's later messages (HPD Attention) are acknowledged, and
  VCONN when it was turned on.  Nobody reads them after this; see
  docs/usbc-dp.md.

  @param[in]   Bus      The I2C controller the chip is on.
  @param[in]   State    Rk3576Fusb302Probe's answer.
  @param[in]   VbusSet  The board's VBUS switch, or NULL.
  @param[out]  Dp       What happened.
**/
VOID
EFIAPI
Rk3576Fusb302DpAltMode (
  IN  CONST RK3576_FUSB302_BUS    *Bus,
  IN  CONST RK3576_FUSB302_STATE  *State,
  IN  RK3576_FUSB302_VBUS_SET     VbusSet  OPTIONAL,
  OUT RK3576_FUSB302_DP           *Dp
  );

#endif // RK3576_FUSB302_LIB_H_
