# Windows

English | [简体中文](windows.zh-CN.md)

Windows 11 23H2 (build 22631) runs from the eMMC of the ArmSoM CM5-IO and the
LCKFB TaishanPi 3M, and from NVMe on the Radxa ROCK 4D. Windows 11 24H2 and
later need ARMv8.1 and cannot run on RK3576 (ARMv8.0). Windows 10 22H2 should
run and has not been tried.

## How to install

Windows Setup does not work here (no eMMC driver, no TPM). The image is
applied with DISM by one of:

- the WinPE USB stick from [woa-rk3576](https://github.com/gahingwoo/woa-rk3576),
  following its [install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md);
- [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio), from the PC
  over USB without a stick. Run on the ROCK 4D (Windows to NVMe). On the
  ROCK 4D it also offers Windows on NVMe with Armbian on an SD card; that
  needs the SPL of branch `rock4d-parity`, not on `main` yet.

Both need this firmware written first ([flashing](flash.md)): as a whole
image to the eMMC (CM5-IO, TaishanPi 3M) or to the SPI NOR (ROCK 4D).

Keep "CPU Idle States" Disabled (the default): with it on, Windows stops at
boot with bugcheck 0x5C ([firmware menus](firmware-menus.md#options-for-operating-systems)).

## Which firmware

| Under Windows | Firmware needed |
|---|---|
| eMMC, SD, Ethernet, USB-A, NVMe (CM5-IO) | 0.4.0 or later. Older images ran the eMMC at 148.5 MHz and could silently corrupt files written under Windows. |
| TaishanPi 3M DSI screen | 0.4.0 or later |
| Touch, Bluetooth, headphone jack, HDMI audio, USB-C (TaishanPi 3M) | the [`ci` build](flash.md#the-rolling-ci-build) until the next release |
| Anything on the ROCK 4D | the `ci` build until the next release (0.4.1 has no ACPI tables for it). Ethernet needs a build from 2026-11-26 or later: older ones get a link but no DHCP address. |

## More drivers

- GPU and display: [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52)
  (rotation, brightness, HDMI hot-plug and audio). Run on the TaishanPi 3M
  and the ROCK 4D; no release yet. Without it the CPU draws the desktop into the firmware's
  framebuffer at one fixed mode.
- NPU: [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu), with
  "NPU (experimental)" on in setup. TaishanPi 3M and ROCK 4D.
- ROCK 4D Wi-Fi and Bluetooth (USB AIC8800D80): no driver yet.

## The clock

The TaishanPi 3M's RTC holds no valid time, so without a network cable
([firmware clock](firmware-menus.md#clock)) the time is wrong until Windows
syncs it. Set
`HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal`
to 1 (REG_DWORD) so Windows reads the firmware clock as UTC. More in
woa-rk3576's [troubleshooting](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/troubleshooting.md).
