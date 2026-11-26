# Flashing

English | [简体中文](flash.zh-CN.md)

## Which image

From the [Releases page](https://github.com/gahingwoo/edk2-rk3576/releases/latest),
or `out/` after `scripts/package.sh`. Offsets: [image layout](dev/image-layout.md).

<!-- generated:images -->

| Release file | Board | Goes on |
|---|---|---|
| `CM5IO-emmc-edk2-<ver>.img` | ArmSoM CM5-IO | eMMC, with a partition table (p1 firmware 64 MiB, p2 data) |
| `CM5IO-sdcard-edk2-<ver>.img` | ArmSoM CM5-IO | SD card (or eMMC, with no partition table) |
| `CM5IO-emmc.sfdisk` | ArmSoM CM5-IO | the eMMC partition table on its own |
| `ROCK4D-spi-edk2-<ver>.img` | Radxa ROCK 4D | 16 MB SPI NOR, from sector 0 |
| `TaishanPi3M-emmc-edk2-<ver>.img` | LCKFB TaishanPi 3M | eMMC, with a partition table (p1 firmware 64 MiB, p2 data) |
| `TaishanPi3M-sdcard-edk2-<ver>.img` | LCKFB TaishanPi 3M | SD card (or eMMC, with no partition table) |
| `TaishanPi3M-emmc.sfdisk` | LCKFB TaishanPi 3M | the eMMC partition table on its own |

<!-- /generated -->

Releases also carry `TaishanPi3M-spl-<ver>.bin`, the TaishanPi 3M SPL on its
own (eMMC sector 64).

### The rolling ci build

The pre-release [`ci`](https://github.com/gahingwoo/edk2-rk3576/releases/tag/ci)
holds the images of the latest successful build of `main`
(`<Board>-emmc-edk2-ci-<sha>.img` and so on). Each build of `main` replaces
it, and nothing in it has been run on a board as a whole. Use it only for a
change that is not released yet, and keep the release image to go back to.

## Put the board in MaskROM

- **Radxa ROCK 4D:** USB-A to USB-A cable from the upper USB 3.0 port to the
  computer (the USB-C port carries power only), then hold the MaskROM button
  while plugging in the power supply.
- **ArmSoM CM5-IO:** hold the MaskROM button while powering on. The MaskROM
  port is the USB-C port; unplug anything else from it.
- **LCKFB TaishanPi 3M:** no button. Power off, short the two `MSK` pads
  directly under the `RK3576` silkscreen (photo: `TaishanPi3M-msk-pads.png`
  in the 0.4.1 release), connect the USB-C port marked `OTG+DP` and power on.
- A board running this firmware also enters MaskROM with F4 during the boot
  countdown.

The computer then sees USB device `2207:350e`. On Windows, first install the
WinUSB driver for it with [Zadig](https://zadig.akeo.ie/); Linux and macOS
need no driver.

## Three ways to write it

1. [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio): desktop app
   for Windows, macOS and Linux; can also install Windows without a USB
   stick.
2. [flash.gahingwoo.com](https://flash.gahingwoo.com/) in Chrome or Edge:
   pick the board and the firmware choice, click **Connect and write**, pick
   the Rockchip device, and after the loader is sent pick it again under
   **Choose the board**.
3. `rkdeveloptool`:
   - `db` needs a Rockchip loader starting with `LDR `, such as rkbin's
     `rk3576_spl_loader_*.bin` (not in this repository).
     `binaries/rk3576_ddr.bin` is a raw DDR blob and fails with
     `Opening loader failed`.
   - Wait a few seconds after `db`; a `wl` right after it can fail with
     `Write LBA failed`.
   - Check that `wl` reached 100% before `rd`. A failed write still reboots
     into the old firmware.

## Writing the whole image

For a new board or a fresh eMMC. **This replaces the partition table,
everything on the eMMC and the saved UEFI settings.** In the web flasher:
"UEFI firmware, whole image".

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 <Board>-emmc-edk2-<ver>.img
rkdeveloptool rd
```

The image is 32 MiB long, so run `sudo sgdisk -e /dev/mmcblk0` once from
Linux to write the backup GPT (the Windows install script does not need it).

## Updating the firmware and keeping everything else

For an eMMC that already holds Windows, Linux or other partitions (CM5-IO,
TaishanPi 3M). This writes sectors <!-- generated:spl-sector -->64<!-- /generated -->
to <!-- generated:keep-last -->43647<!-- /generated --> (SPL and firmware)
and stops before sector <!-- generated:nvdata-sector -->43648<!-- /generated -->,
where the UEFI settings are. In the web flasher and Maskrom Studio: "UEFI
firmware, keep the partitions".

<!-- generated:keep-commands -->

```bash
dd if=<Board>-emmc-edk2-<ver>.img of=firmware.bin bs=512 skip=64 count=43584
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 64 firmware.bin
rkdeveloptool rd
```

<!-- /generated -->

From Linux on the board (check with `lsblk` that `/dev/mmcblk0` is the eMMC):

<!-- generated:keep-dd -->

```bash
sudo dd if=<Board>-emmc-edk2-<ver>.img of=/dev/mmcblk0 bs=512 skip=64 seek=64 count=43584 conv=fsync
```

<!-- /generated -->

Do not write the whole `p1` partition or use `tail -c +32769`: both
overwrite the settings and reset every UEFI setting and boot option.

If the partition table was lost,
`sudo sfdisk --wipe always /dev/mmcblk0 < <Board>-emmc.sfdisk` writes it back;
the data behind it survives.

## Radxa ROCK 4D

The firmware and the UEFI settings are on the 16 MB SPI NOR; writing the
whole image resets the settings.

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 ROCK4D-spi-edk2-<ver>.img
rkdeveloptool rd
```

Maskrom Studio can also write it keeping the settings. From Linux booted
off an SD card, `flashcp` to `/dev/mtd0` also works.

## ArmSoM CM5-IO

The firmware goes on the eMMC or an SD card (the carrier's 64 KB SPI NOR is
too small).

- eMMC: the two sections above, with the `CM5IO-emmc` file.
- SD card: `dd if=CM5IO-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress conv=fsync`.
  The settings are stored on the card. The BootROM tries the eMMC first, so
  the card is ignored while the eMMC holds a bootloader.

## LCKFB TaishanPi 3M

The BootROM reads only the eMMC, so at least the SPL must be on the eMMC.

**All on the eMMC** (needed for Windows): the two sections above with the
`TaishanPi3M-emmc` file. The whole image replaces LCKFB's stock system.

**SPL on the eMMC, firmware on an SD card**: with the card in, the SPL prints
`Trying to boot from MMC2` and loads the firmware from it; with the card out
the board boots whatever is on the eMMC. Make sure MaskROM works first, since
it is the way back from a bad SPL write.

1. SPL to eMMC sector 64, from Linux on the board
   (`sudo dd if=TaishanPi3M-spl-<ver>.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync`)
   or from MaskROM (`rkdeveloptool wl 64 TaishanPi3M-spl-<ver>.bin` after `db`).
2. Firmware to the card:
   `dd if=TaishanPi3M-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress conv=fsync`.

## Recovery

MaskROM is in the BootROM, which nothing here writes, so a board can always
be written again. If the board starts but the settings are wrong, use
"Reset All Settings" on the front page or F9 during the boot countdown
([firmware menus](firmware-menus.md#reset-all-settings)).
