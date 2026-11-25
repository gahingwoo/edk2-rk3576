# Booting Armbian's own U-Boot once ("Armbian (U-Boot)")

English | [简体中文](ARMBIAN-CHAINLOAD.zh-CN.md)

Using it, not building it? Read the user guide: [Armbian (U-Boot)](armbian-boot.md).

Developer notes for the boot manager entry **Armbian (U-Boot)**. Board-tested
on the TaishanPi 3M (normal boot with the card in, the one-shot Armbian boot,
return to UEFI after reboot and power cycle); listing the entry only when a
U-Boot is found has not been run on a board. ROCK 4D: built on branch
`rock4d-parity`, not run. CM5-IO: not built in.

## What it does

Choosing the entry makes the next boot run Armbian's own U-Boot, which runs
Armbian's `boot.scr` with Armbian's kernel and device tree. Every boot after
that (reboot, power cycle, crash, a hung U-Boot that is power-cycled) is this
firmware again.

| Goal | How |
|---|---|
| one choice in the boot manager | boot option `Armbian (U-Boot)`, application `ArmbianOnce.efi` |
| one shot | the SPL erases the request and reads the sector back before it loads anything |
| Armbian's DTB, U-Boot and kernel | the SPL loads Armbian's FIT (its BL31, U-Boot, U-Boot DT); EDK2 does not run on that boot |
| nothing changed on the Armbian side | Armbian's FIT is read where Armbian put it, or copied from Armbian's files at each use |
| small | one SPL patch (one new file, about 260 lines), one EDK2 application, one boot option |

## Why the request is on the eMMC

Nothing on the chip survives a reset on these boards: TF-A's PSCI
SYSTEM_RESET is a first global reset whose reset output makes the PMIC
power-cycle the SoC (`soc.c` `system_reset_init()`, `pmu.c`
`rockchip_soc_soft_reset()`). Measured on the TaishanPi 3M:

| Carrier | Result |
|---|---|
| PMU0/PMU1 GRF OS registers | cleared (OS_REG16 to 23 come back as power-on noise) |
| PMU SRAM | same power domain |
| DRAM | 0 of 3768 canaries survived |
| RTC registers | the RTC loses power at every reset |
| second global reset (`CRU_GLB_SRST_SND = 0xeca8`) | might keep the PMU domain, but nobody runs it on RK3576 and the BootROM/ddrbin behaviour after it is unknown; not chosen |
| **one eMMC sector** | **chosen**: survives anything, EDK2 already writes the eMMC and the SPL already reads it |

To test the second global reset later, from the UEFI Shell:
`mm 0x2602623C 0x12345678 -w 4 -n`, then `mm 0x27200C0C 0xECA8 -w 4 -n`; the
boot log line `PMU1 GRF OS_REG0-15` should end with `12345678`.

## On-disk layout

eMMC user area, 512-byte sectors:

| Sector | What | Written by |
|---|---|---|
| 64 | idbloader (DDR init + SPL) | flashing |
| **0x3FFF (16383)** | **request record** | ArmbianOnce; erased by the SPL |
| 0x4000 (16384) | firmware FIT (BL31 + EDK2 + DTB + nvdata) | flashing |
| **0x10000..0x17FFF (32..48 MiB)** | **slot: a copy of Armbian's FIT** | ArmbianOnce, only when Armbian is not on the SD card |
| 0x20000 (64 MiB) | first partition in `package.sh`'s layout (Windows ESP on the TaishanPi 3M) | |

Sector 0x3FFF is in the zero gap between idbloader and FIT on every Rockchip
boot medium, and the `package.sh` images write it as zero, so flashing clears
a request. ArmbianOnce refuses the slot if any partition other than
`firmware` (type `8DA63339-0007-60C0-C436-083AC8230908`) overlaps it.

The record (`Silicon/Rockchip/Include/ArmbianOnce.h`, `struct ao_request` in
the SPL):

| Offset | Field |
|---|---|
| 0 | `"ARMBONCE"` |
| 8 | version, 1 |
| 12 | target: 1 SD card FIT at 0x4000; 2 eMMC slot; 3 eMMC FIT at 0x4000 |
| 16 | slot sector (0x10000 for target 2, else 0) |
| 20 | reserved |
| 508 | CRC-32 (IEEE, `crc32(0, ...)` / `CalculateCrc32`) of bytes 0..507 |

## The SPL side

`uboot/v2026.10/0001-rockchip-spl-boot-another-U-Boot-FIT-once-on-request.patch`
adds `arch/arm/mach-rockchip/spl-armbian-once.c` and
`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE`, using the weak hooks
`board_spl_mmc_get_uboot_raw_sector()` and `spl_perform_board_fixups()`.

The TaishanPi 3M SPL is mainline U-Boot v2026.10 with Armbian's
`lckfb-tspi-3m-rk3576_defconfig` and DTS, DDR blob v1.08, and SD-first boot
order. `scripts/build-spl.sh tspi3m` builds it from pinned inputs (U-Boot tag
and commit, board files in `uboot/v2026.10/tspi3m/`, the patches, the DDR blob
from armbian/rkbin checked by SHA-256) into `out/TaishanPi3M/idbloader.img`,
which `package.sh` prefers. CI runs it.

On every boot, at the first MMC device it tries, the SPL:

1. finds the eMMC and notes whether its FIT at 0x4000 is the firmware's
   (`/images/edk2`);
2. reads sector 0x3FFF; on a valid request, zeroes it and reads it back, and
   boots the firmware if the erase did not stick;
3. per device, in board order (SD, then eMMC):
   - request for the SD card: the card's FIT if it is not the firmware's,
     else the firmware;
   - request for the eMMC slot or eMMC FIT: skip the SD card, load the slot
     or the eMMC FIT once; a second visit gets the firmware;
   - no request: a foreign FIT on the SD card is skipped when the eMMC has
     the firmware (otherwise an Armbian card would take every boot). With no
     firmware on the eMMC the card boots as a rescue card;
4. on the foreign path only, sets `/config/bootcmd` in the loaded U-Boot's DT
   to `setenv bootmeths script extlinux; bootflow scan -lb`
   (`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_BOOTCMD`; empty disables it). See R2.

A FIT whose hashes fail is skipped, so a damaged Armbian FIT ends in the
firmware.

## The EDK2 side

`Silicon/Rockchip/Applications/ArmbianOnce` is built into boards that set
`RK_ARMBIAN_ONCE_ENABLE` (TaishanPi 3M). On every boot, after disks connect,
`PlatformBootManagerLib` connects the SD controller and calls
`ArmbianOnceProbe()` (`ArmbianOnceLib`). The option is listed when the SPL
takes requests and an arm64 U-Boot FIT (`os = "u-boot"`, `arch = "arm64"`,
no image for another architecture) is at sector 0x4000 of the SD card, the
eMMC (firmware on SD), a USB or NVMe disk, or already in the eMMC slot.
Otherwise the Boot#### variable is deleted. Without a card the probe reads a
sector or two of the eMMC only.

The file-system search for `u-boot-rockchip.bin` is not part of the probe
(too slow for every boot), so Armbian living only in an eMMC partition gets
the option once the slot has been filled.

The option is not active (boot order never picks it), has no hotkey, and is
kept last in BootOrder. Variables are written only on change. It can also be
reached as BootNext (`efibootmgr -n` from Linux booted through this firmware,
with "Save OS Variable Changes to eMMC" on).

The application:

1. refuses unless the SPL at sector 64 (eMMC or SD) contains the tag
   `ArmbianOnce/1`, so an older SPL never sees a stale request;
2. picks the target: a foreign FIT at SD sector 0x4000 (1); a foreign FIT at
   eMMC sector 0x4000 with the firmware on SD (3); otherwise copies Armbian's
   FIT into the eMMC slot (2) from sector 0x4000 of another disk (USB, NVMe)
   or from the newest `/usr/lib/linux-u-boot-*/u-boot-rockchip.bin` on any
   readable file system (Ext4Dxe is included), at offset `(0x4000 - 64) * 512`.
   The FIT length comes from its images' `data-offset`/`data-position` and
   `data-size`. It writes only if the slot differs and reads every chunk
   back;
3. writes the record, reads it back and restarts.

Every failure stops before the record is written and says why.

## SPI NOR boards (Radxa ROCK 4D)

The ROCK 4D's SPL and firmware FIT are in SPI NOR (idblock 0x8000, FIT
0x60000), boot order `"same-as-spl"` then SD. The SPL cannot cheaply write
SPI NOR, so the request lives on the SD card at sector 0x3FFF. Two switches,
both off by default:

- SPL `CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_SD_REQUEST` (`uboot/v2026.10/0002-...patch`):
  reads and erases the record on the SD card and accepts only target 1. On a
  requested boot `spl_spi_get_uboot_offs()` returns 0 (no FIT there; with
  `SPL_RAW_IMAGE_SUPPORT` off the SPL moves on to the card). Otherwise the
  offset is `CONFIG_SYS_SPI_U_BOOT_OFFS` (0x60000). No U-Boot FIT on the card
  after the erase: the SPL boots the firmware.
- EDK2 `PcdArmbianOnceRequestOnSd` (RockchipPkg.dec): looks for the SPL tag
  in SPI NOR (0x8000..0x5FFFF), for a FIT at SD sector 0x4000 only, and
  writes the request to SD sector 0x3FFF. No slot and no copying: sector
  0x10000 of an Armbian card is inside its root partition.

`boards/rock4d.conf` has `build-spl.sh` build U-Boot v2026.10's own
`rock-4d-rk3576_defconfig` and DT with both patches,
`uboot/v2026.10/rock4d/spl.config` (XMC flash for v1.12 boards,
`SPL_MMC_WRITE`, the two switches) and DDR blob v1.09. Because it replaces the
SPL every ROCK 4D image ships, it stays on branch `rock4d-parity` until a
board has booted it.

## What can be booted this way

Any FIT that mainline `spl_load_simple_fit()` loads: TF-A as `firmware`,
U-Boot and optionally OP-TEE (`os = "tee"`, passed as BL32) as loadables, a
U-Boot DT, SHA-1/SHA-256/CRC-32 hashes, external or embedded data. The FIT's
own BL31 runs.

| U-Boot inside | Works? |
|---|---|
| Mainline (Armbian edge and vendor for the TaishanPi 3M, edge for the CM5-IO) | yes: same tag, defconfig, DTS and DDR blob, so it gets the hand-off it expects (DRAM size in PMU1 GRF OS_REG2..5, DDR_MEM ATAG, `u-boot,spl-boot-device`) |
| built for another DDR blob | should work (same formats), not tested |
| Rockchip vendor U-Boot 2017.09 next-dev and forks | not supported (expects vendor SPL ATAGs and layout) |
| Radxa `next-dev-v2024.10` (CM5-IO vendor branch) | mainline-based, untested |
| needs its own TPL/SPL state (Falcon, VBE, secure-boot keys) | not supported |

## Supported cases

| Armbian is on | SPL loads Armbian's U-Boot from | Status |
|---|---|---|
| SD card, firmware on eMMC | SD sector 0x4000, in place | main case, TaishanPi 3M, board-tested |
| eMMC, firmware on SD (SPL on eMMC) | eMMC sector 0x4000, in place | supported |
| USB disk | eMMC slot, copied at each use | supported if Armbian's U-Boot finds `boot.scr` on USB; use USB-A (no FUSB302 driver in U-Boot) |
| NVMe | eMMC slot, as USB | same code path; untested (no M.2 on the TaishanPi 3M) |
| a partition of the firmware's eMMC | eMMC slot, copied from `u-boot-rockchip.bin` at each use | supported; Armbian must never write its bootloader there (R5) |
| same disk as the firmware FIT at 0x4000 | impossible | |
| ROCK 4D: SD card, firmware on SPI NOR | SD sector 0x4000 | branch `rock4d-parity`, not run; no USB/NVMe (no eMMC slot) |
| CM5-IO | | not built in: its SPL is not rebuilt by `build-spl.sh` yet and needs SD-first order |

Armbian writes its bootloader to a disk only with `FORCE_UBOOT_UPDATE=yes` or
`armbian-install`, so the FIT at 0x4000 of an Armbian card is whatever was
last put there, and ArmbianOnce uses it as is.

## Risks

- **R1**: an Armbian SD card no longer boots just by being in the slot when
  the eMMC has the firmware. With no firmware on the eMMC it still rescues.
- **R2**: Armbian's boot command is overridden on this path. Without it,
  Armbian's U-Boot started the eMMC's Windows Boot Manager through its EFI
  boot manager and hung. An Armbian image that boots through EFI from U-Boot
  does not boot this way. Empty the Kconfig string to keep Armbian's `bootcmd`.
- **R3**: the SPL writes one eMMC sector when a request is there; a power cut
  leaves a bad CRC, which means no request.
- **R4**: the slot is checked against the partition table only; 32 to 48 MiB
  is unused in every layout this project ships.
- **R5**: `armbian-install`, `armbian-config`'s bootloader update or
  `FORCE_UBOOT_UPDATE=yes` on the same eMMC overwrites the firmware at sector
  64.
- **R6**: a hang inside Armbian's U-Boot needs a power cycle; the next boot is
  the firmware.
- **R7**: the SPL brings up the eMMC on every boot, also with the firmware on
  SD (a few ms).
- **R8**: BootNext set from Windows does not survive a reset on the TaishanPi
  3M (see [hardware](dev/hardware.md#lckfb-taishanpi-3m)), so "restart into
  Armbian" works from the boot manager only.

## Building

```bash
scripts/build.sh tspi3m
scripts/build-spl.sh tspi3m     # clones U-Boot v2026.10 into third_party/u-boot once
scripts/package.sh tspi3m       # picks up out/TaishanPi3M/idbloader.img
```

`UBOOT_DIR` and `RKBIN_DIR` point at an existing U-Boot clone and DDR blob
directory. Without `build-spl.sh` the image uses `binaries/idblock_tspi3m.bin`
and the boot option says the SPL cannot do it.

To bump U-Boot: copy `uboot/v2026.10` to the new version, refresh the board
files from Armbian's `patch/u-boot/<version>`, rebase the patch (it touches
only `arch/arm/mach-rockchip/{Kconfig,Makefile}` and adds one file), and
update `SPL_UBOOT_TAG`, `SPL_UBOOT_COMMIT`, `SPL_PATCH_DIR`, `SPL_BOARD_DIR`
in `boards/tspi3m.conf`.

## Board test (TaishanPi 3M)

Needs an Armbian SD card (mainline U-Boot image), serial console at 1500000
and MaskROM access. The update writes only the SPL and the firmware, so UEFI
variables stay (nvdata at sector 43648).

1. Flash:

   ```bash
   dd if=TaishanPi3M-emmc.img of=spl.bin bs=512 skip=64 count=400
   dd if=TaishanPi3M-emmc.img of=firmware.bin bs=512 skip=16384 count=27264
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 spl.bin
   rkdeveloptool wl 16384 firmware.bin
   rkdeveloptool rd
   ```

2. No card, boot: firmware and Windows as before; no `ArmbianOnce/1` line.
3. Card in, boot: `ArmbianOnce/1: not booting the U-Boot FIT on the SD card,
   the firmware is on the eMMC`, then the firmware. The entry is listed.
4. Choose the entry: `Request written. Restarting`, then SPL `request found
   and erased: booting U-Boot from the SD card, once`, Armbian's U-Boot,
   `boot.scr`, Armbian. In Armbian `/sys/firmware/efi` is absent and
   `dd if=/dev/mmcblk0 bs=512 skip=16383 count=1 status=none | tr -d '\0' | wc -c`
   prints 0 (use the eMMC's device name).
5. `sudo reboot`, `echo b | sudo tee /proc/sysrq-trigger`, and a power cut in
   Armbian's U-Boot each come back to the firmware. Repeat step 4 three times.
6. Card out: the entry is gone from the boot manager and BootOrder; card in:
   back, last.
7. Pull the card while the screen says `Restarting`: SPL `U-Boot was not
   loaded from the SD card, booting the firmware`; sector 16383 is zero.
8. Optional, slot: card out, Armbian USB disk on USB-A, choose the entry:
   `Copying it to the eMMC slot`, SPL `booting U-Boot from the eMMC slot,
   once`. A second use prints `The eMMC slot already holds that U-Boot.`

The SPL lines in the serial log of steps 3 to 5 are the evidence.
