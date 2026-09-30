# edk2-rk3576

EDK2 / TianoCore UEFI firmware for Rockchip RK3576 boards: Radxa ROCK 4D and
ArmSoM CM5-IO.

```
BootROM → U-Boot SPL → TF-A BL31 → EDK2 (BL33) → OS
```

Both boards boot to the UEFI front page over HDMI and run Fedora from an NVMe
disk. CM5-IO also runs Windows 10 21H2 Setup.

![UEFI front page on CM5-IO](imgs/monitor-cm5io.jpeg)

## Get it

- **Images:** [latest release](https://github.com/gahingwoo/edk2-rk3576/releases/latest)
- **Flash from the browser:** [flash.gahingwoo.com](https://flash.gahingwoo.com/)
- **Source:** [github.com/gahingwoo/edk2-rk3576](https://github.com/gahingwoo/edk2-rk3576)

What works on each board, and the open issues, are in the
[README](https://github.com/gahingwoo/edk2-rk3576#boards).

## Docs

- [Flashing](FLASHING.md): the browser tool, `rkdeveloptool`, SD card and eMMC
- [Building](BUILDING.md): host setup, dependencies, the three build scripts
- [Image layout](SPI_LAYOUT.md): byte offsets in each image, and the FIT
- [Hardware](HARDWARE.md): the board facts the firmware depends on
