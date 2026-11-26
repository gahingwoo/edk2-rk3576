# Testing

English | [简体中文](testing.zh-CN.md)

## What CI checks

`.github/workflows/build.yml` builds every board on every push to `main` and
on pull requests, and checks that the variable store in each SD and eMMC
image is where `package.sh` expects it. It runs nothing on hardware. A
successful build of `main` replaces the `ci` pre-release.

The docs are checked too. `docs.yml` runs `scripts/check-docs --edk2`
(relative links, repository paths in backticks, Pcd and DEFINE names, em and
en dashes) and `scripts/gen-docs --check` (the sections between
`<!-- generated:NAME -->` and `<!-- /generated -->`, made from
`boards/*.conf`, `scripts/package.sh` and `RK3576.fdf`). The
`docs-generated` job of `build.yml` checks the sections made from the
boards' `features.json` and the settings sector in the built images. When a
check fails, run `scripts/gen-docs` (with `--features <dir>` holding the
`features.json` files, for example from `gh run download`) and commit the
result.

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
| ROCK 4D | `main` on 2026-11-26 | Windows 11 23H2 from NVMe (installed by Maskrom Studio), 12 GB board. Fedora last run after 0.2.1; 0.2.1's SPL run by a user on a v1.12 board (XMC flash) |

## Run on the TaishanPi 3M only

- network time sync, RTC read-out, CPU idle states, the CPU frequency option,
  thermal shutdown at 120 C;
- saving OS variable changes to the eMMC at ResetSystem (`efibootmgr -n`
  and `-o`);
- USB-C VBUS on for a sink attached at boot, off for a charger;
- GPU rail at 775 mV with the Windows driver at 702 MHz;
- Bluetooth under Windows.

The 2026-11-14 list of changes merged without a board check is in
[history](../history/2026-11-22-hardware-before-rewrite.md).
