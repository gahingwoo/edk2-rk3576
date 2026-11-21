# Local patches to the EDK2 core

`third_party/edk2/` is the upstream [tianocore/edk2](https://github.com/tianocore/edk2)
submodule, pinned by `scripts/setup-host.sh` to edk2-stable202602. Edits made
inside it are lost when the checkout is reset. Three of the five patches here are
real fixes, and without them the firmware still builds but misbehaves, so they
are kept as patch files.  The others add Chinese to the stock menus and make
them follow a language switch.

They were first generated against tianocore/edk2 `46548b1` and apply cleanly
to edk2-stable202602, which is what CI builds against.

## Applying

`scripts/setup-host.sh` applies the required set, so the normal path needs no
manual step. By hand:

```sh
patches/apply.sh third_party/edk2                 # required patches only
patches/apply.sh --with-debug third_party/edk2    # + UART tracing
patches/apply.sh --check third_party/edk2         # report, change nothing
```

`setup-host.sh` adds `--with-debug` when `EDK2_CORE_DEBUG_PATCHES=1`.
Re-running is safe: an already applied patch is detected and skipped.

## Required

### `0001-PeilessSec-flush-FD-before-DecompressFirstFv.patch`

`ArmConfigureMmu()` enables the D-cache without flushing it first. On RK3576 the
FD is loaded into DRAM by TF-A as BL33, and BL31 has already pulled that same
region into cache while verifying the FIT hash. Those stale lines shadow the
DRAM contents when `DecompressFirstFv()` reads the LZMA section header, and
decompression fails.

Adds a `WriteBackInvalidateDataCacheRange()` (DC CIVAC) over `PcdFdBaseAddress`
… `+PcdFdSize` immediately before the decompress call.

Applies to any platform whose FD is placed in DRAM by an earlier boot stage, so
it is a reasonable upstream candidate.

### `0002-DefaultExceptionHandler-print-ESR-FAR-unconditionally.patch`

The handler's register dump goes through `DEBUG()`, which is filtered by
`PcdDebugPrintErrorLevel`. On a RELEASE build the four values that actually
identify a fault (ESR, FAR, SP, LR) are the ones least likely to survive that
filter, which leaves an unattributable "Synchronous Exception at 0x…" and
nothing else.

Adds a `SerialPortWrite()` of ESR / FAR / SP / LR / EC alongside the existing
banner. This is what makes the crash-triage workflow (decode DFSC, `addr2line`
against `DxeCore.debug`) possible on a shipping image.

### `0005-SdMmcPciHcDxe-TRB-timeouts-are-wall-clock-microseconds.patch`

`SdMmcWaitTrbEnv()` and `SdMmcWaitTrbResult()` treat their `Timeout` as a
count of loop iterations, one `Stall (1)` each. Every iteration also does
several MMIO reads and error-recovery work, so on RK3576 one "microsecond"
costs far more than one. A 2.5 s data timeout took minutes. When the eMMC
answered a cold-boot EXT_CSD read with CRC errors, the firmware sat in these
loops long enough to look hung.

Measures elapsed time with `GetPerformanceCounter()` instead, so the timeout
means what it says. The error itself still has to be fixed in the host driver
(`DwcSdhciDxe`); this only stops it from costing minutes.

### `0006-MdeModulePkg-UI-add-zh-Hans-strings.patch`

Adds Simplified Chinese (`zh-Hans`) to the stock setup menus: the front page
(UiApp), Device Manager, Boot Manager, Boot Maintenance Manager and the Boot
Discovery Policy form.  String files only, no code.

Every string in each of those files gets a `zh-Hans` entry, including the ones
that read the same in both languages (baud rates, terminal types).  That is
not optional: HiiDatabase does not fall back to English for a string that is
missing in the selected language, and the setup browser draws it as "!".
`scripts/gen-zh-hans-font.py check` enforces this, and finds the glyphs these
strings need for `Silicon/Rockchip/Drivers/ZhHansFontDxe`.

The front page's language list takes each language's name from this file
(`#langdef`), which is why UiApp needs the patch even for the platform's own
forms to be selectable in Chinese.

The setup browser's own strings (key help, pop-ups) are in 0009.

### `0007-zh-Hans-strings-for-the-Device-Manager-forms.patch`

The forms of the TianoCore drivers these boards build, in Simplified
Chinese, every string: RAM disk, Secure Boot, EFI Memory Attribute Protocol
(EmbeddedPkg's MemoryAttributeManagerDxe), and the per-NIC network forms
(VLAN, IPv4, IPv6, HTTP boot, TLS authentication). String files only.

Three texts in these drivers are built in C and stay English or stay in the
language the board booted in: the "(MAC:...)" suffix the IPv4, IPv6 and HTTP
boot drivers append to their help line once at start, VlanConfigDxe's whole
"VLAN Configuration (MAC:...)" help line, and Secure Boot's
"Enabled"/"Disabled" state, which is a C literal.

### `0008-UiApp-rebuild-the-front-page-after-a-language-change.patch`

The front page lists the other menus (Device Manager, Boot Manager, Boot
Maintenance Manager, and any driver formset of the front page class) by
copying their titles into its own strings when the page is built, in the
language current then, with one text for every language. "Select Language"
only wrote PlatformLang, so those entries kept the old language until the
next boot. After a language change the menu is now built again
(`UpdateFrontPageForm ()`), which takes the titles in the new language; the
browser reparses the form when it sees the update. This was chosen over
storing each title per language because it is one call in the callback and
covers every copied entry at once.

The banner lines (CPU, memory, firmware version) are data from SMBIOS and
PCDs, the same in every language, and are not rebuilt.

### `0009-DisplayEngine-follow-the-language-and-show-touch-key-help.patch`

DisplayEngineDxe and CustomizedDisplayLib (the key help bar at the bottom,
the save / discard / reset pop-ups, the F9 / F10 hints) in Simplified
Chinese, and two code changes:

- Both load their strings once, at driver entry. DisplayEngineDxe's depex
  does not include the variable services, so that can be before PlatformLang
  is readable, and it is never redone after "Select Language". Now
  `FormDisplay ()` and `DisplayPageFrame ()` reload them when PlatformLang
  differs from the language they were loaded in, and the F9 / F10 hot keys
  are registered again with the new help text.
- With a touch screen that types keys (a device that is both a text input and
  an absolute pointer, other than the console splitter; TouchKeysDxe on the
  TaishanPi 3M) the key help names the gestures too: `<Enter>/tap=Select
  Entry`, `Esc/2 fingers=Exit`, `/drag=Move Highlight`. Without one the bar
  is exactly as before. All of these fit the bar's columns at 80 columns.

Left as they are, because their first character is the key the pop-up waits
for: `ARE_YOU_SURE_YES` "Y", `ARE_YOU_SURE_NO` "N" (`ConfirmDataChange ()`
compares `gYesResponse[0]` / `gNoResponse[0]`), `CONFIRM_OPTION_YES` "Y (y)",
`CONFIRM_OPTION_NO` "N (n)", `CONFIRM_OPTION_OK` "O (o)",
`CONFIRM_OPTION_CANCEL` "C (c)", `DISCARD_OR_JUMP_DISCARD` "D (d)",
`DISCARD_OR_JUMP_JUMP` "G (g)" and `DISCARD_OR_CHECK_CHECK` "C (c)"
(`FormDisplay.c` and `Popup.c` compare their `[0]`). The Chinese prompts that
explain them keep the same Latin letters.

### `0010-DisplayEngine-break-lines-between-CJK-characters-not-inside-words.patch`

Applies on top of 0009. `GetLineByWidth ()`, which wraps prompts, options and
help text, could only break at a space, else at the last character that fit.
Chinese has no spaces, so it broke after the last character that fit, and a
"。" or "，" that did not fit started the next line on its own; a Latin word
inside Chinese text ("Synchronous") was cut in two the same way.

It now breaks at the last allowed place in the line: before a space as
before, or between two characters of which one is wide, unless the next line
would start with closing punctuation (。，、；：！？）」』 and ASCII `) , . : ; ! ?`)
or this one end with opening punctuation (（「『 and `( [ { <`). Between two
narrow characters, inside a word, it does not break. Only a run that is
wider than the whole line (a long URL, a GUID) is still cut where it overflows.
English text breaks exactly where it did.

Simulated over every zh-Hans string at the help widths of 80, 100, 128 and
160 columns: lines starting with closing punctuation 36 before, 0 after
except inside such over-long runs; Latin words split 20 before, 0 after.

### `0013-DisplayEngine-select-the-statement-a-mouse-clicks.patch`

Applies on top of 0009 and 0010, and after 0012 when that is there; it
touches none of their lines. A mouse click in the menus used to be Enter,
which opened the highlighted entry and not the one under the pointer.
Now, while the display engine waits for input, a left click picks what it
lands on: on a statement it moves the highlight there, and on the
highlighted statement it is Enter. The same holds for the options of a
one-of popup and the buttons of a Yes/No popup. A click on anything else
(blank rows, help text, the title, the key bar) does nothing. Right click
and the wheel stay keys (Esc, Up, Down).

The display engine does not know where a pointer is. The new
`PointerClick.c` looks for `DISPLAY_POINTER_CLICK_PROTOCOL`, which
OnScreenKeyboardDxe installs: it tells the driver while it waits
(`SetReading`), and gets each click as the text cell it landed on
(`WaitForClick`, `GetClick`). The driver works that cell out from the
picture, the console's columns and rows and GraphicsConsole's 8x19 cells
centred in it, so the rotation is already accounted for. With no such
protocol nothing changes. The structure is also defined in
`Silicon/Rockchip/Include/Protocol/RockchipOnScreenKeyboard.h`; the two must
stay the same.

## Optional (`--with-debug`)

### `0003-DEBUG-PeilessSec-UART-checkpoints.patch`

Emits `[A]` … `[K]` on the UART between the SEC phases. Locates a silent
pre-DXE hang without a DEBUG build, which does not fit the FV.

### `0004-DEBUG-PrePiLib-dump-HOB-list-before-DxeCore.patch`

Dumps the DxeCore load address and the complete HOB list (resource descriptors,
FV, memory allocations, handoff) just before the handoff. This is the direct way
to answer "is region X actually reserved"; see the RK3576 memory-map notes in
`Silicon/Rockchip/RK3576/Library/PlatformLib/Rk3576Mem.c`.

Both are noisy by design; keep them off for release images and for any
measurement where UART latency could matter.

## Adding a patch

Do not edit `third_party/edk2/` and stop there. Generate the patch too:

```sh
cd third_party/edk2
git diff -- path/File.c > ../../patches/000N-what-it-does.patch
```

Name it `NNNN-DEBUG-...` if it is tracing rather than a fix. That prefix is
what `apply.sh` keys on to decide whether it is opt-in.

### `0011-TerminalDxe-send-non-ASCII-text-as-UTF-8.patch`

The serial console is a TTYTERM terminal, and for every terminal type but
VT-UTF8 `TerminalConOutOutputString ()` turned a character that is neither
ASCII nor a box or arrow glyph into `?`. With the menus in Chinese the
serial console showed rows of question marks. Such characters are now sent
as UTF-8, and an East Asian wide character advances the driver's cursor by
two columns, as it does on a terminal and on the graphics console. English
output is unchanged. Switching the terminal type to VT-UTF8 was not an
option: its function keys differ, and `PlatformBm.c` asserts TTYTERM.

### `0012-UiApp-add-Shut-Down-to-the-front-page.patch`

Applies on top of 0006 and 0008. Adds "Shut Down" / "关机" under "Reset" on
the front page, which calls `ResetSystem (EfiResetShutdown)`. On the
TaishanPi 3M that reaches the PMIC through Rk806I2cPowerOffDxe; on the other
boards it is the PSCI SYSTEM_OFF it always was. The patch also adds the
missing `break` after the Reset case, which used to fall through to
`default`.

### `0017-FileExplorerLib-add-zh-Hans-strings.patch`

Independent of the others. The file explorer ("Boot From File", "Add Boot
Option", "Add Driver Option") had no zh-Hans strings, so with the menus in
Chinese its title and its "new file" / "new folder" entries stayed English.
Every string gets a zh-Hans entry, in the same \wide ... \narrow form as
0006 and 0007. The file keeps its CRLF line endings.
