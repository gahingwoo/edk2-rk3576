# Known issues

As of `main` on 2026-11-24. [简体中文](zh-CN/known-issues.md)

- Windows 11 24H2 and later cannot run: RK3576 is ARMv8.0. Use Windows 11
  23H2.
- The ROCK 4D builds without ACPI tables, so Windows does not start on it.
- TaishanPi 3M USB-C: the firmware turns on VBUS only for a device attached
  at power-on. A device plugged in later gets no power until the next boot.
  Reboot before connecting a charger or a PC to that port.
- On the ROCK 4D and the CM5-IO the USB-C port runs at USB 2.0 outside
  Linux. The firmware brings up the USBDP PHY's USB3 half at
  ExitBootServices, which stopped Windows bugchecking at shutdown, but
  SuperSpeed has not trained there: DRD0's link stays in Rx.Detect.
- On eMMC, boot entries written by an OS survive a reset only with the
  experimental "Save OS Variable Changes to eMMC" option on.
- The ComboPHY and PCIe device-tree fix-ups look for `/pcie@...` at the
  root, where mainline has `/soc/`, so they do nothing. NVMe works without
  them.
- The TaishanPi 3M's RTC (HYM8563) loses its time at every power-off and
  reset, so the firmware's clock, and the names of screenshots and debug
  reports, are wrong until NTP or an OS sets it.
- TaishanPi 3M under Linux: the GPU is disabled in the firmware's device
  tree (panfrost ends in an SError when a job times out), and desktop sound
  is silent ([Linux](linux.md#limits-2026-11-24)).
- WiFi does not work under Windows on either board, and Bluetooth only on
  the TaishanPi 3M.
- Everything listed for the TaishanPi 3M under Windows beyond the desktop
  (touch, Bluetooth, audio, USB-C) needs the `ci` build until the next
  release after 0.4.1.
