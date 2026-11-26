/** @file
 *
 *  The formset of the "About" page (AboutUiLib), also the target of the goto
 *  in RK3576DxeHii.vfr.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_ABOUT_FORMSET_GUID_H_
#define RK3576_ABOUT_FORMSET_GUID_H_

#define RK3576_ABOUT_FORMSET_GUID \
  { 0x39e73e49, 0x1da0, 0x49ba, { 0x88, 0x14, 0x10, 0xe0, 0xc4, 0x53, 0x2b, 0xdf } }

#define RK3576_ABOUT_FORM_ID  0x0001

//
// The clock line, a read-only action whose callback refreshes it.
//
#define RK3576_ABOUT_KEY_CLOCK  0x1001

#endif // RK3576_ABOUT_FORMSET_GUID_H_
