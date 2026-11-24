# Testing

English | [简体中文](testing.zh-CN.md)

What "tested" means for this firmware, and what has been run where.

## What CI checks

`.github/workflows/build.yml` builds every board on every push to `main` and
on pull requests. It checks that the images build and that the variable
store in each SD and eMMC image is where `package.sh` expects it. It does
not run anything on hardware. A successful build of `main` replaces the
`ci` pre-release.

## On a board

There is one unit of each board. Results are written into the merge commit
("Board-verified on the TaishanPi 3M on <date>: ...") and into the release
notes ("How it was tested").

Several branches are often combined into one test image (named
`test-all-<date>` and similar) and run on the TaishanPi 3M together; the
merge message then names that image.

## Last recorded board runs, 2026-11-24

| Board | Last recorded run | Notes |
|---|---|---|
| TaishanPi 3M | `main` builds through 2026-11-24 | Windows 11 23H2 and Armbian from the eMMC. The GPU rail merge (`4479752`) was run on 2026-11-24. |
| CM5-IO | firmware 0.4.x | Has booted an operating system with 0.4.x. Release 0.4.0 recorded the menus only. |
| ROCK 4D | after 0.2.1, version not recorded | 0.2.1's SPL was run on a v1.12 board (XMC flash) by a user. Changes since then are in code shared with the other two boards. |

## Merged and run on the TaishanPi 3M only

Checked on the TaishanPi 3M, not on the other boards:

- network time sync, RTC read-out, CPU idle states and the CPU frequency
  option, thermal shutdown at 120 C (test-all images, 2026-11-22);
- writing OS variable changes to the eMMC at ResetSystem, with the setup
  option on (`efibootmgr -n` and `-o`, 3 runs, 2026-11-22);
- USB-C VBUS for a sink attached at boot, and VBUS off for a charger
  (2026-11-22 and 2026-11-23);
- the GPU rail at 775 mV with the Windows driver at 702 MHz (2026-11-24);
- Bluetooth, headphone audio and HDMI audio under Windows (2026-11-22 and
  2026-11-23).

The bring-up list of 2026-11-14, with what was merged then without a check,
is kept in [history](../history/2026-11-22-hardware-before-rewrite.md).
