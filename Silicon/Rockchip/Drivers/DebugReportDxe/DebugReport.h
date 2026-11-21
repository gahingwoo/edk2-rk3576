/** @file
 *
 *  DebugReportDxe: what its source files share.
 *
 *    DebugReportDxe.c  the setup page, the hot key, running a report
 *    Report.c          where it goes, and everything but the setup pages
 *    FormWalk.c        a screenshot of every setup page
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef DEBUG_REPORT_H_
#define DEBUG_REPORT_H_

#include <PiDxe.h>
#include <Uefi/UefiInternalFormRepresentation.h>
#include <Protocol/SimpleFileSystem.h>

#define REPORT_DIRECTORY  L"uefi-debug"

//
// A text file being written, kept in memory until it is saved.
//
typedef struct {
  CHAR8    *Buffer;
  UINTN    Length;
  UINTN    Size;
} REPORT_TEXT;

VOID
EFIAPI
TextAdd (
  IN OUT REPORT_TEXT  *Text,
  IN     CONST CHAR8  *Format,
  ...
  );

/**
  Unicode text, as ASCII ('?' for anything else).
**/
VOID
TextAddUnicode (
  IN OUT REPORT_TEXT   *Text,
  IN     CONST CHAR16  *String
  );

VOID
TextFree (
  IN OUT REPORT_TEXT  *Text
  );

EFI_STATUS
ReportWriteFile (
  IN EFI_FILE_PROTOCOL  *Dir,
  IN CONST CHAR16       *Name,
  IN CONST VOID         *Data,
  IN UINTN              Size
  );

EFI_STATUS
ReportOpenDir (
  IN  EFI_FILE_PROTOCOL  *Parent,
  IN  CONST CHAR16       *Name,
  OUT EFI_FILE_PROTOCOL  **Dir
  );

//
// Report.c
//

/**
  Make \uefi-debug\<date and time>\ on the best writable FAT file system:
  a USB stick, else an SD card or other removable medium, else internal
  storage.  Where: the folder and the device, for people.
**/
EFI_STATUS
ReportOpenDestination (
  OUT EFI_FILE_PROTOCOL  **Dir,
  OUT CHAR16             *Where,
  IN  UINTN              WhereSize
  );

/**
  Everything except the setup pages: the log, the summary, the ACPI tables,
  the device tree and the screen as it is.  Each part on its own; Parts
  gets a line per part saying how it went.
**/
VOID
ReportWriteAll (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN OUT REPORT_TEXT        *Parts
  );

/**
  A screenshot through ScreenshotDxe, when it is there.
**/
EFI_STATUS
ReportScreenshot (
  IN EFI_FILE_PROTOCOL  *Dir,
  IN CONST CHAR16       *Name
  );

//
// FormWalk.c
//

/**
  The timer the walk uses.  At driver entry.
**/
EFI_STATUS
WalkInit (
  VOID
  );

/**
  The keyboard the walk types Esc on.  Installed at EndOfDxe.
**/
EFI_STATUS
WalkInstallKeys (
  VOID
  );

/**
  Show every form of every form set in turn and save a screenshot of each
  in Dir.  Must run where SendForm () may: from a setup browser callback.
**/
VOID
WalkForms (
  IN     EFI_FILE_PROTOCOL  *Dir,
  IN     EFI_HII_HANDLE     Skip,
  IN OUT REPORT_TEXT        *Parts
  );

#endif
