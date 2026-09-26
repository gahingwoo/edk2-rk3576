# Status

What actually works, what does not, and how confidently we know.

**Rule for this file: every claim carries its sample count.** "Display works"
is not a status; "15 of 15 cold boots reached the front page with
`POST_BUF_EMPTY=0`" is.
The predecessor of this file (`KNOWN_ISSUES.md`) drifted until four of its
entries were false, which is what a status file without evidence turns into.

Last updated: 2026-09-27. Everything claimed for CM5-IO below was measured on
2026-09-17 or later during a long hardware session unless it says otherwise;
entries carrying an earlier date have not been rechecked.
**ROCK 4D was benched again and reported on 2026-09-27**: stable HDMI signal,
and Fedora boots. That run's serial log was lost, so it carries no boot count
and no capture. Every other ROCK 4D entry below still predates the display
fixes and has not been rechecked.

---

## Read this before the next board session

**Record whether an SD card is in the slot, every time.** It changes the
outcome: 6 of 6 WinPE boots clean with the slot empty, every freeze on record
with a card in, and two boots where the firmware alone took three and a half
minutes instead of 31 seconds. Two conclusions drawn on 2026-09-20 — "the
driver-free image is 5 of 5 clean, so it is the drivers" and "that slow boot is
the deaf KD listener" — were both wrong for this reason, and both looked solid
when they were made. `75c0f32` is the fix; 4 card-present boots clean since,
which is not yet enough.

**A KD-enabled image is not a neutral instrument.** With `bootdebug on` and
nothing answering, the target retransmits every packet and the boot crawls —
3m26s to ExitBootServices against 31 s with a listener attached. Before
trusting any run made with such an image, confirm bytes are arriving: UEFI
console text appears within about ten seconds of power-on, well before Windows
starts.

**The colour-bar test pattern is off.** `RK_VOP2_TEST_PATTERN` in
`LcdGraphicsOutputDxe.c` is now `0`. It was `1`, which meant the GOP painted
SMPTE bars over the black fill the UEFI spec asks for on mode set, in every
image this project has ever shipped. Every "picture / no picture" verdict below
was a verdict about *bars*.

A working boot now shows the UEFI console instead. The capture-card pixel
verdict has to change with it: the test is "the frame is not uniformly black",
not "the frame matches the bar pattern". If bars are wanted for a particular
session, flip that one line back — but do not flip it and then compare the
result against a number measured with it the other way.

Two RK3588 drivers left the image: `DwDpLib` and `DwMipiDsi2Lib` were
dispatched unconditionally on both boards and are gated off now. Neither ever
ran (their entry points only register a protocol notify, and nothing installs
either protocol), so no measurement below is affected — but 88 KB of code
holding RK3588 addresses that are DRAM on RK3576 is no longer one DSI panel
driver away from firing.

---

## Verified on hardware

| Thing | Evidence |
|---|---|
| Boot chain to UEFI Shell | BootROM → SPL → BL31 → EDK2, serial 1500000 8N1. Reproduced on every boot of both boards. |
| **The restructured firmware itself** | ROCK 4D booted `rk3576-ROCK4D-legacy-v0.1-1-g652670d` to the front page with no ASSERT, no exception and no abort anywhere in the log. 1 boot. |
| **Display (CM5-IO)** | 2560x1440@60 over HDMI, clean picture, no stripes and no horizontal offset. 15 of 15 cold boots of the fixed code reached the UEFI front page with `POST_BUF_EMPTY=0`; the picture was confirmed by eye on the runs that were checked. Two fixes: `085b8a4` (an SError that killed every boot before the display path ran) and `255f867` (RK3576's three per-VP mixers left at reset — the stripes). |
| **Display (ROCK 4D)** | Stable HDMI signal, and Fedora boots. Reported 2026-09-27 from a bench run with this firmware. **The log was lost**, so there is no boot count, no capture and no identified fix — this is the weakest entry in this table and is here because it contradicts the HPD failure recorded below. |
| **eMMC (CM5-IO)** | HighSpeed, 52 MHz, 8-bit. Reads *and* writes: 512 B through 192 KB each written, read back and compared byte-identical, and `Found boot disk for NV storage!`. One SDHCI error event per boot, the benign CMD7 deselect. Was capped at 26 MHz legacy SDR until `54f46cc`; that cap is what broke every multi-block write. |
| **DRAM (CM5-IO)** | All 4 GB mapped — 4096 MB detected, 3838 MB usable on the front page, no external abort. `0003936`. 3 boots. |
| USB-A (CM5-IO) | Devices enumerate through the onboard 4-port hub. |
| GbE (CM5-IO) | Link and DHCP, after the crystal-less YT8531 fix (SoC 25 MHz via `clk_mac_refout`). |
| ACPI tables | `acpiview` in the UEFI Shell shows the expected tables. |
| NVRAM persistence (CM5-IO) | A variable set from the menu survives a reboot. Note this was measured when the NV store lived on SD; eMMC-backed NV only started working on 2026-09-17 (`54f46cc`). |
| `\EFI\BOOT` fallback boot | Reaches the OS loader. |
| **PCIe + NVMe (CM5-IO)** | The endpoint answers and the disk works. `PciBus: Discovered PPB @ [00\|00\|00] [1D87:3576]` then `Discovered PCI @ [01\|00\|00] [1E95:9100]`, and Fedora boots from the NVMe. Two fixes: PERST# was driven inverted and the LTSSM was enabled after PERST# was released instead of before (2026-09-17), then two MCFG bugs for Windows (2026-09-19). Seen on every boot since. |
| **NVMe under Windows** | `stornvme` and `disk` both Started, in a stock Microsoft ADK WinPE with no third-party drivers. The MCFG had folded the bus offset into the base — segment 1 landed on the USB controller — and declared 15 buses over a 1 MB aperture, eating the root bridge's own MEM32. `2026-09-19`. |
| **8 CPUs under Windows** | `wmic cpu get NumberOfCores,NumberOfLogicalProcessors` → `8 8`. Every MADT GICC carried CPU Interface Number 0, which is right for RK3588's GICv3 and wrong for RK3576's GICv2, where that field is the `GICD_ITARGETSR` bit index. `7b093e7`, 2026-09-19. |
| **CPU at 1608 MHz** | Up from 816 MHz, over SCMI, with the PMIC rails raised to 800 mV. The DSC said this SoC had no SCMI (mainline's DT says otherwise), the SCMI shared-memory page was not mapped — a synchronous abort at `FAR=0x4010F004` — and the clock IDs were RK3588's. 2026-09-19. |
| **eMMC probe, one round, no errors (CM5-IO)** | Was 8 SDHCI errors over 4 probe rounds and about five minutes per boot; now one round inside a single second with zero errors. The CRU rate was being changed underneath a running card clock with no relock afterwards. `cb31cb4`, verified 2026-09-20. |

## Not working

### ~~HDMI on ROCK 4D: HPD reads low, everything else succeeds~~ — no longer reproduces, 2026-09-27

**Resolved.** ROCK 4D was reported on 2026-09-27 as putting out a stable HDMI
signal with this firmware, so the failure below does not survive the display
fixes that landed after 2026-08-04. The measurement is kept because the
cable-fault reading it invited was wrong, and because nothing here identifies
which fix closed it.

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

### ~~CM5-IO HDMI is intermittent — 2 of 8 cold boots produced a picture~~ — SOLVED 2026-09-17

> **This is history, kept for the record of what was measured.** The 2-in-8
> figure is void twice over: the SARADC fix invalidated the sampling, and the
> two real bugs were found afterwards. See the display row in *Verified on
> hardware* above.
>
> The causes were not intermittency at all. `586af04` had deferred the VP0
> STANDBY clear, so every boot died with an SError *before* the display path
> ran — which also means the whole 2026-08-27 and 2026-08-30 audit had never
> executed on hardware. With that fixed the picture came up with black vertical
> stripes, which were RK3576's three per-VP mixers left at their reset values.
> `vop2_setup_alpha()` only programs mixers from `normalized_zpos >= 1`, so
> with a single plane mainline writes none of them and **no source comparison
> could have found it**; diffing the MMIO writes of a vendor binary that
> produced a clean picture did.

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

### What the vendor's HDMI driver does that ours did not

> **The title of this section used to read "Why HDMI is intermittent".** That
> framing is superseded — CM5-IO's display was not intermittent, it was two
> deterministic bugs (see above). What follows is still the most useful thing
> in this file: a working vendor binary's own account of the bring-up order,
> and it is what the mixer fix was eventually found by diffing against.

Found 2026-08-04 by reverse-engineering a working reference image
(`rock4d-sd-uefi（green hdmi）.img`, a Vendor-stack build that puts a stable
green screen out on ROCK 4D). Its DXE volume decompresses to a small custom
`Rk3576HdmiDxe`, and its trace strings are the whole recipe:

```
HDMI: init start (Build 16)
HDMI: clocks enabled
PD:VOP pwron  /  PD:VOP ok idle=%x       <- powers the VOP power domain
HDMI: CLKGATE63=0x%x                     <- programs CRU clock gates
HDMI: SOFTRST21=0x%x / SOFTRST22=0x%x    <- programs CRU soft resets
HDMI: resets deasserted
HDMI: VPLL locked in %d ms               <- configures V0PLL and waits for lock
HDMI: VOP2 configured
HDMI: PHY PLL ok  /  PHY lanes ok
HDMI: DCLK->PHY pixel clk
HDMI: VOP2 output enable
...
PD:VOP idle TO / PD:VOP ack TO / PD:VOP pwr TIMEOUT
```

This firmware does none of the first four.

| Step | Reference build | This firmware |
|---|---|---|
| VOP power domain on | yes, with three distinct timeouts | **no** |
| V0PLL configure + lock wait | yes | **no** |
| CRU `CLKGATE_CON63` | yes | no (one debug read in RK3576Dxe.c) |
| CRU `SOFTRST_CON21/22` | yes | **no** |
| DCLK mux to PHY pixel clock | yes | yes |

`Vop2PowerDomainOn()` in `Vop2Dxe.c:2103` begins `if (!PdData) return`, and
`Vop2Rk3576.c` defines no `PdData` at all — so on RK3576 the VOP power domain
is never touched. `V0PLL` exists only inside `Rk3576CruLib.c`, which nothing
reaches. There is no clock-gate or soft-reset programming.

**So the display comes up on whatever state U-Boot SPL happened to leave
behind.** When SPL left the VOP powered and clocked, there is a picture; when
it did not, there is not. That is the 2-in-8, and it explains why every
register this firmware can read back looks identical on both outcomes — the
difference was established before EDK2 started.

It is also exactly what `Vop2Dxe.c` already warned about, in a comment nobody
acted on:

> KNOWN RISK: if the SPL leaves DCLK_VOP0 sourced from a non-HDMI PLL, pixel
> clock accuracy at 4K@60 may suffer until CRU programming is implemented for
> RK3576.

The consequence is bigger than pixel-clock accuracy: without the power domain
and the PLL, there is no output at all.

**Three of those four were already present.** `HdmiTxIomux()` in each board's
`RockchipPlatformLib.c` already ungates `CLKGATE_CON(61)/(62)/(63)/(64)` and the
PMU1CRU HDPTX gates, and already deasserts `SOFTRST_CON(22)/(25)/(28)`. Both
boards do it, with the same bits. The clocks and resets were never the gap.

The gap was the power domain, and only that. Implemented 2026-08-04 as
`Rk3576DisplayPowerDomainsOn()` in `Vop2Rk3576.c`, called from `Vop2PreInit()`
before any VOP2, HDMI or HDPTX register is touched. It powers `PD_VOP` (the
VOP2) and `PD_VO0` (the DW-HDMI-QP controller and the HDPTX PHY GRF), using
the register map and sequence from mainline
`drivers/pmdomain/rockchip/pm-domains.c` — PMU at `0x27380000`, ungate, release
the power switch, poll `repair_status`, leave NIU idle, poll ack and idle,
re-gate. Not from the vendor binary: that build proved the step is necessary,
mainline is what says how to do it.

It is idempotent — a domain the SPL already powered is left alone, because
re-running the sequence would drop power under a VOP that may already be
scanning out.

V0PLL programming, which the reference build also does, was **not** added.
DCLK_VP0 is switched to `clk_hdmiphy_pixel0` here, which is the same parent
mainline selects for HDMI, so the video PLL is not in that path. Adding it
without a reason would be guessing.

**Untested on hardware.** The prediction is specific and falsifiable: HDMI
output should stop depending on what the SPL left behind, so the cold-boot
ratio should go from about 2-in-8 to consistent. If it does not, the power
domains were not the whole story, and the serial log will now say whether they
came up (`PD_VOP powering on` / `already on` / a timeout).

### ~~PCIe: the link trains, the endpoint's config space does not answer~~ — SOLVED 2026-09-17

**This entry is obsolete and is kept only because it was wrong for a month.**
The endpoint answers. `PciBus: Discovered PPB @ [00|00|00] [1D87:3576]`
followed by `Discovered PCI @ [01|00|00] [1E95:9100]`, on every boot since, and
Fedora boots from the NVMe behind it.

Two defects, neither of them the ATU theory this entry ended on:

* **PERST# was driven inverted.** The reset the endpoint needs to see was
  being asserted when it should have been released.
* **The LTSSM was enabled after PERST# was released**, rather than before. An
  endpoint brought out of reset with nothing training against it has nothing
  to answer.

Fixed 2026-09-17. The all-ones config read below was the endpoint being held
in reset, not a config-access problem.

The ROCK 4D capture that produced it, from 2026-08-04:

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

Reaching LTSSM 0x00030005 needs a link partner, so a device was present and
the lanes did train — that part was read correctly. The conclusion drawn from
it, that config access was the fault, was not.

**Neither PCIe fix has been checked on ROCK 4D.** The 2026-09-27 run covered
the display and a Fedora boot; which medium it booted from was not recorded, so
it is not evidence for PCIe.

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

### ~~Windows on Arm stops at ExitBootServices~~ — Setup boots, 2026-09-18

Windows 10 21H2 ARM64 Setup runs on CM5-IO:

![Windows Setup on CM5-IO](imgs/cm5io-windows.png)

Getting there took removing **one ACPI device**. Setup had been bugchecking
`ACPI_BIOS_ERROR`, and the cause was `Scmi.asl`, inherited whole from RK3588:
it drives SCMI by writing a doorbell register at a hardcoded `0xfec60030` and
polling a shared-memory mailbox. RK3576 has neither. `rk3576.dtsi` declares

    scmi: scmi { compatible = "arm,scmi-smc"; arm,smc-id = <0x82000010>; ... }

with no mailbox node anywhere — the transport is an SMC call, which ASL cannot
issue, and `0xfec60030` decodes to nothing on this SoC. The shared-memory base
was wrong too: `PcdRkMtlMailBoxBase` defaulted to RK3588's `0x0010f000`, where
RK3576 puts it at `0x4010f000` (`scmi-shmem@4010f000`). Nothing in the tree
called the device's methods either. Fixed by df974cb and c5b836b.

The shared-memory PCD matters beyond Windows: `ArmMtlLib` reads the same one,
so every RK3576 image before df974cb pointed it at an address that is neither
DRAM nor a peripheral.

**How it was found, since reading did not find it.** Every static table was
disassembled offline (`iasl -d` on the `.acpi` files the build drops) and read
field by field: FADT, MADT, GTDT, SPCR, DBG2, MCFG, PPTT all check out, and
three real defects fixed along the way — missing `_CCA` on the PL330s and the
UART (5f978a3), `_HID` **and** `_ADR` on PMC0/RTC0 with no `_CRS` at all
(91a1b2a), and an IORT declaring two ITS groups on a SoC whose MADT correctly
says GICv2 with no ITS (bc1a28b). **None of those three was the bugcheck.**
SCMI was invisible to that audit because its addresses are well-formed; they
just belong to another SoC. It took bisecting the DSDT down to 473 bytes.

### ~~Windows sees no NVMe and no eMMC~~ — NVMe solved 2026-09-19; eMMC is now a driver problem

**NVMe works.** `stornvme` and `disk` both reach Started, in a stock Microsoft
ADK WinPE carrying no third-party drivers at all. Two bugs in the MCFG this
firmware hands Windows:

* **The bus offset was folded into the base address.** `ConfigSpaces[].BaseAddress`
  had `PCIE_BUS_BASE_OFFSET` added to it, so segment 1 was pointed at the USB
  controller.
* **Each entry declared 15 buses over a 1 MB aperture.** `EndBusNumber` is now
  clamped to `PCIE_CFG_SIZE / SIZE_1MB`, one bus. The old range swallowed the
  root bridge's own MEM32 window.

The `PCIE_CFG_SIZE` note below turned out to be half of that second bug and is
kept for the record.

**The eMMC is no longer an ACPI question.** The inbox SDHCI driver binds via
`_CID PNP0D40`, starts, and never finds a card, because an SDHCI `SW_RST_ALL`
clears `EMMC_CTRL[0]` `CARD_IS_EMMC` and nothing inbox restores it. A miniport
lives in the `woa-rk3576` repo as `drivers/storage/rkemmc`; as of 2026-09-20 it
runs eMMC identification to CMD6 SWITCH and the card node appears, so this has
moved out of this repository. See `docs/STORAGE.md` there.

Two ACPI defects were found while checking whether RK3588 upstream had anything
to copy, both fixed here: the eMMC `_DSD` `compatible` property was a
three-element package where `_DSD` requires `{name, value}` (`b178307`), and
the `_DSM` clock table was still RK3588's — that SoC's parent is 1200 MHz and
RK3576's is 400 MHz, so the four fast entries were programming 66.7, 33.3, 50
and 16.7 MHz while reporting 200, 150, 100 and 50 (`63508ba`).

The original diagnosis, kept because the reasoning in it was sound and only the
conclusion was incomplete:

* **PCI0 is enabled.** `AcpiDsdtFixupStatus` disables a root only when its
  ComboPHY is not in PCIe mode, and logs when the patch fails. Across the
  whole capture the failures are 10× `Failed to patch \_SB.PCI1._STA` and
  **zero** for PCI0 — PCI1 correctly disabled (ComboPhy1 defaults to USB3),
  PCI0 never touched.
* **The OS is identified correctly.** 56× `ExitBootServices: Booting Windows
  OS`, so `AcpiFixupPcieEcam` takes the `NXPMX6` path and rewrites the FADT
  OEM ID for the Windows ECAM quirk.
* **Every PCIe address matches the DT**: dbi `0x22000000`, apb `0x2a200000`,
  config `0x20000000`, `bus-range = <0x0 0xf>`, io `0x20100000`, mem32
  `0x20200000`.

One defect found while reading, not yet fixed: `PCIE_CFG_SIZE` is 1 MiB, which
is exactly **one** bus of ECAM, but MCFG declares buses 1..15 against that
window — bus 2 would be computed at `0x20100000`, which is the I/O window. A
single endpoint on bus 1 never reaches it, so this is wrong rather than fatal.

That last paragraph was right and the measurement it asked for is what found
the two MCFG bugs. `SDC3` has no `_STA` and carries `_CID PNP0D40`, so it was
never being hidden.

Separately: RK3576 is ARMv8.0, so Windows 11 24H2 and later cannot boot on it
regardless — target 23H2 or Windows 10.

### An SD card in the slot makes Windows crawl or bugcheck

Found 2026-09-20, and it had been poisoning every intermittent result for days.
With the slot empty, **6 of 6** WinPE boots were clean. With a card in it,
every freeze on record, and two boots where BL31 to ExitBootServices alone took
three and a half minutes instead of the usual 31 seconds.

The mechanism, and the reason it is a firmware bug: with a card present the
firmware initialises the SD controller, and `DwMmcHcEnableInterrupt` leaves
`IDINTEN = ~0` while the controller runs with `DW_MMC_CTRL_INT_EN`, so its
interrupt output is live. **No SD or eMMC driver registered an ExitBootServices
callback** — only `StatusLedDxe`, `OhciDxe`, `FdtPlatformDxe` and
`DwcEqosSnpDxe` did — so the controller went to the OS still armed. The GSIV in
`Sdhc.asl` is level triggered and an IDMAC interrupt is reported in `IDSTS`
rather than `RINTSTS`, so an OS host driver servicing `MINTSTS` sees nothing to
do, declines the interrupt, and nothing clears the line. Starvation rather than
a hang, which is what "the spinner turns very slowly" had looked like all
along, and it fits the `DRIVER_PNP_WATCHDOG` bugchecks.

`75c0f32` quiesces both controllers at ExitBootServices. **4 clean card-present
boots since**, against roughly none before. That is not yet a sample worth
calling it solved on.

---

## Known-wrong things carried over deliberately

These are defects that were found during the restructure and **not** fixed in
it, because fixing them changes runtime behaviour and needs a hardware pass.
Each is isolated and commented at the point where it lives.

### The HDMI driver switches the VP0 pixel clock and stands VP0 down to do it

`Silicon/Rockchip/Library/DisplayLib/DwHdmiQpLib.c` reads `VP_DSP_CTRL`, sets
its STANDBY bit, gates `DCLK_VP0`, writes `CLKSEL_CON147` bit 11 to re-source
the clock, ungates, then writes `VP_DSP_CTRL` back. Three things are wrong
with that and none of them stop it working today:

* it is a VOP2 **read** after `Vop2Enable`, which this board has a recorded
  hardware result against (adding register dumps there killed the signal);
* it writes `VP_DSP_CTRL` twice from a value it read back, quietly undoing
  f780ef7's compute-once-write-once discipline for any field whose read-back
  differs from what `Vop2Rk3576.c` computed;
* the vendor firmware never writes `CLKSEL_CON147` at all — it sources
  `dclk_vp0_src` from the CRU and has no mux switch to get wrong.

The alternative — take the clock from VPLL through the CRU and delete the
mux/standby block outright — was specified on 2026-08-30 and **not
implemented**, because the display started working without it. It is the
right shape; it is not urgent.

### `CMU_CONFIG0` is written with bit positions that belong to `CMU_STATUS`

`DwHdmiQpLib.c` does `DwHdmiQpRegMod (Hdmi, 0, VIDQPCLK_OFF | LINKQPCLK_OFF,
CMU_CONFIG0)`. Those two names are `CMU_STATUS` fields in mainline, in the
vendor BSP header and in vendor U-Boot, and **no reference writes
`CMU_CONFIG0` at any point** — the only CMU access any of them makes is a
single read of `CMU_STATUS`. The upstream edk2-rk3588 file this was forked
from has no CMU access either; the write was invented locally. The RK3576 TRM
does not document the register.

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

## The 2026-08-27 audit: what changed, and what it is worth

Four agents read the display path against mainline 7.2-rc7 and the 6.1.115
vendor BSP; a fourth disassembled `dirty/rock4d-sd-uefi（green hdmi）.img`,
the vendor UEFI build that puts a stable picture out on ROCK 4D, and
recovered its actual MMIO write sequence. Commits `66e8b49`, `e994d1d`,
`0961057`.

**Every claim below is source-derived. The cold-boot ratio has not been
re-measured since 2026-08-04, so nothing here is known to help.**

### Found wrong, with two independent references agreeing

| What | Was | Is |
|---|---|---|
| Window -> video port routing | global `OVL_PORT_SEL` 0x608, which RK3576 does not map | per-window `<win>+0xF4[1:0]`, written on every enable |
| `OVL_LAYER_SEL` | one global register at 0x604 | per VP, `0x604 + vp*0x100`, seeded 0xffff |
| `BG_MIX_CTRL` | 0x6E0 + vp*4, value 38 | `0x670 + vp*0x100`, value 20 (win+layer_mix+hdr_mix) |
| VP0's layers | Cluster0, Cluster1, Esmart0, Esmart1 | Esmart0 alone (Esmart1 cannot feed VP0 on RK3576) |
| HDPTX INIT/CMN/LANE/APB resets | main CRU `SOFTRST_CON26`/`CON28` — the DDR and NPU reset registers | PMU1CRU `SOFTRST_CON00`/`CON01` |
| `SRST_HDMITX0_REF`, `_HDMITXHDP`, `_LINKSYM_HDMITXPHY0` | CON22/CON28/CON25 — also DDR and NPU | CON64 b9, PMU1CRU CON01 b13, CON75 b1 |
| `PLL_PCG_CLK_SEL` | 1 (the 10bpc value) | `(bpc-8)>>1` = 0 |
| `CLKGATE_CON(63)` | VO0 roots only | plus ACLK/HCLK/PCLK_HDCP0, which are in PD_VO0's clock list |
| `aclk_pre_auto_gating_en` | never cleared | cleared (RK3528/RK3562/RK3576-only, vendor-documented) |

The reset mistake is worth remembering as a pattern: mainline's reset IDs are
array indices, the vendor binding's are `(cru_sel<<16)|(reg*16+bit)`, and
this tree took mainline's index and applied the vendor's formula to it.
Anything else in this tree that derives a register from a reset ID is
suspect.

### What the working binary settles, and what it does not

It writes `ESMART0_PORT_SEL_IMD`, `DLY_NUM`, `ALPHA_MAP`, the AXI read IDs
and `LAYER_SEL = 0x0000FFF2` — confirming those registers are real and giving
their values. It also writes `CLKGATE_CON(63) = 0x700B0000`, matching the
HDCP0 finding.

It does **not** validate the reset addresses: it writes the same wrong ones
this tree had, plus three deasserts into DDR reset registers of its own. So
it shares this tree's ancestry. What it does prove is that a stable picture
is possible *without* the HDPTX resets ever firing — which means fixing them
is a correctness fix, not a known cure.

Two things it does that we deliberately do not: it programs V0PLL to 594 MHz
and points `dclk_vp0_src` at it (148.5 MHz) so VP0 has a real clock through
the whole configuration, switching to `clk_hdmiphy_pixel0` only at the end.
We instead keep VP0 in standby until after the mux switch (`586af04`), which
is mainline's shape. Both avoid scanning out on an unprogrammed clock; only
theirs is proven.

### Verify on the next hardware session

1. **The inherited-state dump comes first.** `Vop2PreInit` now logs fourteen
   words at `[RK3576-INHERIT]` before touching anything — PMU power/idle, the
   CRU mux and gate state, the PMU1CRU resets, the PHY reference-clock
   select. All outside the VOP2 window. Capture it on a boot that shows a
   picture and one that does not. Everything readable *inside* VOP2 has been
   identical on both outcomes for weeks; this is the state that can differ.
2. **Then the ratio, at least 8 cold boots, monitor and a pixel verdict.**
   Was 2 in 8. Three commits landed at once, so the ratio says whether the
   group helped, not which member did.
3. If it is **0 in 8**, this group made it worse and `586af04` is the
   comparison. The likeliest culprit is the resets: they now genuinely fire,
   and our release ordering is not mainline's byte for byte.

### Still not established

The **black vertical stripes** have no confirmed cause. The pre-dither
question is still unmeasured in either direction. The `bg_dly` correction is
the best candidate on file for the horizontal offset, and offsets and blank
lines are the same defect at different severities — but that is an argument,
not a measurement.
