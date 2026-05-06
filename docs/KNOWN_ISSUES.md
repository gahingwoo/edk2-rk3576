# Known issues

## Display (HDMI) — no signal from UEFI

`RK3576SimpleFbDxe` allocates a 1920×1080 framebuffer in DRAM and installs a
proper EFI `GraphicsOutputProtocol`, and it programs `VOP2 WIN0_YRGB_MST` to
that buffer:

```
RK3576SimpleFb: GOP installed — 1920x1080 FB @ 0xEEB30000 stride 7680
```

**However**, the HDMI TX PHY and VOP2 timing engine are **not** initialised
by EDK2, and U-Boot SPL on RK3576 does not bring them up either. The result
is that nothing leaves the SoC pin until the Linux DRM driver takes over the
display controller — at which point GRUB (via `efifb` on the surviving GOP)
and Fedora both render normally.

Concretely missing / TODO:

* RK3576-specific HDMI TX PHY init (bases at `0x27DA0000` /
  HDPTXPHY GRF at `0x26032000`)
* RK3576 VOP2 mode-set (the existing `Vop2Dxe.c` is RK3588-only — different
  GRF map and plane-mask layout)
* EDID readout (DDC over I²C5 or via SCDC)

The RK3588 display drivers in `edk2-rockchip` cannot be reused as-is:

* `DwHdmiQpLib.c` hard-codes `RK3588_SYS_GRF_BASE`, `RK3588_VO1_GRF_BASE`,
  `RK3588_*_MASK`.
* `Vop2Dxe.c` only carries `mVpDataRK3588`, `mVpPlaneMaskRK3588`,
  `RK3588_*_PD_*`.
* `LcdGraphicsOutputDxe.inf` references only `gRK3588TokenSpaceGuid`.

**Workaround:** boot via UART; let GRUB or Linux bring up the panel.

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
