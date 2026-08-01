# Local patches to the vendored EDK2 core

`edk2_port/edk2/` is an upstream [tianocore/edk2](https://github.com/tianocore/edk2)
checkout and is **gitignored** — it is far too large to track, and it is
described in the build instructions as "cloned at build time".

That combination is a trap: an edit made directly inside that tree is invisible
to `git status`, survives no re-clone, and leaves no trace when it disappears.
Two of the four changes below are functional fixes, not debug scaffolding. If
they silently revert, the firmware still compiles and still produces an image —
it just behaves differently, with nothing to point at. So they live here.

Generated against **tianocore/edk2 `46548b1adac82211d8d11da12dd914f41e7aa775`**,
which is the `edk2` submodule pointer of `edk2-porting/edk2-rk3588`.

## Applying

```sh
edk2_port/patches-edk2-core/apply.sh                 # required patches only
edk2_port/patches-edk2-core/apply.sh --with-debug    # + UART tracing
edk2_port/patches-edk2-core/apply.sh --check         # report, change nothing
```

`build_rock4d_uefi.sh` runs the required set automatically before every build,
so the normal path needs no manual step. Re-running is safe — an already
applied patch is detected and skipped.

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
`Silicon/Rockchip/RK3588/Library/PlatformLib/Rk3588Mem.c`.

Both are noisy by design; keep them off for release images and for any
measurement where UART latency could matter.

## Adding a patch

Do not edit `edk2_port/edk2/` and stop there. Generate the patch too:

```sh
cd edk2_port/edk2
diff -u <pristine>/path/File.c path/File.c > ../patches-edk2-core/000N-what-it-does.patch
# then fix the two header lines to a/path/File.c and b/path/File.c
```

Name it `NNNN-DEBUG-...` if it is tracing rather than a fix — that prefix is
what `apply.sh` keys on to decide whether it is opt-in.
