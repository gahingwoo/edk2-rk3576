# Booting Armbian's own U-Boot once ("Armbian (U-Boot)")

English | [简体中文](ARMBIAN-CHAINLOAD.zh-CN.md)

Status: merged into `main` on 2026-11-24 (`2187846`), built for the TaishanPi 3M.
Board-tested on the TaishanPi 3M the same day (merge `a23adb9`): with the SD
card in, a normal boot still reaches UEFI and Windows; "Armbian (U-Boot)"
boots the card's Armbian U-Boot and Armbian; the request sector reads back
zero; a reboot and a power cycle from Armbian both return to UEFI. The change
that lists the option only while there is a U-Boot to boot (`e081771`) was
merged after that test, and no run of it is recorded. The full board test is
at the end.

## What it does

The boot manager gets an entry **Armbian (U-Boot)**. Choosing it makes the
next boot run Armbian's own U-Boot, which runs Armbian's `boot.scr` with
Armbian's kernel and device tree. Any boot after that (a reboot from Armbian,
a power cycle, a crash, a U-Boot that hangs and is power-cycled) is this
firmware again.

| Goal | How it is met |
|---|---|
| G1 one choice in the boot manager | boot option `Armbian (U-Boot)` (application `ArmbianOnce.efi`) |
| G2 one shot | the SPL erases the request, and reads the sector back, before it loads anything |
| G3 Armbian's DTB, U-Boot, kernel | the SPL loads Armbian's FIT (its BL31, U-Boot, U-Boot DT). EDK2 never runs on that boot, so none of its DT fixups or GOP settings exist |
| G4 nothing changed on the Armbian side | Armbian's FIT is read where Armbian put it, or copied from Armbian's own files each time the entry is used |
| G5 small | one U-Boot SPL patch (one new file of about 260 lines), one EDK2 application, a boot option |

## Why the request is on the eMMC

The first idea was a magic value in a PMU GRF OS register, read by the SPL.
That cannot work on these boards: **nothing on the chip survives a reset.**

Measured on the TaishanPi 3M (2026-11-22, read over the serial console):

* PMU1 GRF OS_REG15 (`0x2602623C`), documented as "with npor reset", was set
  to `0xB007000A` from the UEFI Shell with `mm`; after `reset` it read 0
  (commit 1266943; log around the `mm 0x2602623C 0xB007000A` line).
* DRAM: 0 of 3768 canaries in free memory survived a reset (commit fd2f0c3).
* PMU0 GRF OS_REG16 to 23 are "cannot be reset" in the TRM. OS_REG16 is the
  BOOT_MODE_REG of U-Boot and Linux (`0x26024040`). Nothing in this boot chain
  writes 16 or 20 to 23, yet they change across every reset, warm ones too.
  Two consecutive boots around a Linux `reboot`:
  `42157F6F .. A9057566 86545559 4579D675 1940A113` then
  `42157F6F .. A9055467 86545559 4171C655 3900A113`. That is power-on noise
  of unreset flops: the domain lost power.
* The HYM8563 reports VL (clock lost power) on every boot, after Linux and
  Windows reboots alike. The board has no RTC battery.

TF-A explains it (v2.14.0, de38734, the BL31 in `binaries/`; the rk3576 code is
the same at 2a313ed22):

* `soc.c` `system_reset_init()` writes `CRU_GLB_RST_CON = 0xffdf`: the global
  reset also resets the PMU domain (bit 3), for the *first* global reset
  (bit 2). It writes `PMU1SGRF_SOC_CON0 = 0x00070007`, "enable first/tsadc/wdt
  reset output".
* `pmu.c` `rockchip_soc_soft_reset()` (PSCI SYSTEM_RESET) writes
  `CRU_GLB_SRST_FST = 0xfdb9`, the first global reset. Its reset output goes to
  the PMIC, which power-cycles the SoC.
* The one exception proves the rule: for `BOOT_BROM_DOWNLOAD` in PMU0
  OS_REG16, `rockchip_soc_soft_reset_check_rstout()` turns that output off
  before the reset, with the comment "os_reg0 ... will be reset by NPOR". That
  is how Reset to MaskROM works on this board, and it needs secure-only SGRF
  writes, so EDK2 cannot do the same.

Options considered:

| Carrier | Verdict |
|---|---|
| PMU0/PMU1 GRF OS register | cleared by the power cycle (measured) |
| PMU SRAM, `mem_os_reg` at its top | same power domain, same fate |
| DRAM | wiped (measured) |
| RTC registers | the RTC loses power on every reset on the TaishanPi (measured) |
| Second global reset (`CRU_GLB_SRST_SND = 0xeca8`, TRM 2.3.2), written by EDK2 directly | Plausible: it is not selected for the PMU reset (`GLB_RST_CON` bit 2 = first) and is not in TF-A's reset-output list. But nobody runs it on RK3576: unknown whether the BootROM and ddrbin come up cleanly after it, PMIC rails and board peripherals are not cycled. Not chosen. Measurement for later: `mm 0x2602623C 0x12345678 -w 4 -n`, then `mm 0x27200C0C 0xECA8 -w 4 -n` from the Shell; the board should restart and the boot log line `PMU1 GRF OS_REG0-15` show `12345678` last |
| **One eMMC sector** | **chosen.** Survives anything, EDK2 already writes the eMMC (nvdata), the SPL already reads it. Costs the SPL a one-sector write (`CONFIG_SPL_MMC_WRITE`), only when a request is there |

## On-disk layout

eMMC user area, 512-byte sectors:

| Sector | What | Written by |
|---|---|---|
| 64 | idbloader (DDR init + SPL) | flashing |
| **0x3FFF (16383)** | **request record** | ArmbianOnce, erased by the SPL |
| 0x4000 (16384) | firmware FIT (BL31 + EDK2 + DTB + nvdata) | flashing |
| **0x10000..0x17FFF (32..48 MiB)** | **slot: a copy of Armbian's FIT** | ArmbianOnce, only when Armbian is not on the SD card |
| 0x20000 (64 MiB) | first partition in `package.sh`'s layout (and the Windows ESP on the TaishanPi) | |

Sector 0x3FFF is in the zero gap between the idbloader and the FIT on every
Rockchip boot medium (Armbian's `u-boot-rockchip.bin` covers it with zeros
too). The images from `package.sh` write it as zero, so flashing clears a
request. The slot is past the 32 MiB the images cover; ArmbianOnce refuses to
use it if any partition other than the `firmware` one (type
`8DA63339-0007-60C0-C436-083AC8230908`) overlaps it.

The record (`Silicon/Rockchip/Include/ArmbianOnce.h`, and
`struct ao_request` in the SPL):

| Offset | Field |
|---|---|
| 0 | `"ARMBONCE"` |
| 8 | version, 1 |
| 12 | target: 1 SD card FIT at 0x4000; 2 eMMC slot; 3 eMMC FIT at 0x4000 |
| 16 | slot sector (0x10000 for target 2, else 0) |
| 20 | reserved |
| 508 | CRC-32 (IEEE, as `crc32(0, ...)` and `CalculateCrc32`) of bytes 0..507 |

## The SPL side

`uboot/v2026.10/0001-rockchip-spl-boot-another-U-Boot-FIT-once-on-request.patch`
adds `arch/arm/mach-rockchip/spl-armbian-once.c` and
`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE`. It uses two weak hooks of mainline SPL,
`board_spl_mmc_get_uboot_raw_sector()` and `spl_perform_board_fixups()`, and
touches no existing code.

Our SPL for the TaishanPi 3M is mainline U-Boot v2026.10 with Armbian's
`lckfb-tspi-3m-rk3576_defconfig` and DTS, DDR blob v1.08, and the one local
change that it tries the SD card first (`binaries/README.md`). Before this work it was
built by hand; `scripts/build-spl.sh tspi3m` now builds it from pinned inputs
(U-Boot tag and commit, the board files in `uboot/v2026.10/tspi3m/`, the
patches, the DDR blob from armbian/rkbin by SHA-256) into
`out/TaishanPi3M/idbloader.img`, which `package.sh` already prefers. CI runs it.
Built without the feature, the same script gives an SPL whose strings are
identical to `binaries/idblock_tspi3m.bin` except the version line.

On every boot, at the first MMC device it tries, the SPL:

1. finds the eMMC (the MMC device that is not an SD card), and notes whether
   its FIT at 0x4000 is the firmware's (`/images/edk2`) or another one;
2. reads sector 0x3FFF; if it is a valid request, zeroes it and reads it
   back. If the erase did not stick, it boots the firmware;
3. per device, in the board's order (SD card, then eMMC):
   * request for the SD card: the SD card's FIT, if it is not the firmware's;
     otherwise it falls through to the firmware;
   * request for the eMMC slot or the eMMC FIT: skips the SD card, loads the
     slot (once) or the eMMC FIT; a second visit of the eMMC gets the firmware;
   * no request: a foreign FIT on the SD card is **skipped** if the eMMC has
     the firmware. Without that an Armbian card in the slot would take every
     boot, which is what the SD-first SPL did until now. With no firmware on
     the eMMC the card boots as before, so it still works as a rescue card;
4. on the foreign path only, sets `/config/bootcmd` in the loaded U-Boot's
   control DT to `setenv bootmeths script extlinux; bootflow scan -lb`
   (`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_BOOTCMD`, empty to disable). See risk R2.

A FIT whose hashes fail is reported by the SPL, which moves on to the next
device, so a damaged Armbian FIT on the SD card ends in the firmware.

## The EDK2 side

`Silicon/Rockchip/Applications/ArmbianOnce` is built into boards that set
`RK_ARMBIAN_ONCE_ENABLE` (the TaishanPi 3M only, for now).
`PlatformBootManagerLib` lists it as `Armbian (U-Boot)` only while it has
something to boot. On every boot, after the disks are connected and before the
boot manager reads its options, it connects the SD controller (in case the boot
discovery policy did not) and asks `ArmbianOnceProbe()` (`ArmbianOnceLib`,
shared with the application). The option is there when the SPL takes requests
and there is an arm64 U-Boot FIT (an image with `os = "u-boot"`,
`arch = "arm64"`, and no image for another architecture) at sector 0x4000 of the
SD card, of the eMMC (firmware on the SD card), or of a USB or NVMe disk, or
already in the eMMC slot. Otherwise the Boot#### variable is deleted, which also
takes it out of BootOrder. With no card the SD disk has no media, so the check
reads nothing from it and only a sector or two of the eMMC; the 1 MiB SPL read
happens only when a U-Boot was found. A card put in after the boot shows the
option on the next boot.

The file-system search for `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` is
not part of that check (it would cost a directory walk of every file system on
every boot). So Armbian that lives only in a partition of the eMMC, with no
card, USB disk or filled slot, does not get the option. Once the slot holds a
U-Boot (filled from a USB disk), it does.

The option is not active, so the boot order never picks it, and it has no
hotkey. It is added at the end of BootOrder, and moved back to the end if
anything put it first. The variables are written only when something changes.
It can also be reached as BootNext (`efibootmgr -n` from a Linux booted through
this firmware, with "Save OS Variable Changes to eMMC" on).

The application:

1. finds the eMMC and refuses unless the SPL at sector 64 (of the eMMC, or of
   the SD card) contains the tag `ArmbianOnce/1`, so an older SPL cannot leave
   a request lying around for a later one to act on;
2. picks the target:
   * a foreign FIT at sector 0x4000 of the SD card: target 1;
   * a foreign FIT at sector 0x4000 of the eMMC (this firmware then runs from
     the SD card): target 3;
   * otherwise it copies Armbian's FIT into the eMMC slot (target 2) from,
     in this order, sector 0x4000 of another whole disk (USB, NVMe), or the
     newest `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` on any file system
     it can read (Ext4Dxe is in the firmware), taking the FIT at offset
     `(0x4000 - 64) * 512` of that file. The FIT's length comes from its
     images' `data-offset`/`data-position` and `data-size`. It writes only if
     the slot differs, and reads every chunk back;
3. writes the record, reads it back, and restarts (cold; the reset type does
   not matter for a record on the eMMC).

Every failure stops before the record is written and says why.

## SPI NOR boards (Radxa ROCK 4D)

The ROCK 4D has no eMMC: its SPL and the firmware FIT are in the SPI NOR
(idblock at 0x8000, FIT at 0x60000), and the SPL's boot order is
`"same-as-spl"` (the SPI NOR), then the SD card. The SPL cannot write the SPI
NOR cheaply, and a request has to be erased by the SPL before it loads
anything, so the request lives on the SD card instead, at the same sector
0x3FFF, which is in the zero gap of an Armbian SD image too. Two switches,
both off by default:

* SPL: `CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_SD_REQUEST`
  (`uboot/v2026.10/0002-...patch`). The record is read and erased on the SD
  card, and the only target accepted is 1, the FIT at sector 0x4000 of that
  card. On a requested boot `spl_spi_get_uboot_offs()` returns 0, where the
  SPI NOR has its partition table and no FIT; with `SPL_RAW_IMAGE_SUPPORT`
  off the SPL refuses that payload and goes on to the SD card, where the MMC
  hook lets it load the card's FIT. Without a request the SPI offset is
  `CONFIG_SYS_SPI_U_BOOT_OFFS` (0x60000), so a normal boot is the SPL's
  ordinary SPI boot. If the card holds no U-Boot FIT once the request has
  been erased, the SPL says so and boots the firmware.
* EDK2: `PcdArmbianOnceRequestOnSd` (RockchipPkg.dec). The boot option and
  the application look for the SPL tag in the SPI NOR (read through the NOR
  flash driver, 0x8000..0x5FFFF), for a U-Boot FIT at sector 0x4000 of the SD
  card only, and write the request to sector 0x3FFF of the SD card. No slot,
  no copy from USB, NVMe or a file system: the SPL could not read a slot
  anywhere but on the card, and on the card sector 0x10000 is inside
  Armbian's root partition.

For the ROCK 4D, `boards/rock4d.conf` has `build-spl.sh` build U-Boot
v2026.10's own `rock-4d-rk3576_defconfig` and device tree, unchanged, with
both patches and `uboot/v2026.10/rock4d/spl.config` (XMC flash support for
v1.12 boards, `SPL_MMC_WRITE`, the two switches) and the DDR blob v1.09 that
`binaries/idblock_mainline.bin` (U-Boot 2026.04) carries. That replaces the
SPL every ROCK 4D image ships with, so it is on branch `rock4d-parity` only,
until a board has booted it.

## What can be booted this way

The SPL is mainline U-Boot's; it loads any FIT that `spl_load_simple_fit()`
loads: TF-A as `firmware` (`os = "arm-trusted-firmware"`), U-Boot and
optionally OP-TEE (`os = "tee"`, passed to BL31 as BL32 by `spl_atf.c`) as
loadables, a U-Boot DT, hashes in SHA-1, SHA-256 or CRC-32. External or
embedded data both work. The FIT's own BL31 is the one that runs.

Whether the U-Boot inside then works depends on what it expects from the
stages before it:

* **Mainline U-Boot (Armbian edge and vendor for the TaishanPi 3M, Armbian
  edge for the CM5-IO).** Armbian builds it from the same tag (v2026.10), the
  same defconfig and DTS, with the same DDR blob (rkbin v1.08) and its own
  rkbin BL31 v1.20. Its SPL is ours minus the SD-first order and this patch,
  so it gets exactly the hand-off it was built for: DRAM size in PMU1 GRF
  OS_REG2 to 5 and the DDR_MEM ATAG at DRAM + 2 MiB - 8 KiB, both written by
  the DDR blob, and `u-boot,spl-boot-device` in `/chosen`, written by
  `spl-boot-order.c`. This combination has already run: on 2026-11-16 the
  SD-first SPL booted Armbian's U-Boot from a plain Armbian card. Armbian's
  FIT has no OP-TEE.
* **A different DDR blob.** DRAM is trained by our blob; a U-Boot built for
  another blob version reads the same OS_REG and ATAG formats, so it should
  not care, but that is not tested.
* **Rockchip's vendor U-Boot (2017.09 next-dev and its forks).** It expects
  the vendor SPL's ATAGs (boot device, serial, TOS memory) and usually the
  vendor `uboot.img`/`trust.img` layout. Not supported; may half work. Armbian
  does not use it on these two boards. The CM5-IO vendor branch uses Radxa's
  `next-dev-v2024.10`, which is mainline-based; untested.
* **A U-Boot that needs its own TPL or SPL** to have done something (Falcon
  mode, VBE state, a TPL that leaves different DRAM or clock state, secure
  boot keys in its SPL DT) is not supported.

## Supported matrix

| Armbian is on | SPL loads Armbian's U-Boot from | Kept in step with Armbian upgrades | Status |
|---|---|---|---|
| SD card, firmware on the eMMC | SD sector 0x4000, in place | always: it is Armbian's own copy | main case, TaishanPi 3M |
| eMMC, firmware on the SD card (SPL on the eMMC) | eMMC sector 0x4000, in place | always | supported |
| USB disk | eMMC slot, copied from the disk's sector 0x4000 on every use | yes, on each use | supported if Armbian's U-Boot finds `boot.scr` on USB (its bootflow scans USB). USB-C under U-Boot has no FUSB302 driver; use USB-A |
| NVMe | eMMC slot, as for USB | yes, on each use | same code path; the TaishanPi has no M.2, so untested |
| a partition of the eMMC that also holds the firmware | eMMC slot, copied from `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` | yes, on each use: every apt upgrade of `linux-u-boot-*` replaces that file | supported. Armbian must never write its bootloader there (see R5) |
| the same disk as Armbian's FIT at 0x4000 when the firmware is also there | impossible: one sector, one FIT | | |
| ROCK 4D: SD card, firmware on SPI NOR | SD sector 0x4000, in place | always | built on branch `rock4d-parity`, not run on a board; see [SPI NOR boards](#spi-nor-boards-radxa-rock-4d). Armbian on USB or NVMe is not supported there: there is no eMMC for a slot |
| CM5-IO | design applies (eMMC SPL) | | not built in: its SPL (2026.07-rc3 with the carrier overlay) is not rebuilt by `build-spl.sh` yet, and it needs the SD-first boot order for the SD case |

Armbian's U-Boot package only writes the bootloader to a disk when
`FORCE_UBOOT_UPDATE=yes` (its `postinst`) or through `armbian-install`. So the
FIT at 0x4000 on an Armbian SD card or USB disk is whatever the image or the
user last put there, and ArmbianOnce uses that, current or not. That is
Armbian's own state, which is G4.

## Risks

* **R1 behaviour change**: an Armbian SD card no longer boots by just being
  in the slot when the eMMC has the firmware. That is G2. The firmware is
  required on the eMMC for the skip, so a card still rescues a board whose
  eMMC has no firmware FIT.
* **R2 Armbian's U-Boot boot command is overridden on this path.** Measured
  2026-11-16: with Windows on the eMMC, Armbian's mainline U-Boot started the
  eMMC's Windows Boot Manager through its EFI boot manager (U-Boot makes boot
  options for every disk with an ESP) and hung, before it reached `boot.scr`
  on the card. `/config/bootcmd` restricts it to the `script` and `extlinux`
  bootmeths. The U-Boot binary is Armbian's; only that command differs. An
  Armbian image that boots through EFI from U-Boot does not boot on this path
  (it boots through this firmware anyway). Set the Kconfig string empty to
  get Armbian's own `bootcmd`.
* **R3 the SPL writes the eMMC**, one sector, only when a request is there.
  A power cut in that write leaves a sector that fails the CRC: no request.
* **R4 the slot** is checked against the partition table, not against data
  outside partitions. 32 to 48 MiB is unpartitioned and unused in every
  layout this project ships or documents.
* **R5 Armbian on the same eMMC**: `armbian-install`, `armbian-config`'s
  bootloader update or `FORCE_UBOOT_UPDATE=yes` would write
  `u-boot-rockchip.bin` at sector 64 and over the firmware. Nothing on our
  side can stop that.
* **R6 a hang inside Armbian's U-Boot** needs a power cycle (no watchdog);
  the boot after that is the firmware.
* **R7** the SPL now brings up the eMMC on every boot, also when the firmware
  is on the SD card (a few milliseconds).
* **R8** BootNext set from Windows does not survive a reset on the TaishanPi
  (the board has no RTC battery and the RTC loses power on every reset, see
  [hardware](dev/hardware.md#lckfb-taishanpi-3m)), so "restart into
  Armbian" from Windows is not available; from the boot manager it is.

## Building

```bash
scripts/build.sh tspi3m
scripts/build-spl.sh tspi3m     # clones U-Boot v2026.10 into third_party/u-boot once
scripts/package.sh tspi3m       # picks up out/TaishanPi3M/idbloader.img
```

`UBOOT_DIR` points at an existing U-Boot clone, `RKBIN_DIR` at a directory
with the DDR blob, instead of downloading. Without `build-spl.sh` the image
gets `binaries/idblock_tspi3m.bin`, and the boot option says that the SPL
cannot do it.

When U-Boot is bumped: copy `uboot/v2026.10` to the new version, refresh the
board files from Armbian's `patch/u-boot/<version>`, rebase the patch (it
touches only `arch/arm/mach-rockchip/{Kconfig,Makefile}` and adds one file),
and change `SPL_UBOOT_TAG`/`SPL_UBOOT_COMMIT`/`SPL_PATCH_DIR`/`SPL_BOARD_DIR` in
`boards/tspi3m.conf`.

## Board test (TaishanPi 3M)

Needs: an Armbian SD card for the TaishanPi 3M (mainline U-Boot image; the card
from 2026-11-16 is one), serial console at 1500000, MaskROM access. The
nvdata offset of this build is unchanged (`0x1550000`, sector 43648), and the
update below writes only the SPL and the firmware slice, so the UEFI
variables stay.

1. Flash, from MaskROM (or `dd` from a running Linux, same sectors):

   ```bash
   dd if=TaishanPi3M-emmc.img of=spl.bin bs=512 skip=64 count=400
   dd if=TaishanPi3M-emmc.img of=firmware.bin bs=512 skip=16384 count=27264
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 spl.bin
   rkdeveloptool wl 16384 firmware.bin
   rkdeveloptool rd
   ```

2. No card, three cold boots: `U-Boot SPL 2026.10-edk2-rk3576`, no
   `ArmbianOnce/1` line, firmware and Windows as before. `Armbian (U-Boot)` is
   in the boot manager.
3. Card in, reboot: `ArmbianOnce/1: not booting the U-Boot FIT on the SD card,
   the firmware is on the eMMC`, then the firmware. (Without this change the
   card would have booted.)
4. Boot manager, `Armbian (U-Boot)`: `Found a U-Boot FIT on the SD card.`,
   `Request written. Restarting`. Next boot: `ArmbianOnce/1: request found and
   erased: booting U-Boot from the SD card, once`, `ArmbianOnce/1: U-Boot will
   run "setenv bootmeths script extlinux; bootflow scan -lb"`, Armbian's
   `U-Boot 2026.10` banner (its own build date), `boot.scr`, Armbian.
   In Armbian: `/sys/firmware/efi` is absent, `/proc/device-tree/model` and
   `/proc/cmdline` are Armbian's, and
   `dd if=/dev/mmcblk0 bs=512 skip=16383 count=1 status=none | tr -d '\0' | wc -c`
   prints 0 (use the eMMC's device name; with the card in it may differ).
5. One shot: from Armbian `sudo reboot`: firmware. Choose the entry again,
   then in Armbian `echo b | sudo tee /proc/sysrq-trigger`: firmware. Choose
   it again and pull the power while Armbian's U-Boot is up: firmware.
   Repeat step 4 at least three times in all.
6. Card out, boot: `Armbian (U-Boot)` is not in the boot manager (and not in
   BootOrder). Card in, boot: it is back, last in the list.
7. Card pulled after the request: choose the entry, and while the screen says
   `Restarting`, pull the card. SPL: `request found and erased: booting U-Boot
   from the SD card, once`, then `U-Boot was not loaded from the SD card,
   booting the firmware`, then the firmware from the eMMC; sector 16383 is zero
   and the next boot is a normal one.
8. Optional, slot path: card out, an Armbian USB disk on a USB-A port, choose
   the entry: `Found a U-Boot FIT on ...`, `Copying it to the eMMC slot`, SPL
   `booting U-Boot from the eMMC slot, once`; whether Armbian's U-Boot finds
   the disk is the open part. A second use prints `The eMMC slot already holds
   that U-Boot.` With the USB disk gone too, the option stays (the slot holds a
   U-Boot) and the application says `Using the U-Boot copied to the eMMC slot
   earlier`.

Send the serial log of steps 3 to 5 back; the SPL lines are the evidence.
