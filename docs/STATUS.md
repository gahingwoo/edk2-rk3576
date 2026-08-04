# Status

What actually works, what does not, and how confidently we know.

**Rule for this file: every claim carries its sample count.** "HDMI is
intermittent" is not a status; "2 of 8 cold boots produced a picture" is.
The predecessor of this file (`KNOWN_ISSUES.md`) drifted until four of its
entries were false, which is what a status file without evidence turns into.

Last updated: 2026-08-04, after the repository restructure and a ROCK 4D
hardware run on the restructured firmware.

---

## Verified on hardware

| Thing | Evidence |
|---|---|
| Boot chain to UEFI Shell | BootROM → SPL → BL31 → EDK2, serial 1500000 8N1. Reproduced on every boot of both boards. |
| **The restructured firmware itself** | ROCK 4D booted `rk3576-ROCK4D-legacy-v0.1-1-g652670d` to the front page with no ASSERT, no exception and no abort anywhere in the log. 1 boot. |
| eMMC (CM5-IO) | Enumerates and boots at 26 MHz legacy SDR. Capped there deliberately — see `PcdDwcSdhciForceDefaultSpeed` in `Platform/ArmSoM/CM5IO/CM5IO.dsc`. |
| USB-A (CM5-IO) | Devices enumerate through the onboard 4-port hub. |
| GbE (CM5-IO) | Link and DHCP, after the crystal-less YT8531 fix (SoC 25 MHz via `clk_mac_refout`). |
| ACPI tables | `acpiview` in the UEFI Shell shows the expected tables. |
| NVRAM persistence (CM5-IO) | A variable set from the menu survives a reboot. |
| `\EFI\BOOT` fallback boot | Reaches the OS loader. |

## Not working

### HDMI on ROCK 4D: HPD reads low, everything else succeeds

Distinct from the CM5-IO intermittency below. On the 2026-08-04 run, with a
monitor connected:

* `IOC_HDMI_HPD_STATUS` stayed `0x00000000` for the full 5 s wait, and
  `GPIO4_PC1` — the same pin read as a raw GPIO, with its pull cleared to
  NONE — also read 0. The pin is electrically low.
* The 5 V enable is asserted (`GPIO2_PB0 -> HIGH`, IOMUX confirmed as GPIO).
* Pin mux is right: `GPIO4_PC_IOMUX=0x9999`, matching mainline `hdmi_txm0_pins`.
* **DDC works anyway.** `GetEdid: DDC EDID read succeeded`, and a later SCDC
  write to the sink was ACKed. The sink is connected, powered, and talking.
* The firmware falls back (`HpdTimeoutFlag`), picks 2560x1440@60 from the EDID,
  installs the GOP, draws its test pattern, programs VOP2, and the HDPTX PLL
  locks on the first attempt (`PLL locked (attempt 0, iter 0)`).

So every step the firmware controls succeeded, and the sink answers on the DDC
pair, but HPD (HDMI pin 19) never comes back. A cable with a broken or absent
pin 19 produces exactly this — DDC is a different pair and keeps working.
Untested: swapping the cable, and whether HPD comes up on this same setup with
a `legacy/v0.1` image.

Note what DDC working does *not* prove: many sinks power their EDID EEPROM from
their own supply, so it is not evidence that HDMI +5 V is reaching the sink.

### CM5-IO HDMI is intermittent — 2 of 8 cold boots produced a picture

Measured with a capture card and a pixel verdict, not by eye.

The 2026-08-04 run on the restructured firmware is one more sample of the
no-picture case: HPD came up (`HPD_STATUS=0x000000E9`, `hw=HIGH`), EDID was
read, VOP2 came up at 1920x1080, the GOP framebuffer and the VOP2 window
agree on `0xED660000`, the test pattern was drawn there, and the HDPTX PLL
locked on the first attempt. The monitor showed a signal and a black screen.
That is 1 sample, consistent with the existing 6-in-8, and not evidence either
way about the restructure.

**A newly readable data point:** `Vop2Rk3576.c`'s native computation of
VP_DSP_CTRL agreed with the incremental path — `VP_DSP_CTRL 0x8000000F (legacy
and native agree)`. That cross-check had never been read on hardware before.

That boot was captured twice. Through a terminal, the log ends after the
PLL-lock trace with **45 consecutive bare newlines** and then unrelated output,
which reads as if HDMI setup aborted at step [3]. It did not: a raw capture of
the same boot (`scripts/serial-log.sh`) has all 22 `Setup: [N]` steps and no
newline run longer than zero.

So the entire bring-up completes on a boot that produces no picture:

```
Setup: [4]  VP0_DSP_CTRL post-mux = 0x0000000F  STANDBY=0
Setup: [10] Lanes OK — PHY_RDY + PLL_LOCK_DONE
Setup: [10e] Enable AVP video path (clear SWDISABLE) — PHY stable
Setup: [11] Clear avmute (PKT_CTL0=2 vendor), enable GCP_TX
Setup: [12] SUCCESS — ConnectorEnable exit Success
Step12: GPIO4_EXT_PORT=0x000E0100 GPIO4_PC1_HPD_pad=1
```

HPD is still high at the end, VP0 is out of standby, the lanes trained, AVMUTE
is cleared, and `RK_VOP2_DIAG_READS` is 0 so the suspected VOP2 read side
effects are not in play. This is the first complete, un-eaten capture of a
no-picture boot — the three earlier rounds of instrumentation were all lost to
the console clear. Whatever is wrong is downstream of everything the firmware
can report about itself, which is consistent with the DCLK_VOP0 lead: step [4]
switches VP0's clock to `clk_hdmiphy_pixel0`, and nothing in this firmware can
tell you what that clock is actually running at. Every register that can be read back was identical on
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

### PCIe: the link trains, the endpoint's config space does not answer

This entry used to say training fails and the bridge is never exposed. That is
no longer what happens. On the 2026-08-04 ROCK 4D run:

```
PCIe: LTSSM_STATUS=0x00030005
PCIe0: link up (LTSSM_STATUS=0x00030005) — waiting 1s for Gen switch...
PCIe0: post-settle LTSSM_STATUS=0x00000003
PCIe: Link up (x1, 2.5 GT/s)
PCIe0: endpoint CFG0 VID:DID = 0xFFFFFFFF
```

The link-up bits latch, then LTSSM collapses during the 1 s settle — the
`0x..000D -> 0x..0003` collapse `Rk3576PciHostBridgeLib` already documents and
deliberately tolerates rather than retrying. `PciBusDxe` then enumerates the
root port itself (`PPB @ [00|00|00]`, `1D87:3576`) and allocates its resources,
but finds nothing behind it, because config reads to the endpoint return all
ones.

Reaching LTSSM 0x00030005 needs a link partner, so a device is present and the
lanes do train. What is not working is config access to it. Worth looking at
next: whether the ATU is programmed for CFG space before that read, and whether
the endpoint is simply not ready that early — the read happens immediately
after the settle, before bus numbers are assigned.

Separately, `FdtPlatformDxe` cannot find the nodes it wants to fix up:

```
FdtPlatform: Couldn't locate FDT node path '/pcie@22000000'. Ret=FDT_ERR_NOTFOUND
```

The node exists, but at `/soc/pcie@22000000` — mainline `rk3576.dtsi` puts the
peripherals under a `soc { compatible = "simple-bus" }` node.
`FdtPlatformDxe.c:179,308` hardcodes four root-level paths, two of them for
SATA controllers RK3576 does not have. So the ComboPHY and PCIe fixups have
never run on this SoC, and the DT handed to Linux goes out unmodified. This
predates the restructure.

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

## I2C: a wrong address that never ran

Before the restructure, `I2cDxe` was compiled against RK3588's `Soc.h`, so its
`I2C_BASE(id)` macro produced `0xFD880000` / `0xFEA90000` / `0xFEC80000`. On
RK3576 those are ordinary DRAM. It now produces RK3576's addresses, checked
against the RK3576 TRM V1.2 (I2C0 `0x27300000`, I2C1 `0x2AC40000`, I2C2
`0x2AC50000`, through I2C8 `0x2ACB0000`), as are the eleven register offsets
the driver uses.

**None of this executes.** `Silicon/Rockchip/FvMainModules.fdf.inc:124` gates
both I2C drivers behind `!if $(RK_I2C_ENABLE) == TRUE`, and `RK_I2C_ENABLE` is
defined nowhere in this repository — it never was, before the restructure
either. So `I2cDxe` has never been in either board's firmware volume, and the
wrong addresses were never programmed.

Worth knowing for two reasons. First, this is the same trap as the DSC
inheritance: a flag that reads as a feature switch while nothing defines it,
silently removing a subsystem. Second, if anyone ever sets `RK_I2C_ENABLE`,
the driver will now reach real controllers instead of DRAM — the bus and slave
lists in `RK3576Base.dsc.inc` (`{1,2}`, `{0x23,0x51}`) match the DTS, and both
boards implement `I2cIomux` with RK3576 pin functions, but whether the I2C
PCLKs are ungated at UEFI entry has never been tested.

## Verify on the next hardware session

The restructure removed 41 dead `#else` arms from the display stack and
changed which `Soc.h` those modules see. Most of their `.text` came out
byte-identical, but the display path is the one subsystem with an open bug,
so:

**Take the HDMI cold-boot ratio again, at least 8 boots, capture card and a
pixel verdict.** It should still be about 2 in 8. If it is 0 in 8, the
restructure broke something and `legacy/v0.1` is the comparison.

Nothing else in the restructure changes runtime behaviour on either board.
