# Testing

English | [简体中文](testing.zh-CN.md)

## What CI checks

`.github/workflows/build.yml` builds every board on every push to `main` and
on pull requests, and checks that the variable store in each SD and eMMC
image is where `package.sh` expects it. It runs nothing on hardware. A
successful build of `main` replaces the `ci` pre-release.

## On a board

There is one unit of each board. Results go into the merge commit
("Board-verified on the TaishanPi 3M on <date>: ...") and the release notes
("How it was tested"). Several branches are often combined into one
`test-all-<date>` image and run together on the TaishanPi 3M; the merge
message names that image.

## Last recorded board runs

| Board | Last run | Notes |
|---|---|---|
| TaishanPi 3M | `main` through 2026-11-24 | Windows 11 23H2 and Armbian from the eMMC |
| CM5-IO | firmware 0.4.x | has booted an OS with 0.4.x |
| ROCK 4D | after 0.2.1, version not recorded | 0.2.1's SPL run by a user on a v1.12 board (XMC flash); later changes are shared code |

## Run on the TaishanPi 3M only

- network time sync, RTC read-out, CPU idle states, the CPU frequency option,
  thermal shutdown at 120 C;
- saving OS variable changes to the eMMC at ResetSystem (`efibootmgr -n`
  and `-o`);
- USB-C VBUS on for a sink attached at boot, off for a charger;
- GPU rail at 775 mV with the Windows driver at 702 MHz;
- Bluetooth, headphone audio and HDMI audio under Windows.

The 2026-11-14 list of changes merged without a board check is in
[history](../history/2026-11-22-hardware-before-rewrite.md).
