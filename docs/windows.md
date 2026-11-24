# Windows

[简体中文](zh-CN/windows.md)

Windows 11 23H2 runs from the eMMC of the ArmSoM CM5-IO and of the LCKFB
TaishanPi 3M. The Radxa ROCK 4D builds without ACPI tables, so Windows does
not start on it.

## Which Windows

RK3576 is ARMv8.0. Windows 11 24H2 and later need ARMv8.1 and cannot run on
it. Use Windows 11 23H2 (build 22631). Windows 10 22H2 should also run and
has not been tried.

## How to install

Windows Setup does not work on these boards: it has no driver for the eMMC
controller, and it refuses a machine without a TPM. The image is applied
with DISM instead, by one of two tools:

- the WinPE USB stick from
  [woa-rk3576](https://github.com/gahingwoo/woa-rk3576): the
  [install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md)
  has every step;
- [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio), from the PC
  over USB, without a stick. Not yet run on a board (2026-11-24).

Both need this firmware on the eMMC, written as a whole image first
([flashing](flashing.md)).

## Which firmware

| Under Windows | Firmware needed |
|---|---|
| Windows itself: eMMC, SD, Ethernet, USB-A, NVMe (CM5-IO) | 0.4.0 or later. Older images ran the eMMC at 148.5 MHz instead of 52 MHz, and files written under Windows could come back different with no error: before 0.3.0 on the TaishanPi 3M, before 0.4.0 on the CM5-IO. |
| TaishanPi 3M DSI screen | 0.4.0 or later |
| Touch, Bluetooth, headphone jack, HDMI audio, USB-C (TaishanPi 3M) | the `ci` build until the next release ([flashing](flashing.md#the-rolling-ci-build)) |

## More drivers

- GPU and display: [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52)
  draws the desktop on the Mali-G52, with rotation, brightness, HDMI
  hot-plug and HDMI audio. Run on the TaishanPi 3M; no release yet. Without
  it the desktop is drawn on the CPU into the firmware's framebuffer, at the
  one mode the firmware set.
- NPU: [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu), with
  "NPU (experimental)" turned on in setup. TaishanPi 3M only.

## The clock

The TaishanPi 3M's RTC holds no valid time, so without a network cable
([firmware clock](firmware-menus.md#clock)) the clock is wrong after every
boot until Windows syncs it. Set
`HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal`
to 1 (REG_DWORD) so Windows reads the firmware's clock as UTC. More in
woa-rk3576's [troubleshooting](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/troubleshooting.md).
