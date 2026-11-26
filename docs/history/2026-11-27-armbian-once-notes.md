# Armbian (U-Boot) once: measurements and board test

Kept as written on 2026-11-27, moved out of
[ARMBIAN-CHAINLOAD](../ARMBIAN-CHAINLOAD.md): what was measured on the
TaishanPi 3M to choose where the request lives, and the board test the
feature passed there.

## Why the request is on the eMMC

Nothing on the chip survives a reset on these boards: TF-A's PSCI
SYSTEM_RESET is a first global reset whose reset output makes the PMIC
power-cycle the SoC (`soc.c` `system_reset_init()`, `pmu.c`
`rockchip_soc_soft_reset()`). Measured on the TaishanPi 3M:

| Carrier | Result |
|---|---|
| PMU0/PMU1 GRF OS registers | cleared (OS_REG16 to 23 come back as power-on noise) |
| PMU SRAM | same power domain |
| DRAM | 0 of 3768 canaries survived |
| RTC registers | the RTC loses power at every reset |
| second global reset (`CRU_GLB_SRST_SND = 0xeca8`) | might keep the PMU domain, but nobody runs it on RK3576 and the BootROM/ddrbin behaviour after it is unknown; not chosen |
| **one eMMC sector** | **chosen**: survives anything, EDK2 already writes the eMMC and the SPL already reads it |

To test the second global reset later, from the UEFI Shell:
`mm 0x2602623C 0x12345678 -w 4 -n`, then `mm 0x27200C0C 0xECA8 -w 4 -n`; the
boot log line `PMU1 GRF OS_REG0-15` should end with `12345678`.

## Board test (TaishanPi 3M)

Needs an Armbian SD card (mainline U-Boot image), serial console at 1500000
and MaskROM access.

1. Flash `TaishanPi3M-emmc.img` keeping the partitions
   ([flashing](../flash.md#updating-the-firmware-and-keeping-everything-else)):
   it writes the SPL and the firmware, and the UEFI variables stay.
2. No card, boot: firmware and Windows as before; no `ArmbianOnce/1` line.
3. Card in, boot: `ArmbianOnce/1: not booting the U-Boot FIT on the SD card,
   the firmware is on the eMMC`, then the firmware. The entry is listed.
4. Choose the entry: `Request written. Restarting`, then SPL `request found
   and erased: booting U-Boot from the SD card, once`, Armbian's U-Boot,
   `boot.scr`, Armbian. In Armbian `/sys/firmware/efi` is absent and
   `dd if=/dev/mmcblk0 bs=512 skip=16383 count=1 status=none | tr -d '\0' | wc -c`
   prints 0 (use the eMMC's device name).
5. `sudo reboot`, `echo b | sudo tee /proc/sysrq-trigger`, and a power cut in
   Armbian's U-Boot each come back to the firmware. Repeat step 4 three times.
6. Card out: the entry is gone from the boot manager and BootOrder; card in:
   back, last.
7. Pull the card while the screen says `Restarting`: SPL `U-Boot was not
   loaded from the SD card, booting the firmware`; sector 16383 is zero.
8. Optional, slot: card out, Armbian USB disk on USB-A, choose the entry:
   `Copying it to the eMMC slot`, SPL `booting U-Boot from the eMMC slot,
   once`. A second use prints `The eMMC slot already holds that U-Boot.`

The SPL lines in the serial log of steps 3 to 5 are the evidence.
