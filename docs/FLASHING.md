# Flashing

Prebuilt images are on the
[Releases page](https://github.com/gahingwoo/edk2-rk3576/releases/latest).
If you build your own, `scripts/package.sh` writes the same files under `out/`.

| Release file | Built as | Board | Goes on |
|---|---|---|---|
| `ROCK4D-spi-edk2-<ver>.img` | `out/ROCK4D/ROCK4D-spi.img` | Radxa ROCK 4D | 16 MB SPI NOR |
| `CM5IO-sdcard-edk2-<ver>.img` | `out/CM5IO/CM5IO-sdcard.img` | ArmSoM CM5-IO | SD card |
| `CM5IO-emmc-edk2-<ver>.img` | `out/CM5IO/CM5IO-emmc.img` | ArmSoM CM5-IO | eMMC, with a data partition |
| `CM5IO-emmc.sfdisk` | `out/CM5IO/CM5IO-emmc.sfdisk` | ArmSoM CM5-IO | the eMMC partition table on its own |

Byte offsets inside each image are in [SPI_LAYOUT.md](SPI_LAYOUT.md).

## From the browser

[flash.gahingwoo.com](https://flash.gahingwoo.com/) runs `rkdeveloptool` in the
browser over WebUSB. Use Chrome or Edge.

1. Download the image for your board from the Releases page.
2. Hold the MaskROM button, plug in USB-C, then release.
3. Pick the board, click **Flash Custom Image**, choose the file, and pick the
   Rockchip device from the browser prompt.
4. After the loader is sent, click **Reconnect Device** and pick the device
   again.
5. Wait for the flash to complete. The board reboots on its own.

No driver is needed on Linux or macOS. On Windows, install WinUSB for the
device with [Zadig](https://zadig.akeo.ie/) first.

Source: [github.com/gahingwoo/edk2-webflash](https://github.com/gahingwoo/edk2-webflash)

## ROCK 4D: SPI NOR with rkdeveloptool

1. Put the board in MaskROM: hold the MaskROM button while powering on.
2. Connect the USB-C OTG port to your host.
3. Run:

   ```bash
   rkdeveloptool db   rk3576_spl_loader.bin      # an RKLD loader, see below
   rkdeveloptool wl 0 ROCK4D-spi-edk2-<ver>.img
   rkdeveloptool rd
   ```

The UART shows TF-A, then the EDK2 banner, then the UEFI front page.

## CM5-IO: SD card

The CM5-IO carrier's SPI NOR is 64 KB, far too small for the firmware, so the
firmware lives on the SD card or the eMMC.

```bash
dd if=CM5IO-sdcard-edk2-<ver>.img of=/dev/sdX bs=1M status=progress
sync
```

Insert the card and power on.

**The BootROM tries the eMMC before the SD card.** If the eMMC already holds a
bootloader, the board boots that and ignores the card. Either flash the eMMC
instead, or erase the start of the eMMC first.

UEFI variables persist on the SD card: the image carries a variable store at
`0x1600000`.

## CM5-IO: eMMC with a data partition

`CM5IO-emmc-edk2-<ver>.img` is the firmware with a partition table in front,
so the rest of the eMMC stays usable. The image is 32 MiB; the data partition
is declared, not populated.

| Partition | Start | Size | Contents |
|---|---|---|---|
| p1 `firmware` | sector 64 | 64 MiB | idblock, FIT, variable store. No filesystem. |
| p2 `data` | 64 MiB | rest of the eMMC | unformatted |

```bash
rkdeveloptool db   rk3576_spl_loader.bin
rkdeveloptool wl 0 CM5IO-emmc-edk2-<ver>.img
rkdeveloptool rd
# then once, on the board:
sudo mkfs.ext4 -L data /dev/mmcblk0p2
sudo sgdisk -e /dev/mmcblk0
```

The image stops at 32 MiB, so it carries the primary GPT but not the backup
copy at the end of the device. Linux warns about the missing backup;
`sgdisk -e` writes it.

Flashing the plain SD-card image over a partitioned eMMC wipes the table,
because that image starts with 32 KB of zeros and Linux will not read a GPT
without a valid protective MBR. The data survives, and `CM5IO-emmc.sfdisk`
writes the table back:

```bash
sudo sfdisk --wipe always /dev/mmcblk0 < CM5IO-emmc.sfdisk
```

## The loader `rkdeveloptool db` wants

`db` takes a Rockchip **loader**: a container whose first four bytes are
`LDR `, such as rkbin's `rk3576_spl_loader_*.bin`. It is not in this repo.

`binaries/rk3576_ddr.bin` is the raw DDR init blob. It starts with an AArch64
instruction, and handing it to `db` fails with

    Opening loader failed, exiting download boot!

which is easy to read as a USB or permissions problem. It is neither.

After `db`, the board takes several seconds to come back on USB, and `ld`
keeps reporting `Maskrom` the whole time, so there is nothing to poll for.
And a failed `wl` does not stop `rd` from rebooting the board into whatever
was already on it, so check that `wl` reached 100% before resetting.

## Recovery

MaskROM lives in the BootROM, which nothing here writes to, so a board can
always be put back into MaskROM and reflashed.
