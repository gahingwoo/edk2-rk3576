# Flashing the prebuilt firmware

Two delivery paths are provided in this repo:

| File                       | Use                                          |
|----------------------------|----------------------------------------------|
| `binaries/u-boot.itb`      | U-Boot only (BL31 + U-Boot proper + DTB)     |
| `rock4d-spi-edk2.img`      | Full UEFI image for 16 MB SPI NOR            |

## Option A — SPI NOR (UEFI, persistent) via MaskROM

1. Put the board in **MaskROM** mode (hold MaskROM button while powering on,
   or short the MaskROM pads on the bottom side).
2. Connect the USB-C OTG port to your host.
3. Run:

   ```bash
   rkdeveloptool db   binaries/rk3576_ddr.bin    # download DDR init blob
   rkdeveloptool wl 0 rock4d-spi-edk2.img        # write the 16 MB SPI image
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

## Recovery

If a flash leaves the board unbootable, re-enter MaskROM mode and re-flash
with Option A. The SPI NOR can always be recovered this way; nothing in the
boot ROM is touched.
