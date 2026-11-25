# Known issues

English | [简体中文](known-issues.zh-CN.md)

As of `main` on 2026-11-24.

| Area | Issue |
|---|---|
| Windows | 11 24H2 and later cannot run (RK3576 is ARMv8.0); use 23H2. No Windows on the ROCK 4D (no ACPI tables). |
| Windows | WiFi does not work on either board; Bluetooth only on the TaishanPi 3M. |
| Windows | TaishanPi 3M touch, Bluetooth, audio and USB-C need the `ci` build until the release after 0.4.1. |
| USB-C, TaishanPi 3M | VBUS only for a device attached at power-on. Reboot before connecting a charger or a PC. |
| USB-C, ROCK 4D and CM5-IO | USB 2.0 outside Linux: SuperSpeed does not train (DRD0's link stays in Rx.Detect). |
| Boot entries | On eMMC, entries written by an OS survive a reset only with "Save OS Variable Changes to eMMC" on. |
| Clock, TaishanPi 3M | The HYM8563 RTC loses its time at every power-off and reset; the clock and screenshot names are wrong until NTP or the OS sets it. |
| Linux, TaishanPi 3M | GPU disabled in the device tree (panfrost SError on a job timeout); desktop sound silent ([Linux](linux.md#limits)). |
| Device tree | The ComboPHY and PCIe fix-ups look for `/pcie@...` at the root instead of under `/soc/`, so they do nothing. NVMe works without them. |
