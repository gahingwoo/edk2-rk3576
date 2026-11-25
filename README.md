# edk2-rk3576

English | [简体中文](README.zh-CN.md)

[![SoC](https://img.shields.io/badge/SoC-RK3576-blue)]()
[![License](https://img.shields.io/badge/license-BSD--2--Clause--Patent-lightgrey)](LICENSE)
[![Flash](https://img.shields.io/badge/flash-WebUSB%20browser%20tool-informational)](https://flash.gahingwoo.com/)
[![Docs](https://img.shields.io/badge/docs-edk2.gahingwoo.com-informational)](https://edk2.gahingwoo.com/)

UEFI firmware (EDK2 / TianoCore) for Rockchip RK3576 boards. It boots Linux
the way a PC does, and Windows 11 23H2 on the CM5-IO and the TaishanPi 3M.

```
BootROM -> U-Boot SPL -> TF-A BL31 -> EDK2 (BL33) -> OS
```

| Windows 11 on the TaishanPi 3M | Fedora on the CM5-IO |
|---|---|
| ![Windows 11 23H2 on the TaishanPi 3M](docs/imgs/tspi3m-win11-desktop.jpg) | ![Fedora GNOME on the CM5-IO](docs/imgs/cm5io-desktop.png) |

## Boards

| Board | Firmware on | Status | Last run on the board |
|---|---|---|---|
| [Radxa ROCK 4D](docs/boards/rock4d.md) | SPI NOR | Linux (Fedora from NVMe). No Windows: built without ACPI. | after 0.2.1 |
| [ArmSoM CM5-IO](docs/boards/cm5io.md) | eMMC or SD card | Linux (Fedora from NVMe), Windows 11 23H2 desktop from the eMMC. | 0.4.x |
| [LCKFB TaishanPi 3M](docs/boards/tspi3m.md) | eMMC | Windows 11 23H2 with GPU, DSI touch screen, audio, Bluetooth, NPU; Armbian edge [with limits](docs/linux.md#lckfb-taishanpi-3m). | `main`, 2026-11-24 |

Each result comes from one unit of each board ([testing](docs/dev/testing.md)).
Windows touch, Bluetooth, audio and USB-C on the TaishanPi 3M need the
[`ci` build](docs/flash.md#the-rolling-ci-build); release 0.4.1 does not
have them. Serial console: 1500000 8N1.

## Get started

1. Download an image: the
   [latest release](https://github.com/gahingwoo/edk2-rk3576/releases/latest)
   (0.4.1), or the [`ci` build](docs/flash.md#the-rolling-ci-build) of `main`.
2. Write it ([flashing](docs/flash.md)) with
   [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio),
   [flash.gahingwoo.com](https://flash.gahingwoo.com/) (Chrome or Edge) or
   `rkdeveloptool`.
3. Install an OS: [Windows](docs/windows.md) (23H2 only; RK3576 is ARMv8.0)
   or [Linux](docs/linux.md).
4. Problems: [troubleshooting](docs/troubleshooting.md),
   [known issues](docs/known-issues.md).

Firmware features (Chinese menus, screenshots, debug report, Reset All
Settings, NTP clock, on-screen keyboard): [firmware menus](docs/firmware-menus.md).
Pictures: [gallery](docs/gallery.md).

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
TaishanPi 3M, its MIPI DSI screen, the fan and the EC20 4G module, and to
[ArmSoM](https://www.armsom.org/) for sponsoring the CM5 module and the
CM5-IO board.

## Licence

BSD-2-Clause-Patent, like TianoCore and edk2-rockchip. Code taken from Linux,
U-Boot and Rockchip keeps its own licence; [LICENSE](LICENSE) lists those files.
