# Linux

English | [简体中文](linux.zh-CN.md)

Linux boots through the firmware as on a PC: from a USB stick, NVMe or the
eMMC, through GRUB, with the firmware's device tree. Nothing has to be put on
the ESP.

## Radxa ROCK 4D and ArmSoM CM5-IO

Fedora (44 on the ROCK 4D, 45 on the CM5-IO) installs from a Fedora ARM64
USB stick to NVMe. On the CM5-IO GNOME runs on the Mali-G52 through Panfrost.

1. Write the firmware ([flashing](flash.md)).
2. Boot the Fedora installer stick from the boot manager.
3. Install to the NVMe disk.

## LCKFB TaishanPi 3M

Run with Armbian edge (Linux 7.3.4, GNOME) in a 6 GB eMMC partition next to
Windows, started through GRUB. The firmware's device tree adds to the
mainline board file: the ES8388 headphone codec, the GT9271 touchscreen, the
PWM backlight, the DSI screen (when attached), mini-PCIe power for an EC20
modem, and the GPU (disabled). Checked: headphones (ALSA), touch, backlight
(needs Armbian's RK3576 PWM driver), USB-A, Ethernet, the EC20.

### The screen: two ways

"Force UEFI GOP Display" on the ACPI / Device Tree page:

- **Off: Linux drives the screen.** The firmware stops its picture at
  ExitBootServices and the kernel's VOP2 and DSI drivers take over. Needs a
  kernel with a driver for the panel (Armbian's rocknix generic DSI driver;
  mainline has none). GNOME rotates the screen and touch to landscape. The
  screen is dark until that driver starts.
- **On: Linux keeps the firmware's picture** (simpledrm or efifb), a fixed
  800x1280 portrait mode drawn by the CPU. For landscape, add a udev rule
  with `LIBINPUT_CALIBRATION_MATRIX` `0 -1 1 1 0 0` for the touchscreen and
  rotate the output `left`. HDMI and HDMI audio do not work in this mode.

### Limits

- The GPU is disabled in the firmware's device tree: with it enabled,
  panfrost probes and the kernel panics with an SError when a GPU job times
  out. Cause unknown.
- Armbian's screen service writes a U-Boot overlay setting and restarts,
  which loops under UEFI: mask `lckfb-tspi-3m-screen.service`.
- Under UEFI the sound card is named after SMBIOS (`LCKFB-TaishanPi3M-0`),
  not `rockchip-es8388`, so Armbian's ALSA UCM profile is not found. With the
  profile linked under the new name the card is set up, but GNOME output and
  the microphone are still silent.
- HDMI with "Force UEFI GOP Display" off has not been tried.
- USB-C did not enumerate a disk (the kernel's tcpm takes the port); USB-A
  works.
- An installer does not know that Windows shares the eMMC. The install above
  was written by hand into a partition Windows made room for.
- `rtc-hym8563: no valid clock/calendar values` is expected; the clock comes
  from the network.

Armbian's own images for the TaishanPi 3M and the CM5-IO boot with U-Boot,
not with this firmware.
