# Local patches to the EDK2 core

`third_party/edk2/` is an upstream [tianocore/edk2](https://github.com/tianocore/edk2)
submodule, pinned by `scripts/setup-host.sh` to edk2-stable202602. An edit made
directly inside it shows up only as a modified submodule, is lost the moment
the checkout is reset, and leaves no record of what it was. Two of the four
changes below are functional fixes, not debug scaffolding. If they silently
revert, the firmware still compiles and still produces an image; it just
behaves differently, with nothing to point at. So they live here.

They were first generated against tianocore/edk2 `46548b1` and apply cleanly
to edk2-stable202602, which is what CI builds against.

## Applying

`scripts/setup-host.sh` applies the required set, so the normal path needs no
manual step. By hand:

```sh
patches/apply.sh third_party/edk2                 # required patches only
patches/apply.sh --with-debug third_party/edk2    # + UART tracing
patches/apply.sh --check third_party/edk2         # report, change nothing
```

`setup-host.sh` adds `--with-debug` when `EDK2_CORE_DEBUG_PATCHES=1`.
Re-running is safe: an already applied patch is detected and skipped.

## Required

### `0001-PeilessSec-flush-FD-before-DecompressFirstFv.patch`

`ArmConfigureMmu()` enables the D-cache without flushing it first. On RK3576 the
FD is loaded into DRAM by TF-A as BL33, and BL31 has already pulled that same
region into cache while verifying the FIT hash. Those stale lines shadow the
DRAM contents when `DecompressFirstFv()` reads the LZMA section header, and
decompression fails.

Adds a `WriteBackInvalidateDataCacheRange()` (DC CIVAC) over `PcdFdBaseAddress`
… `+PcdFdSize` immediately before the decompress call.

Applies to any platform whose FD is placed in DRAM by an earlier boot stage, so
it is a reasonable upstream candidate.

### `0002-DefaultExceptionHandler-print-ESR-FAR-unconditionally.patch`

The handler's register dump goes through `DEBUG()`, which is filtered by
`PcdDebugPrintErrorLevel`. On a RELEASE build the four values that actually
identify a fault — ESR, FAR, SP, LR — are the ones least likely to survive that
filter, which leaves an unattributable "Synchronous Exception at 0x…" and
nothing else.

Adds a `SerialPortWrite()` of ESR / FAR / SP / LR / EC alongside the existing
banner. This is what makes the crash-triage workflow (decode DFSC, `addr2line`
against `DxeCore.debug`) possible on a shipping image.

## Optional (`--with-debug`)

### `0003-DEBUG-PeilessSec-UART-checkpoints.patch`

Emits `[A]` … `[K]` on the UART between the SEC phases. Locates a silent
pre-DXE hang without a DEBUG build — which matters here because a DEBUG build
does not fit the FV.

### `0004-DEBUG-PrePiLib-dump-HOB-list-before-DxeCore.patch`

Dumps the DxeCore load address and the complete HOB list (resource descriptors,
FV, memory allocations, handoff) just before the handoff. This is the direct way
to answer "is region X actually reserved" — see the RK3576 memory-map notes in
`Silicon/Rockchip/RK3576/Library/PlatformLib/Rk3576Mem.c`.

Both are noisy by design; keep them off for release images and for any
measurement where UART latency could matter.

## Adding a patch

Do not edit `third_party/edk2/` and stop there. Generate the patch too:

```sh
cd third_party/edk2
git diff -- path/File.c > ../../patches/000N-what-it-does.patch
```

Name it `NNNN-DEBUG-...` if it is tracing rather than a fix. That prefix is
what `apply.sh` keys on to decide whether it is opt-in.
