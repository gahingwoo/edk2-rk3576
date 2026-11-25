# Starting Armbian once: "Armbian (U-Boot)"

English | [简体中文](armbian-boot.zh-CN.md)

For people who run this UEFI firmware, often with Windows, and want Armbian
on the same board. For developers: [design note](ARMBIAN-CHAINLOAD.md).

## What it does

A normal boot starts the UEFI firmware, which then starts Windows or
whatever comes first in the boot order. The boot manager has one more
entry, **Armbian (U-Boot)**. Choose it and the board restarts into
Armbian's own U-Boot, which starts Armbian with its own kernel and device
tree. The UEFI firmware does not run on that boot.

It works once. The next reboot, power cycle or crash starts the UEFI
firmware again. The entry is only listed when the firmware finds an Armbian
U-Boot it can start.

## What you need

| Board | Firmware | Where Armbian can be | Tested |
|---|---|---|---|
| LCKFB TaishanPi 3M | the [`ci` build](flash.md#the-rolling-ci-build) of `main` on the eMMC; release 0.4.1 does not have it | SD card, a partition of the eMMC next to Windows, or a USB disk | SD card: yes, on one board. eMMC partition and USB disk: not yet |
| ArmSoM CM5-IO | not in a release or the `ci` build yet (branch `cm5io-parity`) | SD card, firmware on the eMMC | no |
| Radxa ROCK 4D | not in a release or the `ci` build yet (branch `rock4d-parity`) | SD card only | no |

You need an Armbian image made for your board. It has to use Armbian's
mainline U-Boot. On the TaishanPi 3M, Armbian edge was tested. Images with
Rockchip's vendor U-Boot, such as the board maker's own Debian or Ubuntu,
are not supported. On the CM5-IO, use an edge image.

To get the firmware onto a TaishanPi 3M that already has Windows, use
[Updating the firmware and keeping everything else](flash.md#updating-the-firmware-and-keeping-everything-else)
with the `ci` image. It keeps Windows and your settings, and it also
writes the new boot loader stage the entry needs.

Where Armbian goes on the TaishanPi 3M:

- **SD card.** Write the Armbian image to the card with any image writer
  and put the card in before you power on. With the firmware on the eMMC,
  the card no longer starts Armbian by itself; only the entry does.
- **eMMC partition next to Windows.** In Maskrom Studio's Advanced mode,
  tick "Also install Armbian on the eMMC" when you install Windows. Do not
  also keep an SD card with the same Armbian image in the board.
- **USB disk** on a USB-A port (USB-C does not work in U-Boot). Each time
  you choose the entry, the firmware copies Armbian's U-Boot from the disk
  to a spare area of the eMMC.

## How to use it

1. Power on. During the countdown, press Esc or F2. On the touchscreen,
   touch with two fingers.
2. Open **Boot Manager**.
3. Choose **Armbian (U-Boot)**. The screen says what it found, then
   "Request written. Restarting". The board restarts into Armbian.

The menus can be switched to Simplified Chinese with "Select Language" on
the front page. The entry keeps its name, and the messages it prints stay
in English.

## Going back to UEFI and Windows

Reboot or power off from Armbian. The next boot is the UEFI firmware.

On the TaishanPi 3M you cannot choose Armbian from Windows' restart
options, because the board loses that choice during the restart. Use the
boot manager.

## Keep the firmware safe

Do not run `armbian-install`, and do not use the boot loader update in
`armbian-config`. They can write Armbian's U-Boot over the firmware on the
eMMC. Getting the firmware back then needs MaskROM ([flashing](flash.md)).

## Troubleshooting

**The entry is not in the boot manager.**

- The firmware is older than the `ci` build of `main`, or the board is a
  CM5-IO or a ROCK 4D, where the feature is not released yet.
- The SD card or USB disk went in after power-on. The firmware looks once
  per boot. Reboot.
- The card or disk has no Armbian U-Boot, for example an image with a
  different U-Boot, or a card that got only a copy of Armbian's partition.
- Armbian was installed by hand into an eMMC partition. The firmware does
  not search file systems at boot. Maskrom Studio's install puts Armbian's
  U-Boot where the firmware finds it.

**Choosing it prints an error.** The message says why. Press a key to go
back to the menu.

**The screen is black after choosing it.** This is normal for a while. On
the TaishanPi 3M, Armbian's U-Boot does not use the screen. The picture
comes when Armbian's kernel starts its display driver. If nothing shows
after a few minutes, a serial console at 1500000 8N1 shows how far it got;
lines starting with `ArmbianOnce/1` come from the boot loader. A power
cycle brings back the UEFI firmware.

**Armbian starts but there is no picture.** On this path Armbian drives the
display with its own device tree and settings. The firmware's display
options do not apply. HDMI was tested. On the TaishanPi 3M, Armbian turns
on its DSI screen setting the first time it sees the screen and then
restarts. That restart lands in UEFI, so choose Armbian (U-Boot) again. The
DSI screen on this path has not been tested.

**I want Armbian to start by default.** Not supported. The entry is never
part of the normal boot order, so a crash or a power cut always brings back
a working UEFI. If you only want Armbian, run it without this firmware.
