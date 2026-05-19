# Known issues

## Display (HDMI) — visual artifacts

HDMI display is **working**: VOP2 and the DW HDMI QP TX PHY are fully
initialised, EDID is read via DDC, and the GOP is installed at the monitor's
native resolution (2560×1440@60 QHD on the test monitor). The UEFI front
page, HII menus, and GRUB all render directly on the monitor.

Two minor visual issues remain under investigation:

### Horizontal stripe artifacts

Faint horizontal bands overlay the image. Root cause: the ROPLL dynamic
calculation path (`HdptxPhyClkPllCalc`) produces incorrect SDM register
values for BitRate=2415000 kbps (2560×1440@60 EDID pixel clock × 10).

The `RationalBestApproximation` call computes K=6 / Lc=13 as a ratio of
`(Fref×Mdiv − Fvco) / (Sdc/16)`, but the hardware SDM evaluates
`correction = Fref × (Sdm_Num / Sdm_Deno)`. Writing Sdm_Num=6,
Sdm_Deno=13 to the hardware yields:

```
Fvco = 24000 × (101 − 6/13) ≈ 2 412 923 kHz   ← wrong (2077 kHz low)
```

The correct table entry uses Sdm_Num=3, Sdm_Deno=8:

```
Fvco = 24000 × (101 − 3/8) = 2 415 000 kHz    ← exact
```

**Fix:** `ROPLL_TMDS_CONFIG` now has a `{ 2415000, … }` entry with the
correct Sdm_Num=3 / Sdm_Deno=8, replacing the previous 2417000 placeholder
(which was never matched). The dynamic path is no longer taken for this
bit-rate. Rebuilt image pending hardware verification.

### Slight horizontal shift

The framebuffer content appears shifted a few pixels to the right relative
to the monitor's expected active-image position. Under investigation — may
be a downstream effect of the ROPLL clock error above (monitor auto-adjust
compensating for wrong pixel clock); expected to resolve after the ROPLL fix.

## PCIe — link training fails in UEFI

`Rk3576PciHostBridgeLib` brings the controller out of reset and DBI is
reachable (`VID:DID = 0x1D87:0x3576`, the RK3576 PCIe vendor/device pair),
but `PciHostBridgeDxe` times out waiting for L0:

```
PCIe0: Set link speed Gen1 x1 (initial training)...
PCIe0: Assert PERST#...
PCIe0: Enable LTSSM...
PCIe0: Deassert PERST# (100ms done)...
PCIe0: Waiting for Gen1 link up (up to 1s)...
PCIe: LTSSM_STATUS=0x0003000D
PCIe: LTSSM_STATUS=0x00000003
PCIe: Link up timeout!
Error: Image at … start failed: Unsupported
```

* `0x0003000D` = LTSSM in `Polling.Active`/`Polling.Configuration`
* `0x00000003` = LTSSM dropped back to `Polling.LinkUp`/`Polling.Active`

The same slot + endpoint works under Linux, so the PHY is reaching the link
partner — what we are still missing on the EDK2 path is most likely:

* Combo PHY0 lane-0 RX equalisation / signal-detect calibration
* The `wait-for-link` poll window in `Rk3576PciHostBridgeLib` may need to be
  extended past 1 s with retries
* RC interrupts for hot-plug-style retraining

The driver does not crash the boot — it just refuses to expose the bridge
to `PciBusDxe`, so no NVMe / WiFi attached to PCIe is usable from UEFI.

## ACPI tables are stubs

Tables under `RK3576/AcpiTables/` are minimal placeholders. The firmware
boots in **FDT mode** by default, which is the supported configuration. The
`AcpiPlatformDxe` from RK3588 is intentionally rejected at runtime:

```
AcpiPlatform: ACPI support is disabled by the settings.
```

## EDK2 commit must be pinned

EDK2 must be checked out at commit `46548b1adac82211d8d11da12dd914f41e7aa775`.
Newer EDK2 cores have BaseTools / interface changes that break the rockchip
overlay.

## BaseTools must be rebuilt on AArch64 hosts

The prebuilt `BaseTools/Source/C/bin/` checked into upstream EDK2 is a
**x86_64** binary. On AArch64 hosts you must rebuild it:

```bash
make -C edk2/BaseTools CC=gcc CXX=g++ -j$(nproc)
```

`build_rock4d_uefi.sh` does this automatically.

## `Trim` path resolution

`BinWrappers/PosixLike` must be **first** on `PATH` — using a symlink
elsewhere breaks because the wrapper resolves siblings via `BASH_SOURCE`
relative paths.

## `/Scripts/GccBase.lds`

The EDK2 GCC linker script references `/Scripts/GccBase.lds` as an
**absolute path**. The build script symlinks it:

```bash
sudo ln -sfn $PWD/edk2/BaseTools/Scripts/GccBase.lds /Scripts/GccBase.lds
```

## `FtwPei: Both working and spare block are invalid`

Cosmetic. Variable services run as `VariableStubDxe` (in-RAM, volatile) on
this port, so the FTW headers are intentionally not initialised:

```
FtwPei: Work block header valid bit check error
FtwPei: Both working and spare block are invalid.
Firmware Volume for Variable Store is corrupted
…
VariableStubDxe: in-RAM Variable services installed (volatile). Success
```

UEFI variables therefore do **not persist** across reboots in this build.

## SDMMC0 identification spam when no card is present

Harmless:

```
DwSdExecTrb: Command error. CmdIndex=1, IntStatus=104
EmmcIdentification: Executing Cmd1 fails with Device Error
…
```

The DwMmcHc driver fans out CMD1/CMD8/CMD55/CMD7/CMD3 to detect a card on
SDMMC0; with no card inserted these fail and the controller is dropped.

## Duplicate overlay copies

`edk2_port/RK3576/` and `edk2_port/ROCK4D/` at the top level are early
scaffolding mirrors. **The build does not consume them.** Edits there have
no effect. The active overlay is:

* `edk2_port/edk2-rockchip/edk2-rockchip/Platform/Radxa/ROCK4D/`
* `edk2_port/edk2-rockchip/edk2-rockchip/Silicon/Rockchip/RK3576/`

Verify a driver actually got built with:

```bash
grep <DriverName> edk2_port/edk2-rockchip/Build/ROCK4D/RELEASE_GCC/FV/Guid.xref
```

## Disabling `RK_RTC8563_ENABLE`

When disabling the PCF8563 RTC, also wrap
`gPcf8563RealTimeClockLibTokenSpaceGuid.PcdI2cSlaveAddress` in `!if` blocks
in the DSC, otherwise the build fails with `F001` (unused PCD).
