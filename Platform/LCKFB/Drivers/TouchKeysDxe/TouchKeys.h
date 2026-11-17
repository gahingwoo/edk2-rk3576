/** @file
 *
 *  Shared between TouchKeysDxe and the TouchTest application.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef TOUCH_KEYS_H_
#define TOUCH_KEYS_H_

//
// Vendor device path of the handle TouchKeysDxe installs its protocols on.
// TouchTest finds the touch controller's absolute pointer by it, and not
// through the console splitter's.
//
#define TOUCH_KEYS_DEVICE_GUID \
  { 0x2c0d7a3e, 0x6b1f, 0x4f4c, { 0x8a, 0x55, 0x1e, 0x0b, 0x73, 0x62, 0x9d, 0x14 } }

//
// FILE_GUID of TouchTest.inf, for the boot option TouchKeysDxe adds.
//
#define TOUCH_TEST_FILE_GUID \
  { 0x9b3d1c52, 0x47a8, 0x4e0f, { 0xa2, 0x6d, 0x3f, 0x80, 0x15, 0xc4, 0x7e, 0x29 } }

#endif
