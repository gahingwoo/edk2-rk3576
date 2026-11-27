# LCKFB TaishanPi 3M

English | [简体中文](tspi3m.zh-CN.md)

Windows 11 23H2 and Armbian from the eMMC, LCKFB's DSI screen with touch,
HDMI. Results are from one board, sponsored by JLC (嘉立创).

## What works

Board-tested with `main` builds through 2026-11-24. Touch, Bluetooth, audio
and USB-C under Windows need firmware [0.5.0](https://github.com/gahingwoo/edk2-rk3576/releases/tag/0.5.0) or later.

| | In the firmware | Windows 11 23H2 | Linux (Armbian edge) |
|---|---|---|---|
| HDMI | works, mode from EDID | works; hot-plug and HDMI audio with [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) | not tried with "Force UEFI GOP Display" off |
| DSI screen (LCKFB 800x1280) | works, with touch | works, landscape and touch with wddm-mali-g52 and gt9271 | works (Armbian's panel driver) |
| eMMC | works, settings saved | works, boot disk | works |
| SD card | works | driver starts; no card tried | not tried |
| USB-A (4 ports) | works | works | works |
| USB-C | VBUS only for a device attached at power-on | the same | a disk did not enumerate |
| Ethernet | works | works | works |
| Headphone jack | not used | playback and microphone | playback through ALSA; desktop sound not set up |
| Bluetooth | not used | pairing, file transfer; no voice | not tried |
| WiFi | not used | not working | not tried |
| GPU | powered for the OS | wddm-mali-g52, up to 702 MHz | disabled in the firmware's device tree |
| NPU | powered when the setup option is on | [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | disabled in the firmware's device tree |
| Fan | 100% | 100%, or by temperature with the setup option | 100% |

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: no button. Short the two `MSK` pads directly under the `RK3576`
  silkscreen while powering on, with the USB-C port marked `OTG+DP` connected
  to the computer. A board running this firmware also enters MaskROM with F4
  during the boot countdown.
- The BootROM boots only from the eMMC, never from an SD card alone.

## Install the firmware

1. Write the `TaishanPi3M-emmc` image as a whole image
   ([flashing](../flash.md#lckfb-taishanpi-3m)). This replaces LCKFB's stock
   system.
2. Later updates: "keep the partitions"
   ([flashing](../flash.md#updating-the-firmware-and-keeping-everything-else)),
   which keeps Windows, Linux and the settings.

Then [Windows](../windows.md) (or [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio)
without a USB stick) and [Linux](../linux.md).

## Board notes

- DSI screen: the firmware uses it when the touch controller answers on
  I2C0, otherwise HDMI. Plug the ribbon only with the power off. The menus
  are drawn in landscape ("Screen Rotation" changes that); an OS gets the
  panel in portrait. If only the backlight comes on while touch works,
  re-seat the ribbon first.
- USB-C: the firmware turns on 5 V only for a device that needs power (USB
  disk, hub) attached at power-on, and nothing watches the port after that.
  Plug devices in before power-on; reboot before connecting a charger or a
  PC.
- Clock: the RTC loses its time at every power-off and reset. With a network
  cable the firmware sets it over NTP ([firmware menus](../firmware-menus.md#clock)).
- Fan (CN2): full speed from power-on. Under Windows "Fan Mode" (Always on by
  default) can switch it with the temperature; under Linux it stays at full
  speed.

## Buttons

There is no power button (nothing on the PMIC's PWRON line); the board powers
on when power is applied.

| Button | Before the OS | Under Windows (ACPI) | Under Linux (device tree) |
|---|---|---|---|
| RECOVERY | nothing | power button: does what Windows' Power Options say | `KEY_VENDOR` |
| RESET | resets the board | resets the board | resets the board |

RECOVERY as the power button needs the woa-rk3576 driver rk3xsaradc and the
setup option "ADC keys (experimental)" (on by default here). Holding it during
boot does nothing: a key counts only after it has been seen released. If
Windows has trouble with the ADC device, turn the option off and the firmware
leaves the ADC alone.

## Board facts

[Hardware](../dev/hardware.md#lckfb-taishanpi-3m).
