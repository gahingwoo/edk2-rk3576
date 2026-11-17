/** @file

  Installs the wide glyphs the zh-Hans setup strings need into the HII
  database.

  EDK2's own HII font, the one GraphicsConsoleDxe installs, has ASCII,
  Latin-1, box drawing and four arrows.  A CJK string drawn with it comes out
  blank.  HiiDatabaseDxe looks a glyph up in every simple-font package it
  holds, so adding a second package with the CJK glyphs is enough: nothing
  that draws text has to know about it.

  The package holds only the characters our zh-Hans strings use (see
  scripts/gen-zh-hans-font.py), so it stays a few KiB.

  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>

#include "ZhHansFontDxe.h"

STATIC EFI_GUID  mZhHansFontPackageListGuid = {
  0x5c697c66, 0x3feb, 0x4c4b, { 0x90, 0xc0, 0xf0, 0x2a, 0x99, 0x43, 0xe3, 0x21 }
};

EFI_STATUS
EFIAPI
ZhHansFontDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32                           PackageLength;
  UINT8                            *Package;
  EFI_HII_SIMPLE_FONT_PACKAGE_HDR  *SimpleFont;
  EFI_HII_HANDLE                   HiiHandle;

  //
  // HiiAddPackages() takes a package list body: a UINT32 total length
  // (including itself) followed by the package.  Same layout as
  // GraphicsConsoleDxe's narrow font, with no narrow glyphs.
  //
  // +--------------------------------+ <-- Package
  // | UINT32 length                  |
  // +--------------------------------+ <-- SimpleFont
  // | EFI_HII_SIMPLE_FONT_PACKAGE_HDR|
  // +--------------------------------+
  // | EFI_WIDE_GLYPH[]               |
  // +--------------------------------+
  //
  PackageLength = (UINT32)(sizeof (UINT32) +
                           sizeof (EFI_HII_SIMPLE_FONT_PACKAGE_HDR) +
                           gZhHansWideGlyphCount * sizeof (EFI_WIDE_GLYPH));

  Package = AllocateZeroPool (PackageLength);
  if (Package == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  WriteUnaligned32 ((UINT32 *)Package, PackageLength);
  SimpleFont                       = (EFI_HII_SIMPLE_FONT_PACKAGE_HDR *)(Package + sizeof (UINT32));
  SimpleFont->Header.Length        = PackageLength - sizeof (UINT32);
  SimpleFont->Header.Type          = EFI_HII_PACKAGE_SIMPLE_FONTS;
  SimpleFont->NumberOfNarrowGlyphs = 0;
  SimpleFont->NumberOfWideGlyphs   = (UINT16)gZhHansWideGlyphCount;

  CopyMem (
    SimpleFont + 1,
    gZhHansWideGlyphs,
    gZhHansWideGlyphCount * sizeof (EFI_WIDE_GLYPH)
    );

  HiiHandle = HiiAddPackages (&mZhHansFontPackageListGuid, NULL, Package, NULL);
  FreePool (Package);

  if (HiiHandle == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: HiiAddPackages failed\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_INFO, "%a: %u wide glyphs installed\n", __func__, (UINT32)gZhHansWideGlyphCount));
  return EFI_SUCCESS;
}
