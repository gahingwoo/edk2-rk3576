/** @file
 *
 *  Secondary System Description Table (SSDT): the TaishanPi 3M's SARADC
 *  and its one key, Recovery, as the OS's power button.
 *
 *  Installed only with "ADC keys" on in setup (PcdAcpiSaradc,
 *  default on for this board, TaishanPi3M.dsc): RK3576AcpiPlatformDxe skips
 *  any SSDT with OEM table ID "RKSARADC" otherwise, and RK3576Dxe then
 *  leaves the SARADC clock alone too.  The driver is woa-rk3576 rk3xsaradc
 *  (RKCP0ADC).
 *
 *  The board has two buttons, RECOVERY and RESET.  The PMIC's PWRON net
 *  has no key fitted, so the RK806 power-key device (PwrKey.asl) is not
 *  offered here (PcdAcpiPwrKeySupported is FALSE), and once this firmware
 *  runs the Recovery key has no other use: the BootROM does not read it,
 *  and since d053639 neither does the firmware.  So under Windows it is the
 *  power button, like a laptop's: a short press does what "When I press
 *  the power button" says in Power Options.
 *
 *  The key (rk3576-lckfb-tspi-3m.dts adc-keys, dts:30-42): channel 1,
 *  keyup-threshold-microvolt 1800000, press-threshold-microvolt 18000,
 *  linux,code KEY_VENDOR (0x168).  _DSD keeps that code and adds the HID
 *  usage the driver reports instead of a vendor-page button: Generic
 *  Desktop (page 0x01), System Power Down (0x81), the usage woa-rk3576
 *  rk806pwrkey reports for the PMIC's key on the other boards.  The driver
 *  reports a key only after it has seen the keys released (300 ms) and
 *  then held for two polls (200 ms), so a key held through boot or a
 *  converter that reads 0 V never sends a press.
 *
 *  What is in this table, and why it is safe to load (a new optional table
 *  once failed to boot Windows on this board, see the RhProxy SSDT): one
 *  device in \_SB, nothing External, no reference to any other table's
 *  objects; its register window (0x2AE00000) and interrupt (GSIV 156) are
 *  claimed by nothing else; the only AML that writes hardware, RSTC, runs
 *  only when the driver calls it and moves one bit of one CRU register
 *  (SOFTRST_CON13 bit 6, SRST_P_SARADC), the way the eMMC, SAI and NPU
 *  tables already write the CRU.  Without the driver installed Windows
 *  shows an unknown device and nothing else happens.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

DefinitionBlock ("RecoveryKey.aml", "SSDT", 2, "RKCP  ", "RKSARADC", 1)
{
  Scope (\_SB) {
    // SARADC (adc@2ae00000, &saradc dts:779-782, vref vcca1v8_pldo2_s0 =
    // 1.8 V).
#define BOARD_SARADC_KEYS_CHANNEL    1
#define BOARD_SARADC_KEYS_KEYUP_UV   1800000
#define BOARD_SARADC_KEYS_PRESS_UV   18000
#define BOARD_SARADC_KEYS_CODE       0x168
    // Generic Desktop << 16 | System Power Down
#define BOARD_SARADC_KEYS_HID_USAGE  0x00010081
    include ("Saradc.asl")
  }
}
