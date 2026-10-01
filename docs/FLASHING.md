# Flashing

Images are on the
[Releases page](https://github.com/gahingwoo/edk2-rk3576/releases/latest);
`scripts/package.sh` writes the same files under `out/`.

| Release file | Board | Goes on |
|---|---|---|
| `ROCK4D-spi-edk2-<ver>.img` | Radxa ROCK 4D | 16 MB SPI NOR |
| `CM5IO-sdcard-edk2-<ver>.img` | ArmSoM CM5-IO | SD card |
| `CM5IO-emmc-edk2-<ver>.img` | ArmSoM CM5-IO | eMMC, with a partition table |
| `CM5IO-emmc.sfdisk` | ArmSoM CM5-IO | that partition table on its own |

Offsets inside each image: [SPI_LAYOUT.md](SPI_LAYOUT.md).

## From the browser

[flash.gahingwoo.com](https://flash.gahingwoo.com/) runs `rkdeveloptool` over
WebUSB in Chrome or Edge.

1. Download the image for your board.
2. Hold the MaskROM button, plug in USB-C, release.
3. Pick the board, click **Flash Custom Image**, choose the file, and pick the
   Rockchip device in the browser prompt.
4. After the loader is sent, click **Reconnect Device** and pick it again.
5. The board reboots when the flash completes.

Linux and macOS need no driver. On Windows, install WinUSB for the device with
[Zadig](https://zadig.akeo.ie/) first.

## rkdeveloptool

`db` needs a Rockchip loader, a file starting with `LDR `, such as rkbin's
`rk3576_spl_loader_*.bin`. It is not in this repo. `binaries/rk3576_ddr.bin`
is a raw DDR blob, and `db` rejects it with `Opening loader failed`.

After `db`, give the board a few seconds before the next command. Check that
`wl` reached 100% before `rd`: a failed write still reboots into whatever was
there before.

On the CM5-IO the MaskROM port is the USB-C port, so unplug anything else from
it first.

### ROCK 4D

Hold MaskROM while powering on, connect USB-C, then:

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 ROCK4D-spi-edk2-<ver>.img
rkdeveloptool rd
```

### CM5-IO, empty eMMC

```bash
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool wl 0 CM5IO-emmc-edk2-<ver>.img
rkdeveloptool rd
```

The image carries a GPT with the firmware in p1 (sector 64, 64 MiB) and the
rest of the eMMC as p2, unformatted. It stops at 32 MiB, so the backup GPT is
missing until you run `sudo sgdisk -e /dev/mmcblk0` once.

### CM5-IO, eMMC that already holds Windows or other partitions

Writing the whole image at sector 0 replaces the partition table. Write only
the firmware partition: drop the first 32 KiB of the image and write the rest
at sector 64.

```bash
tail -c +32769 CM5IO-emmc-edk2-<ver>.img > p1.bin
rkdeveloptool db rk3576_spl_loader.bin
rkdeveloptool rl 0 17408 gpt.bin       # check p1 starts at sector 64 first
rkdeveloptool wl 64 p1.bin
rkdeveloptool rd
```

From Linux on the board, the same thing is
`dd if=CM5IO-emmc-edk2-<ver>.img of=/dev/mmcblk0p1 bs=512 skip=64 conv=fsync`.

If the table was lost anyway, `sudo sfdisk --wipe always /dev/mmcblk0 <
CM5IO-emmc.sfdisk` writes the image's table back; the data behind it survives.

## CM5-IO: SD card

The carrier's SPI NOR is 64 KB, too small for the firmware.

```bash
dd if=CM5IO-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress
```

The BootROM tries the eMMC before the SD card, so a card is ignored while the
eMMC holds a bootloader. UEFI variables are stored on the card at `0x1600000`.

## Recovery

MaskROM is in the BootROM, which nothing here writes, so a board can always be
put back into MaskROM and reflashed.
