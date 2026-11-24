# Flashing

How to put the firmware on a board, update it later, and get a board back
when something went wrong. [简体中文](zh-CN/flash.md)

## Which image

Releases are on the
[Releases page](https://github.com/gahingwoo/edk2-rk3576/releases/latest).
`scripts/package.sh` writes the same files under `out/`.

| Release file | Board | Goes on |
|---|---|---|
| `ROCK4D-spi-edk2-<ver>.img` | Radxa ROCK 4D | 16 MB SPI NOR, from sector 0 |
| `CM5IO-emmc-edk2-<ver>.img` | ArmSoM CM5-IO | eMMC, with a partition table |
| `CM5IO-sdcard-edk2-<ver>.img` | ArmSoM CM5-IO | SD card |
| `TaishanPi3M-emmc-edk2-<ver>.img` | LCKFB TaishanPi 3M | eMMC, with a partition table. Needed for Windows. |
| `TaishanPi3M-sdcard-edk2-<ver>.img` | LCKFB TaishanPi 3M | SD card, with the SPL on the eMMC |
| `TaishanPi3M-spl-<ver>.bin` | LCKFB TaishanPi 3M | the SPL alone, eMMC sector 64 |
| `<Board>-emmc.sfdisk` | CM5-IO, TaishanPi 3M | the eMMC partition table on its own |

Offsets inside each image: [image layout](dev/image-layout.md).

### The rolling ci build

The pre-release tagged
[`ci`](https://github.com/gahingwoo/edk2-rk3576/releases/tag/ci) holds the
images of the latest successful build of `main`: the same files, named
`<Board>-emmc-edk2-ci-<sha>.img` (and `-sdcard-`, `-spi-`), with the
`.sfdisk` tables and `SHA256SUMS.txt`. Every successful build of `main`
replaces it. It is never marked latest, and nothing in it has been run on a
board as a whole.

Use a release. Take the `ci` build only for a change that is on `main` and
not released yet, and keep the release image at hand to go back. On
2026-11-24 that includes, for the TaishanPi 3M under Windows: touch,
Bluetooth, the headphone jack, HDMI audio and the USB-C port. Release 0.4.1
has none of them.

## Three ways to write it

All three talk to the board in MaskROM mode over USB.

1. [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio), a desktop
   app for Windows, macOS and Linux. It writes the firmware and can also
   install Windows without a USB stick. Not yet run on a board
   (2026-11-24).
2. [flash.gahingwoo.com](https://flash.gahingwoo.com/), in Chrome or Edge.
   Nothing to install.
3. `rkdeveloptool` on the command line, described below.

On Windows, MaskROM devices need the WinUSB driver: install it for the
Rockchip device with [Zadig](https://zadig.akeo.ie/) first. Linux and
macOS need no driver.

### From the browser

1. Put the board in MaskROM mode (see your board below) and connect it.
2. Open [flash.gahingwoo.com](https://flash.gahingwoo.com/), pick the board
   and the firmware choice.
3. Click **Connect and write** and pick the Rockchip device in the browser
   prompt.
4. After the loader is sent, the board comes back as a new USB device:
   click **Choose the board** and pick it again.
5. Wait for the write to finish. The board restarts.

### rkdeveloptool

`db` needs a Rockchip loader, a file starting with `LDR `, such as rkbin's
`rk3576_spl_loader_*.bin`. It is not in this repository.
`binaries/rk3576_ddr.bin` is a raw DDR blob, and `db` rejects it with
`Opening loader failed`.

After `db`, wait a few seconds before the next command: the device
re-enumerates, and the first `wl` right after `db` can fail with
`Write LBA failed`. Check that `wl` reached 100% before `rd`: a failed write
still reboots into whatever was there before. Some builds of rkdeveloptool
spell the commands `boot`, `write` and `reset`.

## Writing the whole image

This is for a new board, or an eMMC you want to start again. **It replaces
the partition table and everything installed on the eMMC**, and the saved
UEFI settings.

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 <Board>-emmc-edk2-<ver>.img
rkdeveloptool rd
```

The eMMC images carry a GPT: the firmware in p1 (sector 64, 64 MiB) and the
rest of the eMMC as p2, unformatted. The image stops at 32 MiB, so the backup
GPT is missing until you run `sudo sgdisk -e /dev/mmcblk0` once from Linux
(the Windows install script does not need it).

In the web flasher this is "UEFI firmware, whole image".

## Updating the firmware and keeping everything else

For an eMMC that was set up with the whole image before and now holds
Windows, Linux or anything else. It writes sectors 64 to 43647 of the eMMC
image (the SPL and the firmware) back to sector 64, and stops before
sector 43648, where the saved UEFI settings are. The partition table, the
other partitions and the settings stay. The same range is used on the
CM5-IO and the TaishanPi 3M.

In the web flasher and in Maskrom Studio this is "UEFI firmware, keep the
partitions". With rkdeveloptool:

```bash
dd if=<Board>-emmc-edk2-<ver>.img of=firmware.bin bs=512 skip=64 count=43584
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 64 firmware.bin
rkdeveloptool rd
```

From Linux running on the board itself, the same thing is:

```bash
sudo dd if=<Board>-emmc-edk2-<ver>.img of=/dev/mmcblk0 bs=512 skip=64 seek=64 count=43584 conv=fsync
```

Check that `/dev/mmcblk0` is the eMMC (`lsblk`) before running it.

Do not write the whole `p1` partition, and do not use `tail -c +32769` on the
image: both write the image's empty settings store over sector 43648 and
reset every UEFI setting and boot option.

If the partition table was lost anyway,
`sudo sfdisk --wipe always /dev/mmcblk0 < <Board>-emmc.sfdisk` writes the
image's table back; the data behind it survives.

## Radxa ROCK 4D

The firmware lives on the 16 MB SPI NOR.

The ROCK 4D's USB-C port only supplies power (its data pins are not
connected). For MaskROM:

1. Connect a USB-A to USB-A cable from the board's upper USB 3.0 USB-A port
   to the computer.
2. Hold the MaskROM button while plugging in the power supply.
3. Write the image:

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 0 ROCK4D-spi-edk2-<ver>.img
   rkdeveloptool rd
   ```

From a Linux booted off an SD card, `flashcp` to `/dev/mtd0` also works.
UEFI settings are kept in the SPI NOR at `0xFC0000`; writing the whole image
resets them.

## ArmSoM CM5-IO

The firmware lives on the eMMC or on an SD card. The carrier's SPI NOR is
64 KB, too small for it.

MaskROM: hold the MaskROM button while powering on. The MaskROM port is the
USB-C port, so unplug anything else from it first.

On the eMMC: "Writing the whole image" or "Updating the firmware" above,
with the `CM5IO-emmc` file.

On an SD card:

```bash
dd if=CM5IO-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress conv=fsync
```

The BootROM tries the eMMC before the SD card, so a card is ignored while the
eMMC holds a bootloader. UEFI settings are stored on the card, inside the
firmware image at sector 43648.

## LCKFB TaishanPi 3M

The BootROM on this board reads the eMMC and nothing else, so an SD card on
its own does not boot. At least the SPL has to be on the eMMC.

MaskROM: the board has no MaskROM button.

1. Power off.
2. Short the two pads marked `MSK`, directly under the `RK3576` silkscreen
   (a photo is attached to the 0.4.1 release as `TaishanPi3M-msk-pads.png`).
3. Connect the USB-C port marked `OTG+DP` to the computer and power on.
   A device `2207:350e` appears.

A board that already runs this firmware can also be sent to MaskROM with F4
during the boot countdown.

Everything on the eMMC, which is what Windows needs: "Writing the whole
image" or "Updating the firmware" above, with the `TaishanPi3M-emmc` file.
Writing the whole image also replaces LCKFB's stock system; it will not start
again until LCKFB's own firmware is written back.

Or the SPL on the eMMC and the firmware on an SD card. With the card in, the
SPL prints `Trying to boot from MMC2` and loads the firmware from it. With
the card out, the board boots whatever is on the eMMC.

1. Write the SPL to eMMC sector 64. From a running Linux on the board (this
   replaces only the SPL, about 200 KB; an OS whose own U-Boot FIT is at
   sector 16384 still boots when no card is in the slot):

   ```bash
   sudo dd if=TaishanPi3M-spl-<ver>.bin of=/dev/mmcblk0 bs=512 seek=64 conv=fsync
   ```

   Or from MaskROM:

   ```bash
   rkdeveloptool db rk3576_spl_loader.bin
   rkdeveloptool wl 64 TaishanPi3M-spl-<ver>.bin
   rkdeveloptool rd
   ```

2. Write the firmware to the card on the computer:

   ```bash
   dd if=TaishanPi3M-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress conv=fsync
   ```

Check that MaskROM can be reached before replacing the SPL. If the SPL write
goes wrong, MaskROM is the way back.

To build an image yourself: `scripts/build.sh tspi3m` and
`scripts/package.sh tspi3m` ([building](dev/building.md)).

## Recovery

MaskROM is in the BootROM, which nothing here writes, so a board can always
be put back into MaskROM and written again.

If the board starts but the settings are wrong, "Reset All Settings" on the
front page, or F9 during the boot countdown, erases the UEFI settings and
starts as a freshly written board. See [firmware menus](firmware-menus.md).
