/** @file
 *
 *  The "About" page of the RK3576 platform form.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_ABOUT_H_
#define RK3576_ABOUT_H_

#include <Uefi.h>

/**
  Fill in what is known now (firmware, board) and arrange for the rest
  (clocks, memory, display, touch screen) to be filled in once the consoles
  are up.

  @param  HiiHandle   The platform form's package list.
  @param  BuildStamp  When RK3576Dxe.c was compiled (__DATE__ __TIME__).
                      It is passed in because that is the file
                      scripts/build.sh touches on every build.
**/
VOID
RK3576InitAboutPage (
  IN EFI_HII_HANDLE  HiiHandle,
  IN CONST CHAR8     *BuildStamp
  );

#endif // RK3576_ABOUT_H_
