/** @file
*
*  Copyright (c) 2021, Rockchip Limited. All rights reserved.
*  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#ifndef __PALTFORM_LIB_H__
#define __PALTFORM_LIB_H__

#include <Uefi.h>
#include <Library/GpioLib.h>

VOID
EFIAPI
SdmmcIoMux (
  VOID
  );

VOID
EFIAPI
SdhciEmmcIoMux (
  VOID
  );

VOID
EFIAPI
EdpEnableBacklight (
  IN UINT32   Id,
  IN BOOLEAN  Enable
  );

VOID
EFIAPI
GmacIomux (
  IN UINT32  Id
  );

VOID
EFIAPI
GmacIoPhyReset (
  IN UINT32   Id,
  IN BOOLEAN  Enable
  );

VOID
EFIAPI
Rk806SpiIomux (
  VOID
  );

VOID
EFIAPI
Rk806Configure (
  VOID
  );

VOID
EFIAPI
SetCPULittleVoltage (
  IN UINT32  Microvolts
  );

VOID
EFIAPI
NorFspiIomux (
  VOID
  );

VOID
EFIAPI
NorFspiEnableClock (
  UINT32  *CruBase
  );

VOID
EFIAPI
I2cIomux (
  UINT32  id
  );

VOID
EFIAPI
UsbPortPowerEnable (
  VOID
  );

VOID
EFIAPI
Usb2PhyResume (
  VOID
  );

//
// Plug orientation of a USB Type-C port, for setting up the SuperSpeed lanes
// behind it. Boards without a CC controller UEFI can read return Unknown.
//
typedef enum {
  UsbTypeCOrientationUnknown = 0,
  UsbTypeCOrientationNormal,      // CC1 attached
  UsbTypeCOrientationReverse      // CC2 attached
} USB_TYPEC_ORIENTATION;

USB_TYPEC_ORIENTATION
EFIAPI
UsbTypeCGetOrientation (
  IN UINT32  Port
  );

//
// DisplayPort Alt Mode on a USB-C port, entered by the board when the
// "USB-C DisplayPort" setup option is on (docs/usbc-dp.md).  Called by
// UsbHcd's ExitBootServices handler right after UsbTypeCGetOrientation,
// before the USBDP PHY's lanes are set up.  The board negotiates PD and the
// alt mode with its Type-C controller and sets the SBU (AUX) bias; the PHY,
// the DP controller's clocks and HPD are UsbHcd's.
//
typedef struct {
  BOOLEAN                  Active;         // DP Configure acknowledged
  BOOLEAN                  Hpd;            // the partner's HPD_State
  CHAR8                    PinAssignment;  // 'C', 'D' or 'E'
  USB_TYPEC_ORIENTATION    Orientation;
} USB_TYPEC_DP_STATE;

/**
  @retval EFI_SUCCESS      State says what happened (Active may be FALSE).
  @retval EFI_UNSUPPORTED  This board's port does not carry DisplayPort.
**/
EFI_STATUS
EFIAPI
UsbTypeCDpAltModeEnter (
  IN  UINT32              Port,
  OUT USB_TYPEC_DP_STATE  *State
  );

VOID
EFIAPI
PcieIoInit (
  UINT32  Segment
  );

VOID
EFIAPI
PciePowerEn (
  UINT32   Segment,
  BOOLEAN  Enable
  );

VOID
EFIAPI
PciePeReset (
  UINT32   Segment,
  BOOLEAN  Enable
  );

VOID
EFIAPI
HdmiTxIomux (
  IN UINT32  Id
  );

VOID
EFIAPI
PwmFanIoSetup (
  VOID
  );

VOID
EFIAPI
PwmFanSetSpeed (
  IN UINT32  Percentage
  );

VOID
EFIAPI
PlatformInitLeds (
  VOID
  );

VOID
EFIAPI
PlatformSetStatusLed (
  IN BOOLEAN  Enable
  );

CONST EFI_GUID *
EFIAPI
PlatformGetDtbFileGuid (
  IN UINT32  CompatMode
  );

VOID
EFIAPI
PlatformEarlyInit (
  VOID
  );

/*
 * PlatformSdioWifiInit: set up the board's SDIO Wi-Fi module for an OS
 * that has no pin, clock or power-sequence driver of its own (pins, clocks,
 * WL_REG_ON), and probe the card so the serial log says whether it answers.
 * Called by RK3576Dxe only when the "SDIO Wi-Fi" setup option is on
 * (PcdAcpiSdioWifi) and the board says it has one (PcdAcpiSdioWifiSupported).
 * Returns EFI_UNSUPPORTED on a board without one.
 */
EFI_STATUS
EFIAPI
PlatformSdioWifiInit (
  VOID
  );

/*
 * The board's 40-pin header, for the "40-pin UART" and "40-pin GPIO/I2C/SPI
 * for apps" setup options (PcdPin40Uart, PcdPin40RhProxy).  RK3576Dxe does
 * the work from this description: clocks of the listed controllers (UARTs
 * from xin24m, I2C and SPI from their 200 MHz default parent), then the
 * pin functions.  The board's AcpiTables describe the same controllers and
 * pins (its UART and RhProxy SSDTs); the two must agree.
 *
 * Uart/I2c/Spi are controller numbers (UARTn, I2Cn, SPIn).  UartPins are
 * applied with the UART option, BusPins (I2C and SPI) with the RhProxy
 * option, GpioPins (the pins RhProxy exposes as GPIOs, function 0, pull left
 * as it is) with the RhProxy option too.  The debug console UART (UART0)
 * must never be listed.
 */
#define PIN40_MAX_CONTROLLERS  4

typedef struct {
  UINT8                       UartCount;
  UINT8                       Uart[PIN40_MAX_CONTROLLERS];
  UINT8                       I2cCount;
  UINT8                       I2c[PIN40_MAX_CONTROLLERS];
  UINT8                       SpiCount;
  UINT8                       Spi[PIN40_MAX_CONTROLLERS];
  CONST GPIO_IOMUX_CONFIG     *UartPins;
  UINT32                      UartPinCount;
  CONST GPIO_IOMUX_CONFIG     *BusPins;
  UINT32                      BusPinCount;
  CONST GPIO_IOMUX_CONFIG     *GpioPins;
  UINT32                      GpioPinCount;
} PIN40_CONFIG;

/*
 * PlatformGetPin40Config: the description above, or NULL on a board that
 * does not set PcdPin40Supported.
 */
CONST PIN40_CONFIG *
EFIAPI
PlatformGetPin40Config (
  VOID
  );

#endif
