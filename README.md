# edk2-rk3576

English | [简体中文](README.zh-CN.md)

[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![License](https://img.shields.io/badge/license-BSD--2--Clause--Patent-lightgrey)](LICENSE)
[![Flash](https://img.shields.io/badge/flash-WebUSB%20browser%20tool-informational)](https://flash.gahingwoo.com/)
[![Docs](https://img.shields.io/badge/docs-edk2.gahingwoo.com-informational)](https://edk2.gahingwoo.com/)

UEFI firmware (EDK2 / TianoCore) for Rockchip RK3576 boards: the Radxa
ROCK 4D, the ArmSoM CM5-IO and the LCKFB TaishanPi 3M. It boots Linux like
a PC does, and Windows 11 23H2 on the CM5-IO and the TaishanPi 3M.

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> OS
```

This repository is the starting point for all the RK3576 projects listed
below.

| Windows 11 on the TaishanPi 3M | Fedora on the CM5-IO |
|---|---|
| ![Windows 11 23H2 on the TaishanPi 3M](docs/imgs/tspi3m-win11-desktop.jpg) | ![Fedora GNOME on the CM5-IO](docs/imgs/cm5io-desktop.png) |

More pictures: [gallery](docs/gallery.md).

## From a new board to Windows or Linux

1. Read your board's page: [Radxa ROCK 4D](docs/boards/rock4d.md),
   [ArmSoM CM5-IO](docs/boards/cm5io.md),
   [LCKFB TaishanPi 3M](docs/boards/tspi3m.md).
2. Write the firmware ([flashing](docs/flash.md)) with one of:
   - [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio), a
     desktop app;
   - [flash.gahingwoo.com](https://flash.gahingwoo.com/), in Chrome or Edge;
   - `rkdeveloptool`.
3. For Windows 11 23H2 (CM5-IO, TaishanPi 3M), follow
   [woa-rk3576's install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md),
   or let Maskrom Studio install it from the PC without a USB stick.
   Overview: [Windows](docs/windows.md).
4. Add the drivers you want under Windows: the GPU and display driver
   [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52), the NPU
   driver [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu).
5. For Linux, see [Linux](docs/linux.md).
6. If something goes wrong: [troubleshooting](docs/troubleshooting.md),
   [known issues](docs/known-issues.md).

Images: [latest release](https://github.com/gahingwoo/edk2-rk3576/releases/latest)
(0.4.1, 2026-11-17), or the [`ci` build](docs/flash.md#the-rolling-ci-build)
of `main`, which has everything merged since.

## Boards

Status on 2026-11-24. "ci" means: works with the `ci` build of `main`, not
in release 0.4.1.

| | Radxa ROCK 4D | ArmSoM CM5-IO | LCKFB TaishanPi 3M |
|---|---|---|---|
| Firmware lives on | SPI NOR | eMMC or SD card | eMMC (or SPL on eMMC, rest on SD) |
| HDMI | works | works | works |
| DSI screen and touch | not tried | not tried | works (LCKFB 800x1280) |
| eMMC | no eMMC (UFS, not tried) | works | works |
| NVMe | works | works | no M.2 slot |
| USB-A | works | works | works |
| USB-C | USB 2.0 | USB 2.0 | works for a device attached at power-on (ci) |
| Ethernet | works | works | works |
| Linux through UEFI | Fedora from NVMe | Fedora from NVMe | Armbian edge from the eMMC, [with limits](docs/linux.md#lckfb-taishanpi-3m) |
| Windows 11 23H2 | not available (no ACPI) | boots to the desktop | boots to the desktop |
| Windows: GPU desktop | | not tried | works with wddm-mali-g52 |
| Windows: audio | | not tried | headphones, microphone, HDMI (ci) |
| Windows: Bluetooth, WiFi | | not working | Bluetooth works (ci), WiFi not working |
| Windows: NPU | | not available | works with woa-rk3576-npu (ci) |

Each board's results come from one unit. When each board was last run, and
on what: [testing](docs/dev/testing.md). Serial console: 1500000 8N1.

The firmware itself: Simplified Chinese menus, screenshots, a debug report,
Reset All Settings, network time sync, an on-screen keyboard. See
[firmware menus](docs/firmware-menus.md).

## Windows

Windows 11 23H2 runs from the eMMC of the CM5-IO and of the TaishanPi 3M.
Windows 11 24H2 and later cannot run: RK3576 is ARMv8.0. Windows Setup does
not work on these boards, so the image is applied with DISM from a WinPE
stick, or by Maskrom Studio. How:
[woa-rk3576's install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md).
Which firmware each feature needs: [Windows](docs/windows.md).

### Linux on the TaishanPi 3M

Armbian edge runs from the eMMC next to Windows, through GRUB and the
firmware's device tree: the DSI screen, touch, headphones, backlight, USB-A,
Ethernet and the EC20 modem work. The GPU is disabled there for now. Details
and limits: [Linux](docs/linux.md#lckfb-taishanpi-3m).

## Known issues

The main ones, all on [known issues](docs/known-issues.md):

- Windows 11 24H2 and later cannot run (ARMv8.0).
- The TaishanPi 3M's USB-C port gets power only for a device attached at
  power-on.
- USB-C runs at USB 2.0 outside Linux on the ROCK 4D and the CM5-IO.
- The TaishanPi 3M's RTC loses its time at every power-off; the clock comes
  from NTP or the OS.

## Related projects

| Repository | What |
|---|---|
| [woa-rk3576](https://github.com/gahingwoo/woa-rk3576) | Windows drivers and the WinPE install image |
| [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) | Windows GPU and display driver for the Mali-G52 |
| [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | Windows NPU driver and the charsiu LLM runtime |
| [maskrom-studio](https://github.com/gahingwoo/maskrom-studio) | Desktop app: firmware, Windows without a USB stick, back to Armbian or the factory image |
| [edk2-webflash](https://github.com/gahingwoo/edk2-webflash) | The browser flasher at flash.gahingwoo.com |

## Build

```bash
scripts/setup-host.sh
scripts/build.sh   cm5io      # or rock4d, tspi3m
scripts/package.sh cm5io      # -> out/CM5IO/
```

[Building](docs/dev/building.md) · [Architecture](docs/dev/architecture.md) ·
[Image layout](docs/dev/image-layout.md) · [Hardware](docs/dev/hardware.md) ·
[ACPI](docs/dev/acpi.md) · [Testing](docs/dev/testing.md) ·
[Releasing](docs/dev/releasing.md) · [Core patches](patches/README.md) ·
[History](docs/history/README.md)

## Acknowledgements

Thanks to [JLC](https://jlc.com/) (嘉立创) for sponsoring the LCKFB
TaishanPi 3M, its MIPI DSI screen, the fan and the EC20 4G module used for
this work, and to [ArmSoM](https://www.armsom.org/) for sponsoring the CM5
module and the CM5-IO board.

## Licence

BSD-2-Clause-Patent, like TianoCore and edk2-rockchip. Code taken from Linux,
U-Boot and Rockchip keeps its own licence; [LICENSE](LICENSE) lists those files.
