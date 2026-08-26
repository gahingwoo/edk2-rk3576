# Building

```bash
git clone --recurse-submodules https://github.com/gahingwoo/edk2-rk3576
cd edk2-rk3576

scripts/setup-host.sh          # once: packages, EDK2 checkout, BaseTools
scripts/build.sh   cm5io       # compile
scripts/package.sh cm5io       # -> out/CM5IO/CM5IO-sdcard.img
```

Boards live in [`boards/`](../boards/): `cm5io`, `rock4d`.

`scripts/build.sh <board> clean` wipes `Build/` and `Conf/` first.

## What each script does

The three are deliberately separate. A compile should not install packages or
create symlinks in `/`, which is what happened when all of this was one file.

| Script | Does | Needs sudo |
|---|---|---|
| `setup-host.sh` | apt packages, EDK2 clone pinned to `46548b1`, core patches, BaseTools, `/Scripts/GccBase.lds` | yes, and it asks first |
| `build.sh` | regenerates `Conf/`, patches `tools_def.txt`, runs `build` | no |
| `package.sh` | extracts BL31 segments, builds the FIT, writes the flash image | no |

## Dependencies

All four live in `third_party/` (gitignored). **`setup-host.sh` fetches only
the first.**

| Tree | Note |
|---|---|
| `edk2` | Submodule. Fetched and pinned by `setup-host.sh`. See the pin warning below. |
| `edk2-non-osi` | Realtek UNDI, `Emulator/X86EmulatorDxe`. **Not fetched** — and not tianocore's: that one has neither of those, so this is a fork. |
| `edk2-platforms` | **Not fetched.** |
| `edk2-rockchip-non-osi` | **Not fetched.** |

Without the last three, `build.sh` stops immediately with `error 000E: One
Path in PACKAGES_PATH doesn't exist`, which names none of them. `setup-host.sh`
now checks for them and says which are missing. Point `DEPS_DIR` at a
directory holding all three, or symlink them into `third_party/`.

### The pinned EDK2 commit cannot build this tree

`EDK2_COMMIT` is `46548b1adac82211d8d11da12dd914f41e7aa775` (2025-11-14).
`Silicon/Rockchip/RK3576/Drivers/FdtPlatformDxe` calls `FdtSetPropEmpty`,
`FdtOverlayApply`, `FdtOffsetDtStrings` and `FdtSizeDtStrings`, added upstream
in `74c508abe8` "MdePkg/BaseFdtLib: Add more wrappers" (2025-12-05) — newer
than the pin. Linking fails with undefined references.

Every image that has actually been tested was built against a local checkout
newer than the pin, via the `EDK2_DIR` override:

```bash
EDK2_DIR=/path/to/newer/edk2 scripts/build.sh cm5io
```

Moving the pin forward changes the EDK2 core for every module in the image,
so it needs its own hardware pass and should not ride along with a driver
change. Until then the override is the supported path.

Our three patches to the EDK2 core are versioned in [`patches/`](../patches/)
and applied by `setup-host.sh`. Two are functional (FD cache flush before
decompression; ESR/FAR on RELEASE faults); without them the build still
produces an image, just a differently-behaving one. `apply.sh` fails loudly
rather than leaving a checkout you cannot reason about.

### The device-tree include mirror is not fetched automatically

Compiling the board DTS needs the upstream kernel's `dt-bindings` headers at
`devicetree/mainline/upstream/` (about 90 MB). `setup-host.sh` does not fetch
this yet; point it at a kernel tree or a copy of the rockchip devicetree
mirror:

```bash
ln -s /path/to/rockchip-devicetree devicetree/mainline/upstream
```

`RockchipPkg.dec` lists `devicetree/mainline/upstream/include` and
`.../src/arm64/rockchip` in `[Includes]`, so the build fails early and clearly
if it is missing.

## Do not trust the exit code alone

`build.sh` greps its own log for compiler errors as well as checking the exit
status. The script this replaced could exit 0 with a failed compile, which is
how a stale image got flashed more than once. If you invoke `build` by hand,
grep the log.

## Host notes

* **AArch64 hosts**: the BaseTools binaries in upstream EDK2 are x86_64.
  `setup-host.sh` detects the mismatch and rebuilds them with the host
  compiler.
* **GCC 10-13**: several warnings became errors, and `-flto` and
  `-fstack-protector` both break a freestanding EDK2 link.
  `scripts/lib/tools_def_patch.py` handles all three, regenerating
  `Conf/tools_def.txt` from the template each build so it stays idempotent.
* **`PATH`**: `BinWrappers/PosixLike` must come first. The wrappers find their
  siblings through `BASH_SOURCE`-relative paths, so reaching them via a
  symlink elsewhere breaks `Trim`.
