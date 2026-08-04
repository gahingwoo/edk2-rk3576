#!/usr/bin/env bash
#
# One-time host setup: packages, the EDK2 checkout, BaseTools, and the one
# symlink EDK2 needs at an absolute path.
#
# Every `sudo` in this repository lives in this file, and nothing here runs
# as a side effect of building.  The predecessor script ran `apt-get install`
# and `ln -s /Scripts` in the middle of a rebuild, which is a surprising thing
# for a compile step to do.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/lib/common.sh"

EDK2_COMMIT=46548b1adac82211d8d11da12dd914f41e7aa775

step "1/5  Host packages"

PACKAGES=(build-essential uuid-dev nasm acpica-tools device-tree-compiler
          python3 python3-pyelftools python3-dev swig
          libssl-dev libgnutls28-dev u-boot-tools)
[ "$(uname -m)" = "x86_64" ] && PACKAGES+=(gcc-aarch64-linux-gnu g++-aarch64-linux-gnu)

MISSING=()
for p in "${PACKAGES[@]}"; do
    dpkg -s "$p" >/dev/null 2>&1 || MISSING+=("$p")
done

if [ "${#MISSING[@]}" -gt 0 ]; then
    warn "These packages are missing and will be installed with sudo:"
    printf '        %s\n' "${MISSING[@]}"
    read -r -p "  proceed? [y/N] " reply
    [[ "$reply" =~ ^[Yy] ]] || die "aborted -- install them yourself and re-run"
    sudo apt-get update -qq
    sudo apt-get install -y "${MISSING[@]}"
fi
ok "packages present"

step "2/5  EDK2 checkout (pinned)"

EDK2="$(edk2_dir)"
if [ ! -d "$EDK2/.git" ] && [ ! -f "$EDK2/edksetup.sh" ]; then
    info "cloning TianoCore edk2 into $EDK2"
    git -C "$ROOT" submodule update --init --depth=1 third_party/edk2 2>/dev/null \
        || git clone https://github.com/tianocore/edk2 "$EDK2"
fi
[ -f "$EDK2/edksetup.sh" ] || die "no EDK2 at $EDK2"

if [ -d "$EDK2/.git" ]; then
    HAVE="$(git -C "$EDK2" rev-parse HEAD)"
    if [ "$HAVE" != "$EDK2_COMMIT" ]; then
        warn "EDK2 is at $HAVE, pinning to $EDK2_COMMIT"
        git -C "$EDK2" fetch --depth=1 origin "$EDK2_COMMIT" 2>/dev/null || git -C "$EDK2" fetch origin
        git -C "$EDK2" checkout -q "$EDK2_COMMIT"
    fi
    ok "EDK2 pinned at $EDK2_COMMIT"
else
    warn "EDK2 at $EDK2 is not a git checkout -- cannot verify it is $EDK2_COMMIT"
fi

# Newer EDK2 cores change BaseTools and library interfaces in ways the rockchip
# overlay does not follow, which is why the commit is pinned rather than tracked.

info "initialising the submodules EDK2 itself needs"
for sub in MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
           MdePkg/Library/MipiSysTLib/mipisyst \
           MdePkg/Library/BaseFdtLib/libfdt \
           CryptoPkg/Library/OpensslLib/openssl \
           CryptoPkg/Library/MbedTlsLib/mbedtls; do
    if [ -d "$EDK2/$sub" ] && [ -z "$(ls -A "$EDK2/$sub" 2>/dev/null)" ]; then
        git -C "$EDK2" submodule update --init --depth=1 "$sub" >/dev/null 2>&1 \
            || warn "could not init $sub"
    fi
done

step "3/5  EDK2 core patches"

# edk2/ is an upstream checkout, so edits made inside it are invisible to this
# repository and vanish on a re-clone.  Two of these are functional fixes (FD
# cache flush before decompression; ESR/FAR reported on RELEASE faults) --
# without them the build still produces an image, just a differently-behaving
# one.  Applying them here is what keeps a fresh checkout equivalent.
if [ -x "$ROOT/patches/apply.sh" ]; then
    EXTRA=""
    [ "${EDK2_CORE_DEBUG_PATCHES:-0}" = "1" ] && EXTRA="--with-debug"
    "$ROOT/patches/apply.sh" $EXTRA "$EDK2" \
        || die "EDK2 core patches did not apply -- refusing to leave an untraceable checkout"
    ok "core patches applied"
else
    warn "patches/apply.sh missing; EDK2 core is unpatched"
fi

step "4/5  BaseTools"

# The BaseTools binaries checked into upstream EDK2 are x86_64.  On an AArch64
# host they have to be rebuilt, with the HOST compiler -- not the cross one.
BT_GENSEC="$EDK2/BaseTools/Source/C/bin/GenSec"
NEED_BUILD=1
if [ -f "$BT_GENSEC" ]; then
    HAVE_ARCH="$(file -b "$BT_GENSEC" | grep -oE 'x86-64|ARM aarch64' | sed 's/ARM aarch64/aarch64/')"
    WANT_ARCH="$(uname -m | sed 's/x86_64/x86-64/')"
    [ "$HAVE_ARCH" = "$WANT_ARCH" ] && NEED_BUILD=0
    [ "$NEED_BUILD" = 1 ] && warn "BaseTools is $HAVE_ARCH but the host is $WANT_ARCH; rebuilding"
fi
if [ "$NEED_BUILD" = 1 ]; then
    make -C "$EDK2/BaseTools" CC=gcc CXX=g++ BUILD_CC=gcc BUILD_CXX=g++ -j"$(nproc)" >/dev/null
    [ -f "$BT_GENSEC" ] || die "BaseTools build produced no GenSec"
fi
ok "BaseTools ready ($(uname -m))"

step "5/5  /Scripts/GccBase.lds"

# The GNUmakefile EDK2 generates references this script by absolute path.
if [ ! -e /Scripts/GccBase.lds ]; then
    warn "creating /Scripts/GccBase.lds (needs sudo; EDK2 hardcodes this absolute path)"
    sudo mkdir -p /Scripts
    sudo ln -sfn "$EDK2/BaseTools/Scripts/GccBase.lds" /Scripts/GccBase.lds
fi
ok "/Scripts/GccBase.lds present"

echo
ok "host ready -- next: scripts/build.sh <board>"
