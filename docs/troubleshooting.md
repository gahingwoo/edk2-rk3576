# Troubleshooting

English | [简体中文](troubleshooting.zh-CN.md)

To report a problem, save a [debug report](firmware-menus.md#debug-report)
and attach it to an [issue](https://github.com/gahingwoo/edk2-rk3576/issues).

| Problem | Fix |
|---|---|
| No MaskROM device (`2207:350e`) | TaishanPi 3M: short the `MSK` pads before power-on, use the USB-C port marked `OTG+DP`. ROCK 4D: USB-A to USB-A cable on the upper USB 3.0 port (USB-C has no data lines). CM5-IO: the USB-C port, nothing else plugged in. On Windows, install WinUSB with [Zadig](https://zadig.akeo.ie/). |
| `Opening loader failed` | `db` was given `binaries/rk3576_ddr.bin`. Use an RKLD loader (starts with `LDR `), such as rkbin's `rk3576_spl_loader_*.bin`. |
| `Write LBA failed` right after `db` | Wait a few seconds and write again. |
| Old firmware still boots after a write | The write did not reach 100%. Write again before `rd`. |
| CM5-IO ignores the SD card | The BootROM tries the eMMC first; the card is used only when the eMMC has no bootloader. The TaishanPi 3M never boots an SD card alone ([flashing](flash.md#lckfb-taishanpi-3m)). |
| Settings lost after an update | The write covered sector <!-- generated:nvdata-sector -->43648<!-- /generated -->, where the settings are. Use "keep the partitions" or the `dd` range in [flashing](flash.md#updating-the-firmware-and-keeping-everything-else). |
| Boot entries made in Linux or Windows gone after a reboot | On eMMC, turn on "Save OS Variable Changes to eMMC" ([firmware menus](firmware-menus.md#boot-entries-written-by-an-os)). Reset All Settings also removes hand-made entries. |
| Wrong clock | No board keeps time across a power-off. Connect a network cable (NTP at boot) and let the OS sync. |
| TaishanPi 3M screen: backlight only, touch works | The DSI ribbon is loose. Power off and re-seat it. |
| Ventoy stick does not boot WinPE | Ventoy's UDF driver hits a synchronous exception on this firmware. Write the WinPE image directly. |
| Windows 11 24H2 does not start | RK3576 is ARMv8.0; 24H2 needs ARMv8.1. Use 23H2. |
| Windows stops at boot with bugcheck 0x5C | Set "CPU Idle States" to Disabled (the default) in setup. |
| ROCK 4D, Windows: Ethernet link up, no DHCP address | Update the firmware to 0.5.0 or later (it sets the RTL8211F RGMII delays). |

## Recovery

MaskROM is in the BootROM and nothing here writes it, so a board can always
be put back into MaskROM and written again.
