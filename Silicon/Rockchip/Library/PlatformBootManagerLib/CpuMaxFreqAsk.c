/** @file
 *
 *  "CPU Maximum Frequency", asked once on the screen.
 *
 *  The setup option (RK3576Dxe CpuMaxFreq.c, PcdCpuMaxFreq) defaults to
 *  1608 MHz.  On a board that offers Max (PcdCpuMaxFreqSupported), the
 *  first boot with a display asks the user which one to use, once:
 *
 *    - only when the variable CpuMaxFreqAsked is not in the NV store; it is
 *      written once the question has been answered or has timed out, so the
 *      question is never shown again (delete the variable to see it again);
 *    - only with a display: the console has a GOP and the display driver
 *      did not report that it found nothing (LcdGraphicsOutputDxe publishes
 *      DisplayDetected; with forced output and no sink it is 0).  Without
 *      one, nothing is asked and nothing is written, so a board started
 *      headless is asked the first time it has a screen;
 *    - 10 seconds, then the current setting is kept (1608 MHz on a board
 *      that never chose); any key stops the countdown;
 *    - keys: Up/Down (a drag on the touch screen) move, Enter (a tap)
 *      chooses, 1 and 2 choose directly, Esc (two fingers, long press)
 *      takes the default.
 *
 *  A choice that differs from what this boot ran with is written to
 *  CpuMaxFreq and the board restarts, so the rails and clocks are set by
 *  the same code that sets them on every later boot.
 *
 *  English only, as the setup menus.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include "PlatformBm.h"

//
// RK3576Dxe's form set (RK3576DxeFormSetGuid.h) and CpuMaxFreq.h values.
//
extern EFI_GUID  gRK3576DxeFormSetGuid;

#define CPU_MAX_FREQ_DEFAULT  0
#define CPU_MAX_FREQ_MAX      1

#define CPU_MAX_FREQ_VAR_ATTR  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)
#define CPU_MAX_FREQ_ASKED     L"CpuMaxFreqAsked"
#define ASK_TIMEOUT_SECONDS    10

STATIC CONST CHAR16  *mChoice[2] = {
  L"1608 MHz (default, runs cooler)",
  L"Max: 2208 MHz (faster, needs cooling)"
};

STATIC CONST CHAR16  *mText[] = {
  L"CPU maximum frequency",
  L"",
  L"This board can run its CPU above the default 1608 MHz: 2208 MHz on the",
  L"big cores and 2016 MHz on the little ones. Max raises the CPU voltages to",
  L"the top operating points of mainline Linux. It is faster, but the board",
  L"gets hotter: use it only with a heatsink or a fan. Without cooling it can",
  L"overheat and become unstable.",
  L"",
  NULL,     // the two choices go here
  NULL,
  L"",
  L"You are asked this once. Change it later in Setup: Device Manager >",
  L"RK3576 Platform Configuration > CPU Maximum Frequency.",
  L"",
};

#define TEXT_CHOICE_ROW  8

/**
  Is there a screen to ask on?
**/
STATIC
BOOLEAN
CpuMaxFreqHasDisplay (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT8                         Detected;
  UINTN                         Size;

  if ((gST->ConsoleOutHandle == NULL) ||
      EFI_ERROR (gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)))
  {
    return FALSE;
  }

  Size = sizeof (Detected);
  if (!EFI_ERROR (gRT->GetVariable (L"DisplayDetected", &gRK3576DxeFormSetGuid, NULL, &Size, &Detected)) &&
      (Detected == 0))
  {
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
CpuMaxFreqDraw (
  IN UINTN    Column,
  IN UINTN    Row,
  IN UINTN    Width,
  IN UINT8    Choice,
  IN UINTN    SecondsLeft,
  IN BOOLEAN  Counting
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  CHAR16                           Line[128];
  CHAR16                           Hint[96];
  UINTN                            Index;
  UINT8                            This;

  Out = gST->ConOut;
  for (Index = 0; Index < ARRAY_SIZE (mText); Index++) {
    Out->SetCursorPosition (Out, Column, Row + Index);
    if ((Index == TEXT_CHOICE_ROW) || (Index == TEXT_CHOICE_ROW + 1)) {
      This = (UINT8)(Index - TEXT_CHOICE_ROW);
      Out->SetAttribute (Out, (This == Choice) ? EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY) : EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
      UnicodeSPrint (Line, sizeof (Line), L"  %c %d. %-*s", (This == Choice) ? L'>' : L' ', (INT32)This + 1, Width - 8, mChoice[This]);
    } else {
      Out->SetAttribute (Out, EFI_TEXT_ATTR ((Index == 0) ? EFI_WHITE : EFI_LIGHTGRAY, EFI_BLACK));
      UnicodeSPrint (Line, sizeof (Line), L"%-*s", Width, mText[Index]);
    }

    Out->OutputString (Out, Line);
  }

  Out->SetAttribute (Out, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  Out->SetCursorPosition (Out, Column, Row + ARRAY_SIZE (mText));
  if (Counting) {
    UnicodeSPrint (Hint, sizeof (Hint), L"Up/Down (drag) to move, Enter (tap) to choose. %u s left.", (UINT32)SecondsLeft);
  } else {
    UnicodeSPrint (Hint, sizeof (Hint), L"Up/Down (drag) to move, Enter (tap) to choose.");
  }

  UnicodeSPrint (Line, sizeof (Line), L"%-*s", Width, Hint);
  Out->OutputString (Out, Line);
}

/**
  Ask, if this is the boot to ask on.  Called from
  PlatformBootManagerAfterConsole () with the consoles connected and before
  the logo.  Restarts the board when the answer changes the setting.
**/
VOID
PlatformAskCpuMaxFreqOnce (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  EFI_EVENT                        Timer;
  EFI_EVENT                        Events[2];
  EFI_INPUT_KEY                    Key;
  UINTN                            Size;
  UINTN                            Which;
  UINTN                            Columns;
  UINTN                            Rows;
  UINTN                            Width;
  UINTN                            Column;
  UINTN                            Row;
  UINTN                            SecondsLeft;
  UINT8                            Current;
  UINT8                            Choice;
  UINT8                            Asked;
  BOOLEAN                          Counting;
  BOOLEAN                          Done;

  if (!FixedPcdGetBool (PcdCpuMaxFreqSupported)) {
    return;
  }

  Size = sizeof (Asked);
  if (!EFI_ERROR (gRT->GetVariable (CPU_MAX_FREQ_ASKED, &gRK3576DxeFormSetGuid, NULL, &Size, &Asked))) {
    return;
  }

  if (!CpuMaxFreqHasDisplay ()) {
    DEBUG ((DEBUG_INFO, "CpuMaxFreqAsk: no display, not asking this boot\n"));
    return;
  }

  Size = sizeof (Current);
  if (EFI_ERROR (gRT->GetVariable (L"CpuMaxFreq", &gRK3576DxeFormSetGuid, NULL, &Size, &Current)) ||
      (Current > CPU_MAX_FREQ_MAX))
  {
    Current = FixedPcdGet8 (PcdCpuMaxFreqDefault);
  }

  Out = gST->ConOut;
  if (EFI_ERROR (Out->QueryMode (Out, Out->Mode->Mode, &Columns, &Rows))) {
    Columns = 80;
    Rows    = 25;
  }

  Width  = MIN (Columns > 2 ? Columns - 2 : Columns, 78);
  Column = (Columns - Width) / 2;
  Row    = (Rows > ARRAY_SIZE (mText) + 1) ? (Rows - ARRAY_SIZE (mText) - 1) / 2 : 0;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    return;
  }

  gBS->SetTimer (Timer, TimerPeriodic, EFI_TIMER_PERIOD_SECONDS (1));
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = Timer;

  //
  // Keys typed before the question are not answers.
  //
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
  }

  Out->EnableCursor (Out, FALSE);
  Out->SetAttribute (Out, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  Out->ClearScreen (Out);

  Choice      = Current;
  SecondsLeft = ASK_TIMEOUT_SECONDS;
  Counting    = TRUE;
  Done        = FALSE;
  DEBUG ((DEBUG_INFO, "CpuMaxFreqAsk: asking, current %u\n", Current));

  while (!Done) {
    CpuMaxFreqDraw (Column, Row, Width, Choice, SecondsLeft, Counting);
    if (EFI_ERROR (gBS->WaitForEvent (2, Events, &Which))) {
      break;    // keep the current setting
    }

    if (Which == 1) {
      if (Counting && (--SecondsLeft == 0)) {
        Choice = Current;
        Done   = TRUE;
      }

      continue;
    }

    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    Counting = FALSE;
    if ((Key.ScanCode == SCAN_UP) || (Key.ScanCode == SCAN_DOWN) ||
        (Key.ScanCode == SCAN_LEFT) || (Key.ScanCode == SCAN_RIGHT))
    {
      Choice = (Choice == CPU_MAX_FREQ_MAX) ? CPU_MAX_FREQ_DEFAULT : CPU_MAX_FREQ_MAX;
    } else if (Key.UnicodeChar == L'1') {
      Choice = CPU_MAX_FREQ_DEFAULT;
      Done   = TRUE;
    } else if (Key.UnicodeChar == L'2') {
      Choice = CPU_MAX_FREQ_MAX;
      Done   = TRUE;
    } else if (Key.ScanCode == SCAN_ESC) {
      Choice = CPU_MAX_FREQ_DEFAULT;
      Done   = TRUE;
    } else if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.UnicodeChar == CHAR_LINEFEED)) {
      Done = TRUE;
    }
  }

  gBS->CloseEvent (Timer);
  Out->SetAttribute (Out, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  Out->ClearScreen (Out);

  DEBUG ((DEBUG_INFO, "CpuMaxFreqAsk: %a\n", (Choice == CPU_MAX_FREQ_MAX) ? "Max" : "1608 MHz"));

  if (Choice != Current) {
    Status = gRT->SetVariable (L"CpuMaxFreq", &gRK3576DxeFormSetGuid, CPU_MAX_FREQ_VAR_ATTR, sizeof (Choice), &Choice);
    if (EFI_ERROR (Status)) {
      //
      // Not stored: neither is the answer, so the question comes back.
      //
      DEBUG ((DEBUG_ERROR, "CpuMaxFreqAsk: could not store CpuMaxFreq: %r\n", Status));
      return;
    }
  }

  Asked  = 1;
  Status = gRT->SetVariable (
                  CPU_MAX_FREQ_ASKED,
                  &gRK3576DxeFormSetGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Asked),
                  &Asked
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CpuMaxFreqAsk: could not store %s: %r\n", CPU_MAX_FREQ_ASKED, Status));
  }

  if (Choice != Current) {
    Print (L"CPU maximum frequency set to %s. Restarting to apply it...\n", mChoice[Choice]);
    gBS->Stall (1000 * 1000);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }
}
