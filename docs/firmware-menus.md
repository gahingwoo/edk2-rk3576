# Firmware menus and features

English | [简体中文](firmware-menus.zh-CN.md)

All three boards unless a board is named. Serial console: 1500000 8N1.

| Key | When | Does |
|---|---|---|
| F4 | boot countdown | reset into MaskROM |
| F9 | boot countdown | Reset All Settings |
| Ctrl+Alt+F12 (F12 on serial) | anywhere | screenshot |
| Ctrl+Alt+F11 (F11 on serial) | anywhere | debug report |

## Language

Simplified Chinese can be chosen on the front page. Chinese text reaches the
serial console as UTF-8.

## Screenshots

Ctrl+Alt+F12, F12 on the serial console, or the camera key of the on-screen
keyboard. Saved as `\uefi-screenshots\YYYYMMDD-HHMMSS.png` (`shot-NNNN.png`
without a set clock) on a FAT medium. "Save Screenshots To" in the platform
setup picks Automatic (USB stick, then SD card, then the ESP) or one of them.
A square flashes in the top left corner: green saved, yellow nowhere to
write, red failed. The serial log has the path.

## Debug report

Device Manager > Debug Report, or Ctrl+Alt+F11. It writes
`\uefi-debug\YYYYMMDD-HHMMSS\` to the same place as screenshots: this boot's
firmware log, a summary (firmware, board, boot options, display, PCI, USB,
storage, memory map), the ACPI tables, the device tree and, from Device
Manager, a screenshot of every setup page (do not touch anything while it
runs). Attach the folder to an
[issue](https://github.com/gahingwoo/edk2-rk3576/issues).

## Reset All Settings

On the front page, or F9 during the boot countdown. It erases the whole UEFI
variable store (setup settings, language, boot options and order, timeout,
Secure Boot keys) and nothing else. Answer Yes to the warning; the board
restarts as on its first boot. Add back boot options you made by hand, for
example with `bcfg boot add` in the UEFI Shell.

## Clock

No board keeps time across a power-off. With a network cable in, the
firmware sets the clock (UTC) from NTP at every boot ("Network Time Sync",
on by default). It tries the DHCP server's NTP servers, then pool.ntp.org and
time.cloudflare.com, then fixed Cloudflare and Google addresses. It runs in
the background, never delays the boot, and gives up 10 s after getting an
address (30 s without one). The serial log lines `NetworkTimeSync:` and
`NetworkTimeSync: stage:` show the result.

## Boot entries written by an OS

On SPI NOR (ROCK 4D) entries written by `efibootmgr` or `bcdboot` are saved
at once. On eMMC (CM5-IO, TaishanPi 3M) they are lost at the next reset,
except BootNext, unless "Save OS Variable Changes to eMMC" (experimental,
off by default) is on; then the firmware writes them when the OS restarts
or shuts down. Tested on the TaishanPi 3M only.

## Display

- Display setup page: "Display Mode", "Output Priority", "Force Output",
  "Duplicate Output", "HDMI Signaling".
- "Screen Rotation" rotates the menus, the UEFI Shell and the touch test,
  not the OS.
- "Screen Brightness (%)": TaishanPi 3M DSI backlight, 10 to 100%.
- "Force UEFI GOP Display" (ACPI / Device Tree page): whether Linux keeps
  the firmware's picture or drives the display itself ([Linux](linux.md)).

## On-screen keyboard, mouse and touch

- TaishanPi 3M DSI screen: the keyboard key near the top right corner opens
  the on-screen keyboard; Hide closes it. It works in setup, the UEFI Shell,
  GRUB and the Windows boot manager.
- Any board with a USB mouse: the keyboard and a cursor appear; wheel is
  Up/Down, left click Enter, right click (off the keyboard) Esc. Not run on a
  board yet.
- Touch (TaishanPi 3M): drag moves the highlight, tap is Enter, two fingers
  or a long press is Esc. "Touch Test" in the boot manager shows touches.

## Power off

"Shut Down" on the front page, `reset -s` in the UEFI Shell, or a Windows
shutdown. On the TaishanPi 3M and the ROCK 4D the firmware switches the RK806 PMIC off over
I2C1, and arms it for the PSCI call Windows makes.

## Options for operating systems

On the ACPI / Device Tree and CPU pages; each takes effect at the next boot.
All are off by default except where the table says otherwise.

| Option | What it does |
|---|---|
| Config Table Mode | ACPI + device tree (default on CM5-IO, TaishanPi 3M and ROCK 4D), ACPI only, or device tree only |
| CPU Maximum Frequency | 1608 MHz on both clusters (default) or Max (2208 / 2016 MHz). On the first boot with a screen the firmware asks once (10 s timeout, kept in variable `CpuMaxFreqAsked`) and restarts if the answer changes it. TaishanPi 3M and ROCK 4D |
| GPU voltage for 702 MHz (775 mV) | raises the GPU supply (RK806 BUCK5) from 750 to 775 mV before the OS starts, so the Windows GPU driver runs 702 MHz instead of 594 MHz. Only on a plain RK3576 with thermal shutdown armed. Off puts a raised rail back to 750 mV. Keep a fan running. On by default on the TaishanPi 3M |
| CPU Idle States | tells the OS about the cores' idle states; off means WFI only |
| Fan Mode | Always on (default, full speed from power-on, never off); Performance / Balanced / Quiet: Windows switches the fan with the big-core temperature, on at 50 / 60 / 70 C, off 10 C lower |
| NPU (experimental) | powers the NPU and declares it in ACPI for [woa-rk3576-npu](https://github.com/gahingwoo/woa-rk3576-npu). TaishanPi 3M and ROCK 4D |
| 40-pin UART (experimental) | header UART pins as COM ports for the woa-rk3576 rk3xuart driver (never the debug UART). TaishanPi 3M; CM5-IO and ROCK 4D on their board branches |
| 40-pin GPIO/I2C/SPI for apps (experimental) | header GPIO, I2C and SPI for Windows.Devices.Gpio, .I2c, .Spi (ACPI RhProxy). Same boards |
| Save OS Variable Changes to eMMC | see [above](#boot-entries-written-by-an-os) |
