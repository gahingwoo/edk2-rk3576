# LCKFB TaishanPi 3M

[简体中文](../zh-CN/boards/tspi3m.md)

The board most of this work runs on: Windows 11 23H2 and Armbian from the
eMMC, LCKFB's DSI screen with touch, HDMI. Everything below is from one
board. The board was sponsored by JLC (嘉立创).

## What works, 2026-11-24

| | In the firmware | Windows 11 23H2 | Linux (Armbian edge) |
|---|---|---|---|
| HDMI | works, mode from the monitor's EDID | works; hot-plug and HDMI audio with [wddm-mali-g52](https://github.com/gahingwoo/wddm-mali-g52) | not tried with "Force UEFI GOP Display" off |
| DSI screen (LCKFB 800x1280) | works, with touch | works, landscape and touch with wddm-mali-g52 and gt9271 | works (Armbian's panel driver) |
| eMMC | works, settings are saved | works, boot disk | works |
| SD card | works | driver starts; a card in the slot was not tried | not tried |
| USB-A (4 ports) | works | works | works |
| USB-C | a device plugged in before power-on gets VBUS | works for a device plugged in before power-on | not working with the disk tried |
| Ethernet | works | works | works |
| Headphone jack | not used | playback and microphone work | playback works through ALSA; desktop sound not set up |
| Bluetooth | not used | works: pairing, file transfer; no voice | not tried |
| WiFi | not used | not working | not tried |
| GPU | powered for the OS | works with wddm-mali-g52, up to 702 MHz | disabled in the firmware's device tree |
| NPU | powered when the setup option is on | works with [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu) | disabled in the firmware's device tree |
| Fan | runs at 100% | runs at 100%, or by temperature with the setup option | stays at 100% |

Release 0.4.1 (2026-11-17) has the DSI screen, touch in the menus, Chinese
menus and Windows. Touch, Bluetooth, audio and USB-C under Windows need the
[`ci` build](../flash.md#the-rolling-ci-build) until the next release.

## Before you start

- Serial console (optional): 1500000 8N1.
- MaskROM: there is no button. Short the two pads marked `MSK`, directly
  under the `RK3576` silkscreen, while powering on, with the USB-C port
  marked `OTG+DP` connected to the computer. A board already running this
  firmware also goes to MaskROM with F4 during the boot countdown.
- The BootROM boots only from the eMMC, never from an SD card alone.

## Install the firmware

1. Write the `TaishanPi3M-emmc` image to the eMMC as a whole image
   ([flashing](../flash.md#lckfb-taishanpi-3m)). This replaces LCKFB's
   stock system.
2. Later updates: "keep the partitions"
   ([flashing](../flash.md#updating-the-firmware-and-keeping-everything-else)),
   which keeps Windows, Linux and the settings.

Then:

- Windows: [woa-rk3576's install guide](https://github.com/gahingwoo/woa-rk3576/blob/main/docs/INSTALL.md),
  or [Maskrom Studio](https://github.com/gahingwoo/maskrom-studio) without a
  USB stick. See [Windows](../windows.md).
- Linux: [Linux](../linux.md).

## DSI screen and touch

- The firmware looks for the touch controller on I2C0 and uses the screen
  when it is there, HDMI when it is not.
- Plug or unplug the ribbon only with the power off.
- The menus are drawn rotated to landscape. "Screen Rotation" in the
  platform setup changes that. An operating system gets the panel in
  portrait and rotates it itself.
- Touch in the menus: drag moves the highlight, a tap is Enter, two fingers
  or a long press is Esc. "Touch Test" in the boot manager shows where
  touches land.
- The keyboard key near the top right corner opens an on-screen keyboard.
- "Screen Brightness (%)" on the display page sets the backlight from 10 to
  100%.

If the screen shows only the backlight while touch still works, re-seat the
ribbon with the power off before anything else.

## USB-C

The firmware turns on the port's 5 V only when, at power-on, a device that
needs power (a USB disk, a hub) is attached. Nothing watches the port after
that:

- plug a USB device in before powering on;
- a device plugged in later gets no power until the next boot;
- reboot before connecting a charger or a PC to this port.

## Clock

The board's RTC loses its time at every power-off and reset. With a network
cable in, the firmware sets the clock over NTP at each boot
([firmware menus](../firmware-menus.md#clock)); without one, the clock is
wrong until the OS syncs it.

## Fan

The firmware starts the fan on CN2 at full speed and leaves it running.
Under Windows, "Fan Control by ACPI Thermal Zone" in setup (off by default)
lets the temperature set it. Under Linux it stays at full speed.

## Board facts

PHY, GPIOs, I2C addresses, the GPU supply and the rest:
[hardware](../dev/hardware.md#lckfb-taishanpi-3m).
