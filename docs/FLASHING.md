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

## CM5-IO eMMC partitions (rescue system + data)

Beyond the firmware, the CM5-IO eMMC carries a partition table.  The layout
lives in `boards/cm5io-emmc.sfdisk`:

| Partition | Start | Size | Contents |
|---|---|---|---|
| (none) | 0 | 32 KiB | protective MBR + primary GPT |
| p1 `firmware` | 64 MiB region from sector 64 | 64 MiB | idblock, FIT, NV variable store -- **never put a filesystem here** |
| p2 `rescue-esp` | 64 MiB | 512 MiB | ESP holding the rescue UKI |
| p3 `rescue-root` | 576 MiB | 8 GiB | minimal Fedora rescue system |
| p4 `data` | 8768 MiB | ~20.5 GiB | general storage |

**Flashing the firmware wipes the partition table.**  `rkdeveloptool wl 0` writes
the whole image starting at LBA 0, and the image's first 32 KiB are zeros, so the
protective MBR and primary GPT go with it.  Linux will not look for a GPT without
a valid protective MBR, so the partitions disappear from the OS.  No data is lost
-- everything lives past 64 MiB and the image is only 33 MB.  Put the table back:

```bash
sudo sfdisk --wipe always /dev/mmcblk0 < boards/cm5io-emmc.sfdisk
```

### Booting the rescue system

The rescue UKI is at `\EFI\rescue\rescue.efi` on p2, deliberately **not** at the
`\EFI\BOOT\BOOTAA64.EFI` fallback path.  UEFI enumerates the eMMC before the
NVMe, so a bootloader at the fallback path would make the rescue system the
default, and the boot order cannot be pinned: `PlatformBootManagerLib` calls
`EfiBootManagerRefreshAllBootOption()` on every boot, which regenerates
`BootOrder` and discards any manual ordering made in the setup menu.

To boot it, enter the UEFI menu (the setup UI is on the serial console as well as
HDMI) and use **Boot Maintenance Manager -> Boot From File**, then pick the eMMC
volume and `EFI/rescue/rescue.efi`.

It runs the same kernel as the main install, has sshd and NetworkManager
enabled, and mounts p4 at `/data`.  Set its `root` and user passwords yourself
when you build it -- do not record them here.
