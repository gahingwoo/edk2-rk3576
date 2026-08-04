# Status

What actually works, what does not, and how confidently we know.

**Rule for this file: every claim carries its sample count.** "HDMI is
intermittent" is not a status; "2 of 8 cold boots produced a picture" is.
The predecessor of this file (`KNOWN_ISSUES.md`) drifted until four of its
entries were false, which is what a status file without evidence turns into.

Last updated: 2026-08-04, after the repository restructure.

---

## Verified on hardware

| Thing | Evidence |
|---|---|
| Boot chain to UEFI Shell | BootROM → SPL → BL31 → EDK2, serial 1500000 8N1. Reproduced on every boot of both boards. |
| eMMC (CM5-IO) | Enumerates and boots at 26 MHz legacy SDR. Capped there deliberately — see `PcdDwcSdhciForceDefaultSpeed` in `Platform/ArmSoM/CM5IO/CM5IO.dsc`. |
| USB-A (CM5-IO) | Devices enumerate through the onboard 4-port hub. |
| GbE (CM5-IO) | Link and DHCP, after the crystal-less YT8531 fix (SoC 25 MHz via `clk_mac_refout`). |
| ACPI tables | `acpiview` in the UEFI Shell shows the expected tables. |
| NVRAM persistence (CM5-IO) | A variable set from the menu survives a reboot. |
| `\EFI\BOOT` fallback boot | Reaches the OS loader. |

## Not working

### HDMI output is intermittent — 2 of 8 cold boots produced a picture

The single largest open item. Measured with a capture card and a pixel
verdict, not by eye. Every register that can be read back was identical on
the boots that produced output and the boots that did not.

Ruled out, each with its own sample count, in the notes that came with the
board bring-up: PHY quiesce ordering, HPD sampling, SCDC, retimed writes,
warm-reset paths, and DDC timing. None of them changed the ratio.

The lead that has never been examined: `DCLK_VOP0` as the SPL leaves it.
`Vop2Dxe.c` says as much where it skips the runtime clock mux —

> KNOWN RISK: if the SPL leaves DCLK_VOP0 sourced from a non-HDMI PLL, pixel
> clock accuracy at 4K@60 may suffer until CRU programming is implemented for
> RK3576.

There is no RK3576 CRU programming in this firmware at all. See the CRU item
below.

### PCIe link training fails in UEFI

The controller comes out of reset and DBI reads back the right VID:DID
(`0x1D87:0x3576`), but `PciHostBridgeDxe` times out waiting for L0. The same
slot and endpoint train under Linux, so the PHY reaches the link partner.
Most likely missing: combo PHY lane-0 RX equalisation, a longer wait window,
or retraining on hot-plug interrupts. The boot is not affected — the bridge
is simply never exposed to `PciBusDxe`.

### Windows on Arm stops at ExitBootServices

Reaches ExitBootServices and hangs. Display output is the blocker for
diagnosing further. Separately: RK3576 is ARMv8.0, so Windows 11 24H2 and
later cannot boot on it regardless — target 23H2 or Windows 10.

---

## Known-wrong things carried over deliberately

These are defects that were found during the restructure and **not** fixed in
it, because fixing them changes runtime behaviour and needs a hardware pass.
Each is isolated and commented at the point where it lives.

### PlatformCruLib is still RK3588's clock tree

`Silicon/Rockchip/RK3576/Library/Rk3576CruLib/Rk3576CruLib.c` carries RK3588's
PLL rate table, clock IDs and register offsets. Only the base addresses are
RK3576's, and only because `<Soc.h>` now resolves to RK3576's copy. The
`RK3588_*` macro names are left in place so this stays visible.

Nothing on RK3576 is known to reach that table: `Vop2Dxe` was the one caller,
through code that only ever compiled for RK3588. But "not known to be reached"
is not "unreachable", and the clock IDs in
`Silicon/Rockchip/RK3576/Include/Soc.h` are RK3588's too.

**Implementing real RK3576 CRU programming is the top open item**, and it is
the same item the HDMI intermittency points at.

### PlatformLib reads a variable under the wrong formset GUID

`Silicon/Rockchip/RK3576/Library/PlatformLib/Rk3576.c` looks up
`L"DebugSerialPortBaudRate"` under `gRK3588DxeFormSetGuid`. The formset that
would write it is `gRK3576DxeFormSetGuid`, so the lookup never finds anything
and the baud-rate override silently does nothing. Left at its original value
during the restructure; changing it is a behaviour change.

### The display PCDs still live in a token space named `gRK3588TokenSpaceGuid`

Declared in `Silicon/Rockchip/RockchipPkg.dec` now rather than in another
SoC's package, but the name and GUID are unchanged. They are `PcdsDynamicHii`,
so the token space GUID is baked into the runtime PCD database — renaming it
changes the firmware image and the stored variable layout, which is a separate
change with its own hardware pass.

### `--gzip-edk2` declares compression it does not perform

`scripts/lib/gen_fit_its.py` sets `compression = "gzip"` in the ITS for the
SD/eMMC path but does not actually compress the payload. Both the pre- and
post-restructure images are uncompressed, and both boot, so whatever reads
this either ignores the property or never needed it. Noted because the ITS
currently claims something untrue.

---

## Verify first on the next hardware session

**I2C now uses RK3576 controller addresses.** Before the restructure,
`I2cDxe` was compiled against RK3588's `Soc.h` and its `I2C_BASE(id)` macro,
so it programmed `0xFD880000` / `0xFEA90000` / `0xFEC80000`. On RK3576 those
land in ordinary DRAM — no fault, no error, and no I2C. Confirmed by
disassembling both firmware images: the old one contains only RK3588 I2C
addresses and the new one only RK3576's.

The new addresses are correct against the RK3576 TRM V1.2 (I2C0 `0x27300000`,
I2C1 `0x2AC40000`, I2C2 `0x2AC50000`, through I2C8 `0x2ACB0000`), as are the
eleven register offsets `I2cDxe` uses, and both boards already implement
`I2cIomux` with RK3576 pin functions. The bus and slave lists match the DTS
(`i2c1/pmic@23`, `i2c2/rtc@51`).

What is **not** verified: whether the I2C PCLKs are ungated when UEFI starts.
Previously this could not matter, because the writes went to DRAM.

Expected serial output if it works — `I2cDxe.c` prints one line per bus:

```
Succesfully installed controller 1 at 0x2ac40000
Succesfully installed controller 2 at 0x2ac50000
```

If it hangs instead, it will hang inside `I2cInitialise`, early and
identifiably. Roll back by flashing an image built from the `legacy/v0.1`
branch.

Also worth re-measuring in the same session, since the restructure touched the
display stack (dead-branch removal, byte-identical `.text` for most of it):
**the HDMI cold-boot ratio should still be about 2 in 8.** If it becomes 0 in
8, the restructure broke something.
