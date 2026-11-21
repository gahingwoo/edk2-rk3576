/** @file
 *
 *  DebugReportDxe: what the setup page (DebugReport.vfr) and the C code
 *  share.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef DEBUG_REPORT_FORM_SET_H_
#define DEBUG_REPORT_FORM_SET_H_

#define DEBUG_REPORT_FORMSET_GUID \
  { 0xbd543bec, 0xfaee, 0x487a, { 0x92, 0xce, 0xcd, 0xa6, 0xb9, 0x9d, 0x9d, 0xbe } }

#define DEBUG_REPORT_HII_DEVICE_GUID \
  { 0xd2d75a5c, 0xeed4, 0x4583, { 0x9b, 0x59, 0x41, 0xd8, 0x85, 0x89, 0xb7, 0x65 } }

#define DEBUG_REPORT_FORM_ID         1
#define DEBUG_REPORT_QUESTION_ALL    0x1001
#define DEBUG_REPORT_QUESTION_QUICK  0x1002

#endif
