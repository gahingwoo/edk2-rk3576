# Flashing the prebuilt firmware

Two delivery paths are provided in this repo:

| File                       | Use                                          |
|----------------------------|----------------------------------------------|
| `binaries/u-boot.itb`      | U-Boot only (BL31 + U-Boot proper + DTB)     |
| `out/ROCK4D/ROCK4D-spi.img`      | Full UEFI image for 16 MB SPI NOR            |
| `out/CM5IO/CM5IO-sdcard.img`          | Full UEFI image for CM5-IO SD card (SPI is 64 KB) |

## Option 0 — Browser flash (easiest, no tools required)

Open **[gahingwoo.github.io/edk2-webflash](https://gahingwoo.github.io/edk2-webflash/)** in **Chrome or Edge** (WebUSB required).

1. Hold the MaskROM button, plug in USB-C, then release.
2. Click **Flash UEFI** and pick the Rockchip device from the browser prompt.
3. After the loader is sent, click **Reconnect Device** and pick the device again.
4. Wait for *Flash complete* — the board reboots into UEFI automatically.

No drivers needed on Linux / macOS. On Windows, install WinUSB for the device
via [Zadig](https://zadig.akeo.ie/) first.

Source: [github.com/gahingwoo/edk2-webflash](https://github.com/gahingwoo/edk2-webflash)

## Option A — SPI NOR (UEFI, persistent) via MaskROM

1. Put the board in **MaskROM** mode (hold MaskROM button while powering on,
   or short the MaskROM pads on the bottom side).
2. Connect the USB-C OTG port to your host.
3. Run:

   ```bash
   rkdeveloptool db   binaries/rk3576_ddr.bin    # download DDR init blob
   rkdeveloptool wl 0 out/ROCK4D/ROCK4D-spi.img        # write the 16 MB SPI image
   rkdeveloptool rd                              # reboot
   ```

4. On UART you should see TF-A → U-Boot → EDK2 banner, then the UEFI Shell
   prompt (or the boot menu).

## Option B — SD card / eMMC (U-Boot only, no UEFI)

Use Radxa's upstream packaging recipe with `binaries/u-boot.itb` plus the
`idblock` from `binaries/u-boot-spl.bin`. The standard layout is:

| Offset (sectors @ 512 B) | Content                  |
|--------------------------|--------------------------|
| 64                       | idblock (SPL + DDR init) |
| 16384                    | u-boot.itb               |

## Option C — SD card (ArmSoM CM5-IO — UEFI, persistent)

The CM5-IO carrier board has only a **64 KB SPI NOR flash**, which cannot hold
the UEFI firmware stack (~5 MB). Use an SD card (or eMMC) instead.

SD card image layout:

| Sector offset (× 512 B) | Byte offset | Content                              |
|--------------------------|-------------|--------------------------------------|
| 64 (0x40)                | 0x008000    | idblock (DDR init + mainline SPL)    |
| 16384 (0x4000)           | 0x800000    | FIT image (BL31 + EDK2 BL33 + DTB)  |

1. Insert an SD card (≥ 64 MB) into your host.
2. Write the image (replace `/dev/sdX` with your SD card device):

   ```bash
   dd if=out/CM5IO/CM5IO-sdcard.img of=/dev/sdX bs=1M status=progress
   sync
   ```

3. Insert the SD card into the CM5-IO SD slot and power on.
4. On UART (1500000 8N1) you should see:
   `U-Boot SPL → INFO: BL31 → TianoCore EDK2 / UEFI Interactive Shell`

> **Note:** EFI variables are **volatile** on SD card boot. The carrier SPI
> (64 KB) is too small for a variable store, so variable changes do not persist
> across reboots.

## Recovery

If a flash leaves the board unbootable, re-enter MaskROM mode and re-flash
with Option A. The SPI NOR can always be recovered this way; nothing in the
boot ROM is touched.

## Option D — CM5-IO eMMC (firmware + data partition)

`out/CM5IO/CM5IO-emmc.img` is the firmware image with a partition table in
front, so flashing it leaves the whole eMMC usable instead of only the 33 MB the
firmware occupies. Still 32 MB; the data partition is declared, not populated.

| Partition | Start | Size | Contents |
|---|---|---|---|
| p1 `firmware` | sector 64 | 64 MiB | idblock, FIT, NV variable store. No filesystem. |
| p2 `data` | 64 MiB | rest of the eMMC | unformatted |

```bash
rkdeveloptool db   binaries/rk3576_ddr.bin
rkdeveloptool wl 0 out/CM5IO/CM5IO-emmc.img
rkdeveloptool rd
# then once, on the board:
sudo mkfs.ext4 -L data /dev/mmcblk0p2
sudo sgdisk -e /dev/mmcblk0
```

The image stops at 32 MB, so it carries the primary GPT but not the backup copy
at the end of the device. Linux reads the table and warns about the missing
alternate; `sgdisk -e` writes it and silences that.

`CM5IO-sdcard.img` is still produced and is still just the firmware. Flashing it
over a partitioned eMMC wipes the table, because it starts with 32 KB of zeros
and Linux will not read a GPT without a valid protective MBR. The data survives;
`CM5IO-emmc.sfdisk` next to the image writes the table back.
