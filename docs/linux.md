# Linux

[简体中文](zh-CN/linux.md)

Linux boots through the firmware like on a PC: from a USB stick, NVMe or
the eMMC, through GRUB, with the firmware's device tree. Nothing has to be
put on the ESP.

## Radxa ROCK 4D and ArmSoM CM5-IO

Fedora (44 on the ROCK 4D, 45 on the CM5-IO) installs from a Fedora ARM64
USB stick and boots from NVMe. On the CM5-IO the GNOME desktop runs on the
Mali-G52 through Panfrost.

1. Write the firmware ([flashing](flashing.md)).
2. Boot a Fedora ARM64 installer stick from the boot manager.
3. Install to the NVMe disk.

## LCKFB TaishanPi 3M

Run on one board with Armbian edge (Linux 7.3.4, GNOME), installed in a
6 GB partition of the eMMC next to Windows and started through GRUB from
the boot manager.

The firmware's device tree describes, besides the mainline board file, the
ES8388 headphone codec, the GT9271 touchscreen, the PWM backlight, the DSI
screen (only when it is attached), the mini-PCIe power for an EC20 modem and
the GPU (disabled, see below).

Checked on that install: the headphones play (through ALSA), touch, the
backlight from `/sys/class/backlight` (it needs the RK3576 PWM driver that
Armbian carries), the USB-A ports, Ethernet and the EC20.

### The screen: two ways

"Force UEFI GOP Display" on the ACPI / Device Tree page chooses:

- **Off: Linux drives the screen.** The firmware stops its own picture at
  ExitBootServices so the kernel's VOP2 and DSI drivers can take over, and
  the screen shows the GNOME login. It needs a kernel with a driver for the
  panel (Armbian's rocknix generic DSI driver; mainline has none yet). GNOME
  turns it to landscape from the panel's rotation and turns touch with it.
  The screen stays dark from power on until that driver starts.
- **On: Linux keeps the firmware's picture** through simpledrm or efifb: a
  fixed 800x1280 portrait mode drawn on the CPU. GNOME cannot tie the
  touchscreen to that output, so turning it to landscape needs a
  `LIBINPUT_CALIBRATION_MATRIX` udev rule for the touchscreen
  (`0 -1 1 1 0 0` with the output rotated `left`). The kernel's own display
  drivers are off in this mode, so HDMI and HDMI audio do not work in Linux.

### Limits, 2026-11-24

- The GPU is disabled in the firmware's device tree. Enabled, panfrost
  probes and then the kernel panics with an SError when a GPU job times out.
  Not understood yet.
- Armbian's board support does not expect UEFI yet. Its screen service
  writes a U-Boot overlay setting and restarts, which under UEFI is a loop:
  mask `lckfb-tspi-3m-screen.service`.
- Armbian's ALSA UCM profile is named after the sound card's long name.
  Under UEFI the kernel names the card after SMBIOS (`LCKFB-TaishanPi3M-0`)
  instead of `rockchip-es8388`, so PipeWire does not find it. With the
  profile linked under the new name the card is set up, but GNOME's output
  is still silent and the microphone records silence.
- HDMI with "Force UEFI GOP Display" off has not been tried on this board.
- The USB-C port did not enumerate a disk under Linux (the kernel's tcpm
  takes over the port), while USB-A worked.
- An installer will not know that Windows shares the eMMC. The Armbian
  install above was written by hand into a partition Windows made room for.
- The RTC holds no valid time: `rtc-hym8563: no valid clock/calendar
  values` is expected. The clock comes from the network.

Armbian's own images for the TaishanPi 3M and the CM5-IO boot with U-Boot,
not with this firmware.
