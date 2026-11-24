# Firmware menus and features

What the firmware offers before an operating system starts. Applies to all
three boards unless a board is named. [简体中文](zh-CN/firmware-menus.md)

Serial console: 1500000 8N1. Keys during the boot countdown: F4 resets into
MaskROM, F9 starts Reset All Settings.

## Language

The setup menus can be switched to Simplified Chinese on the front page.
Chinese text also reaches the serial console, as UTF-8, from 0.4.1 on.

## Screenshots

Press Ctrl+Alt+F12 on a keyboard, F12 on the serial console, or the camera
key of the on-screen keyboard. The picture is saved as
`\uefi-screenshots\YYYYMMDD-HHMMSS.png` (`shot-NNNN.png` when the clock is not
set) on a FAT formatted medium.

"Save Screenshots To" in the platform setup picks where: Automatic (a USB
stick first, then an SD card, then the ESP), or only one of them. A green
square flashes in the top left corner when it is saved, yellow when there is
nowhere to write, red when it failed. The serial log has the path.

## Debug report

To report a problem, save a debug report: Device Manager > Debug Report, or
Ctrl+Alt+F11 (F11 on the serial console). It writes
`\uefi-debug\YYYYMMDD-HHMMSS\` to the same place as screenshots:

- the firmware log of this boot;
- a summary: firmware, board, boot options, display, PCI, USB and storage
  devices, memory map;
- the ACPI tables and the device tree;
- from Device Manager, a screenshot of every setup page. While it walks the
  pages, "Collecting debug report, please do not touch anything..." stays in
  the top left corner.

Attach the folder to the
[issue](https://github.com/gahingwoo/edk2-rk3576/issues).

## Reset All Settings

"Reset All Settings" on the front page, or F9 during the boot countdown
(from a serial terminal too), puts the firmware back to how a freshly
written board starts. It erases the whole UEFI variable store: every setup
setting, the language, the boot options and boot order, the timeout and any
Secure Boot keys. Nothing outside the variable store is touched.

1. Choose it. A red warning asks first, with No selected.
2. Choose Yes. The board restarts, erases the store and sets itself up as
   on its first boot.
3. Add back any boot options you had made by hand (an OS, a second loader),
   for example with `bcfg boot add` in the UEFI Shell.

## Clock

None of the boards keeps time across a power-off in the firmware. With a
network cable in, the firmware sets its clock from NTP at every boot
("Network Time Sync" in the platform setup, on by default):

- it asks the NTP servers the DHCP server names, then pool.ntp.org and
  time.cloudflare.com, then fixed Cloudflare and Google addresses;
- it runs in the background while the boot menu counts down and never delays
  the boot;
- it gives up 10 seconds after getting an address (30 seconds without one);
- the clock is kept in UTC.

The serial log has a `NetworkTimeSync:` line with the old and the new time,
and a `NetworkTimeSync: stage:` line saying how far it got (no link, no
lease, DNS timeout, NTP timeout, or clock set).

## Boot entries written by an OS

On SPI NOR (ROCK 4D), boot entries an OS writes (`efibootmgr`, `bcdboot`)
go to the flash at once.

On eMMC (CM5-IO, TaishanPi 3M) they are kept in memory and lost at the next
reset, except BootNext, unless "Save OS Variable Changes to eMMC" is on
(off by default, marked experimental). With it on, the firmware writes them
to the eMMC when the OS restarts or shuts down the board. Checked on the
TaishanPi 3M on 2026-11-22 with `efibootmgr`, 3 runs.

## Display

On the display setup page:

- "Display Mode", "Output Priority", "Force Output", "Duplicate Output",
  "HDMI Signaling" choose the mode and the output.
- "Screen Rotation" turns the menus, the UEFI Shell and the touch test. An
  operating system gets the panel as it is and rotates it itself.
- "Screen Brightness (%)" sets the TaishanPi 3M's DSI backlight, 10 to 100%.

"Force UEFI GOP Display" on the ACPI / Device Tree page decides whether
Linux keeps the firmware's picture or drives the display itself
([Linux](linux.md)).

## On-screen keyboard and mouse

On the TaishanPi 3M's DSI screen the small keyboard key near the top right
corner opens an on-screen keyboard; Hide on it closes it. Its keys reach
setup, the UEFI Shell, GRUB and the Windows boot manager.

The same keyboard appears on every board as soon as a USB mouse is plugged
in, with a mouse cursor: the wheel is Up and Down, a left click Enter and a
right click Esc anywhere off the keyboard. The mouse part has not been run on
a board yet (2026-11-24).

## Touch (TaishanPi 3M)

Touch works in the menus as keys: drag moves the highlight, a tap is Enter,
two fingers or a long press is Esc. "Touch Test" in the boot manager shows
where touches land.

## Power off

"Shut Down" on the front page, `reset -s` in the UEFI Shell and a shutdown
from Windows power the board off. On the TaishanPi 3M the firmware switches
the RK806 PMIC off over I2C1 itself, and arms it for the PSCI call Windows
makes.

## Options for operating systems

On the ACPI / Device Tree and CPU pages. Each takes effect at the next boot.
Except Config Table Mode and CPU Maximum Frequency, all are off by default.

| Option | What it does |
|---|---|
| Config Table Mode | ACPI + device tree (default on CM5-IO and TaishanPi 3M), ACPI only, or device tree only |
| CPU Maximum Frequency | 1608 MHz on both clusters (default), or Max (2208 / 2016 MHz) |
| CPU Idle States | tells the OS about the cores' idle states; off means WFI only |
| Fan Control by ACPI Thermal Zone | lets Windows run the fan from the big-core temperature; off means the fan stays at the speed the firmware set |
| NPU (experimental) | powers the NPU and declares it in ACPI, for [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu); TaishanPi 3M only |
| Save OS Variable Changes to eMMC | see above |
