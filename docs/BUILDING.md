# Building

```bash
git clone --recurse-submodules https://github.com/gahingwoo/edk2-rk3576
cd edk2-rk3576

scripts/setup-host.sh          # once: packages, EDK2, dependency trees, BaseTools
scripts/build.sh   cm5io       # compile
scripts/package.sh cm5io       # -> out/CM5IO/CM5IO-sdcard.img
```

Boards live in
[`boards/`](https://github.com/gahingwoo/edk2-rk3576/tree/main/boards):
`cm5io`, `rock4d`.

`scripts/build.sh <board> clean` wipes `Build/` and `Conf/` first.

CI runs these three scripts for both boards on every push, after fetching
the device-tree include mirror described below. The workflow is
[`.github/workflows/build.yml`](https://github.com/gahingwoo/edk2-rk3576/blob/main/.github/workflows/build.yml).

## What each script does

The three are deliberately separate. A compile should not install packages or
create files in `/`.

| Script | Does | Needs sudo |
|---|---|---|
| `setup-host.sh` | apt packages, EDK2 pinned to edk2-stable202602, the three dependency trees, core patches, BaseTools, `/Scripts/GccBase.lds` | yes, and it asks first |
| `build.sh` | regenerates `Conf/`, patches `tools_def.txt`, runs `build` | no |
| `package.sh` | extracts BL31 segments, builds the FIT, writes the flash images | no |

## Dependencies

| Tree | Where it comes from |
|---|---|
| `third_party/edk2` | Submodule, pinned by `setup-host.sh` to edk2-stable202602 (`b7a715f`). |
| `edk2-non-osi` | [tianocore/edk2-non-osi](https://github.com/tianocore/edk2-non-osi) |
| `edk2-platforms` | [tianocore/edk2-platforms](https://github.com/tianocore/edk2-platforms) |
| `edk2-rockchip-non-osi` | A directory inside [edk2-porting/edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588), sparse-checked out. Its AMD GOP drivers end up in the image, so it is not optional. |

`setup-host.sh` clones the last three into `third_party/`. Set `DEPS_DIR` to
keep them somewhere else; `build.sh` reads the same variable.

### EDK2 core patches

Our patches to the EDK2 core live in
[`patches/`](https://github.com/gahingwoo/edk2-rk3576/tree/main/patches) and
`setup-host.sh` applies them. Two are functional fixes: flushing the FD before
decompression, and printing ESR/FAR on RELEASE-build faults. The other two are
UART tracing and are applied only with `EDK2_CORE_DEBUG_PATCHES=1`.

Without the functional two the build still produces an image, just one that
behaves differently, which is why they are versioned here rather than edited
into the checkout. Applying them modifies the submodule, so a built image
reports its version with a `-dirty` suffix.

### The device-tree include mirror

Compiling the board DTS needs the upstream kernel's `dt-bindings` headers and
Rockchip DTS sources at `devicetree/mainline/upstream/`. `setup-host.sh` does
not fetch them. The recipe CI uses:

```bash
git clone --depth=1 --filter=blob:none --sparse \
    https://github.com/torvalds/linux /tmp/linux
git -C /tmp/linux sparse-checkout set \
    include/dt-bindings arch/arm64/boot/dts/rockchip
mkdir -p devicetree/mainline/upstream/src/arm64
ln -sfn /tmp/linux/include devicetree/mainline/upstream/include
ln -sfn /tmp/linux/arch/arm64/boot/dts/rockchip \
        devicetree/mainline/upstream/src/arm64/rockchip
```

`RockchipPkg.dec` lists both directories in `[Includes]`, so the build fails
early and clearly if they are missing.

## Do not trust the exit code alone

`build.sh` greps its own log for compiler errors as well as checking the exit
status. The script this replaced could exit 0 with a failed compile, which is
how a stale image got flashed more than once. If you invoke `build` by hand,
grep the log.

## Host notes

* **AArch64 hosts**: the BaseTools binaries in upstream EDK2 are x86_64.
  `setup-host.sh` detects the mismatch and rebuilds them with the host
  compiler.
* **GCC 10–13**: several warnings became errors, and `-flto` and
  `-fstack-protector` both break a freestanding EDK2 link.
  `scripts/lib/tools_def_patch.py` handles all three, regenerating
  `Conf/tools_def.txt` from the template each build so it stays idempotent.
* **`PATH`**: `BinWrappers/PosixLike` must come first. The wrappers find their
  siblings through `BASH_SOURCE`-relative paths, so reaching them via a
  symlink elsewhere breaks `Trim`.
* **Clock skew**: if the host clock is behind the file timestamps, `make`
  skips rebuilds it should do and still exits 0. Check the build stamp in the
  image, or `touch` what you changed.
