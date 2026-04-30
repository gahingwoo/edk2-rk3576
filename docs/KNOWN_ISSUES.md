# Known issues

## Display output (HDMI / DSI) — not implemented

The RK3588 display drivers in upstream `edk2-rockchip` are SoC-specific and
cannot be reused as-is for RK3576:

* `DwHdmiQpLib.c` hard-codes `RK3588_SYS_GRF_BASE`, `RK3588_VO1_GRF_BASE`,
  and the `RK3588_*_MASK` constants.
* `Vop2Dxe.c` only carries `mVpDataRK3588`, `mVpPlaneMaskRK3588`, and
  `RK3588_*_PD_*` defines.
* `LcdGraphicsOutputDxe.inf` references only `gRK3588TokenSpaceGuid`.
* No simple-framebuffer / `GenericGop` fallback exists in `edk2-rockchip`.

The RK3576 HDMI controller lives at `0x27DA0000`. U-Boot proper does have
a working HDMI init path (`HdmiTxIomux()` with the appropriate CRU clock
ungating), which can serve as a reference for an RK3576-specific port.

**Workaround:** boot UEFI over UART only.

## ACPI tables are stubs

Tables under `ROCK4D/AcpiTables/` are minimal placeholders. The firmware
boots in **FDT mode** by default, which is the supported configuration.

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
