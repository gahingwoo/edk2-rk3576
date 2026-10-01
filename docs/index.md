# edk2-rk3576

UEFI firmware (EDK2 / TianoCore) for Rockchip RK3576 boards: Radxa ROCK 4D
and ArmSoM CM5-IO.

```
BootROM → U-Boot SPL → TF-A BL31 → EDK2 (BL33) → OS
```

Both boards boot Fedora from NVMe. The CM5-IO also boots Windows 11 23H2 from
its eMMC.

## Get it

- Images: [latest release](https://github.com/gahingwoo/edk2-rk3576/releases/latest)
- Flash from the browser: [flash.gahingwoo.com](https://flash.gahingwoo.com/)
- Source, board status and known issues: [github.com/gahingwoo/edk2-rk3576](https://github.com/gahingwoo/edk2-rk3576#boards)

## Screenshots

| Radxa ROCK 4D | ArmSoM CM5-IO |
|---|---|
| ![UEFI front page on ROCK 4D](imgs/monitor-4d.png) | ![UEFI front page on CM5-IO](imgs/monitor-cm5io.jpeg) |
| TianoCore front page, 2560×1440@60 over HDMI | TianoCore front page, 2560×1440@60 over HDMI |
| ![GNOME on ROCK 4D](imgs/rock4d-desktop.png) | ![GNOME on CM5-IO](imgs/cm5io-desktop.png) |
| GNOME *About*: Fedora 44, 11.5 GiB RAM | GNOME *About*: Fedora 45 Workstation, kernel 7.2, 3.7 GiB RAM, Mali-G52 via Panfrost |

| | |
|---|---|
| ![GRUB on USB](imgs/grub.png) | ![Fedora live console](imgs/fedora.png) |
| GRUB from a Fedora USB stick | Fedora live console |

![Windows 11 on CM5-IO](imgs/cm5io-win11-desktop.jpg)

Windows 11 23H2 on CM5-IO, booted from the eMMC: eight cores, 3.7 GB, the eMMC
as C:, the NVMe, the SD card, Ethernet and both USB controllers. The drivers
are in [woa-rk3576](https://github.com/gahingwoo/woa-rk3576).

## Docs

- [Flashing](FLASHING.md): the browser tool, `rkdeveloptool`, SD card and eMMC
- [Building](BUILDING.md): host setup, dependencies, the three build scripts
- [Image layout](SPI_LAYOUT.md): byte offsets in each image, and the FIT
- [Hardware](HARDWARE.md): the board facts the firmware depends on
