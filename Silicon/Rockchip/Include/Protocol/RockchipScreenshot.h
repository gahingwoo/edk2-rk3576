/** @file
 *
 *  ScreenshotDxe, as seen by other drivers.
 *
 *  The protocol asks for a screenshot, as if Ctrl+Alt+F12 had been
 *  pressed: it is taken once the caller's TPL drops below TPL_CALLBACK.
 *
 *  Around every capture two event groups are signalled, from TPL_CALLBACK:
 *  START just before the picture is read, END once the file is written.
 *  A driver that draws over the picture (the on-screen keyboard and its
 *  mouse cursor) takes itself off the screen at START, in a notification
 *  at TPL_NOTIFY so that it runs before the picture is read, and may come
 *  back after END.
 *
 *  The GUIDs are defined here and not in RockchipPkg.dec; each user makes
 *  its own EFI_GUID from the macros.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ROCKCHIP_SCREENSHOT_H_
#define ROCKCHIP_SCREENSHOT_H_

#define ROCKCHIP_SCREENSHOT_PROTOCOL_GUID \
  { 0x77c2be78, 0x97d2, 0x41d3, { 0x8d, 0x9b, 0x3a, 0x8b, 0x6f, 0x2b, 0xa8, 0x42 } }

#define ROCKCHIP_SCREENSHOT_START_GROUP_GUID \
  { 0x89b01338, 0x32ff, 0x49a0, { 0xa2, 0x29, 0x0a, 0x3c, 0xcf, 0xe6, 0x95, 0x43 } }

#define ROCKCHIP_SCREENSHOT_END_GROUP_GUID \
  { 0x46c63f9e, 0x5282, 0x451f, { 0xa2, 0xd6, 0x77, 0x6d, 0xf3, 0xab, 0x69, 0x30 } }

typedef struct _ROCKCHIP_SCREENSHOT_PROTOCOL ROCKCHIP_SCREENSHOT_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *ROCKCHIP_SCREENSHOT_TAKE)(
  IN ROCKCHIP_SCREENSHOT_PROTOCOL  *This
  );

struct _ROCKCHIP_SCREENSHOT_PROTOCOL {
  UINT32                      Revision;
  ROCKCHIP_SCREENSHOT_TAKE    Take;
};

#define ROCKCHIP_SCREENSHOT_REVISION  1

#endif
