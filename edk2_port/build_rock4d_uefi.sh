#!/usr/bin/env bash
# =============================================================================
# build_rock4d_uefi.sh  —  EDK2 UEFI firmware for Radxa ROCK 4D (RK3576)
#
# 启动链:
#   BootROM → SPL (idbloader) → TF-A BL31 → EDK2 UEFI (BL33) → OS
#
# SPI NOR 16MB 布局:
#   0x000000 - 0x007FFF : GPT 分区表
#   0x008000 - 0x05FFFF : idblock (DDR init blob + SPL)
#   0x060000 - 末尾     : FIT image (.itb)  ← SPL 硬编码从这里加载
#                          ├─ ATF BL31 (EL3 安全服务)  → 加载到 0x40000
#                          ├─ EDK2 BL33_AP_UEFI.Fv     → 加载到 0x200000
#                          └─ DTB                       → 传给 UEFI
#
# 打包方式:
#   EDK2 编译产物 BL33_AP_UEFI.Fv + bl31.elf (提取 PT_LOAD 段) + DTB
#   → mkimage 打成 FIT image (.itb)
#   → 写入 SPI NOR @ 0x60000 (384KB) offset
#
# Handles ALL known pitfalls:
#   AArch64 host   → rebuilds BaseTools natively
#   GCC 10-13      → suppresses new -Werror warnings
#   GCC 13 LTO     → removes -flto from DLINK_FLAGS
#   GCC 13 ssp     → replaces -fstack-protector with -fno-stack-protector
#   /Scripts/*.lds → creates symlink automatically
#   Submodules     → inits brotli/mipisyst/openssl/libfdt automatically
#
# Usage:
#   bash build_rock4d_uefi.sh           # full build + package
#   bash build_rock4d_uefi.sh clean     # wipe Build/ and Conf/
#   bash build_rock4d_uefi.sh package   # re-package only (no recompile)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2="$SCRIPT_DIR/edk2"
WSDIR="$SCRIPT_DIR/edk2-rockchip"
MISC="$SCRIPT_DIR/misc"
LOG="$WSDIR/build_rock4d.log"
BINDIR="$(dirname "$SCRIPT_DIR")/binaries"
OUT_IMG="$(dirname "$SCRIPT_DIR")/rock4d-spi-edk2.img"

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; CYN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GRN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YLW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
step()  { echo -e "\n${CYN}══ $* ══${NC}"; }

# ── Subcommands ───────────────────────────────────────────────────────────────
if [ "${1:-}" = "clean" ]; then
    info "Cleaning..."
    rm -rf "$WSDIR/Build" "$WSDIR/Conf" "$WSDIR/build_rock4d.log"
    info "Done. Re-run without arguments to rebuild."
    exit 0
fi

JUST_PACKAGE=0
[ "${1:-}" = "package" ] && JUST_PACKAGE=1

# ── STEP 1: Detect environment ────────────────────────────────────────────────
step "1/6  Detect Environment"

HOST_ARCH=$(uname -m)
GCC_FULL=$(gcc --version | head -1)
GCC_MAJOR=$(echo "$GCC_FULL" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)
GCC_MAJOR=${GCC_MAJOR:-10}

info "Host: $HOST_ARCH  |  GCC: $GCC_FULL  |  Jobs: $(nproc)"

if [ "$HOST_ARCH" = "aarch64" ]; then
    export GCC5_AARCH64_PREFIX=""
    export GCC_AARCH64_PREFIX=""
    info "Mode: Native AArch64"
elif [ "$HOST_ARCH" = "x86_64" ]; then
    command -v aarch64-linux-gnu-gcc &>/dev/null || \
        error "需要 aarch64-linux-gnu-gcc\n  sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    export GCC5_AARCH64_PREFIX="aarch64-linux-gnu-"
    export GCC_AARCH64_PREFIX="aarch64-linux-gnu-"
    info "Mode: Cross-compile → AArch64"
else
    error "不支持的主机架构: $HOST_ARCH"
fi

# ── STEP 2: Dependencies ──────────────────────────────────────────────────────
step "2/6  Dependencies"

NEED=()
for p in build-essential uuid-dev nasm acpica-tools \
         python3 python3-pyelftools python3-dev swig \
         libssl-dev libgnutls28-dev; do
    dpkg -s "$p" &>/dev/null || NEED+=("$p")
done
[ "${#NEED[@]}" -gt 0 ] && {
    warn "Installing: ${NEED[*]}"
    sudo apt-get update -qq && sudo apt-get install -y "${NEED[@]}" 2>&1 | tail -3
}
[ "$HOST_ARCH" = "x86_64" ] && {
    dpkg -s gcc-aarch64-linux-gnu &>/dev/null || \
        sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu 2>&1 | tail -2
}
info "依赖 OK ✓"

# ── STEP 3: BaseTools ─────────────────────────────────────────────────────────
step "3/6  Build BaseTools (native $HOST_ARCH)"

BT_BIN="$EDK2/BaseTools/Source/C/bin/GenFw"
BT_GENSEC="$EDK2/BaseTools/Source/C/bin/GenSec"
NEED_BT=1

if [ -f "$BT_BIN" ] && [ -f "$BT_GENSEC" ]; then
    T_ARCH=$(file "$BT_BIN" | grep -oE 'x86-64|ARM aarch64|ARM' | head -1 | sed 's/ARM aarch64/aarch64/')
    if { [ "$HOST_ARCH" = "aarch64" ] && [ "$T_ARCH" = "aarch64" ]; } || \
       { [ "$HOST_ARCH" = "x86_64"  ] && [ "$T_ARCH" = "x86-64"  ]; }; then
        info "BaseTools 已存在 ($T_ARCH) ✓"
        NEED_BT=0
    else
        warn "BaseTools 是 $T_ARCH 但主机是 $HOST_ARCH，重新编译"
    fi
fi

[ $NEED_BT -eq 1 ] && {
    info "编译 BaseTools (用 HOST 的 gcc，不是交叉编译器)..."
    make -C "$EDK2/BaseTools" CC=gcc CXX=g++ BUILD_CC=gcc BUILD_CXX=g++ \
         -j$(nproc) 2>&1 | grep -v "^make\[" | tail -5 || true
    [ -f "$BT_GENSEC" ] || error "BaseTools 编译失败：GenSec 未生成"
    info "BaseTools 编译完成 ✓"
}

# ── STEP 4: Git submodules ────────────────────────────────────────────────────
step "4/6  Init Submodules"

cd "$EDK2"
for sub in \
    "MdeModulePkg/Library/BrotliCustomDecompressLib/brotli" \
    "MdePkg/Library/MipiSysTLib/mipisyst" \
    "MdePkg/Library/BaseFdtLib/libfdt" \
    "CryptoPkg/Library/OpensslLib/openssl" \
    "CryptoPkg/Library/MbedTlsLib/mbedtls"; do
    [ -d "$EDK2/$sub" ] && [ -z "$(ls -A "$EDK2/$sub" 2>/dev/null)" ] && {
        info "Init: $sub"
        git submodule update --init --depth=1 "$sub" 2>&1 | tail -2 || warn "Init $sub 失败，可能影响编译"
    }
done
cd "$SCRIPT_DIR"
info "Submodules OK ✓"

# Apply edk2 patches (idempotent via --check)
PATCH_DIR="$WSDIR/edk2-patches"
if [ -d "$PATCH_DIR" ]; then
    for p in "$PATCH_DIR"/*.patch; do
        [ -f "$p" ] || continue
        cd "$EDK2"
        git apply --check "$p" 2>/dev/null && {
            git apply "$p" && info "Applied patch: $(basename $p)" || warn "Patch failed: $(basename $p)"
        } || info "Patch already applied: $(basename $p)"
        cd "$SCRIPT_DIR"
    done
fi

# ── STEP 5: Patch tools_def.txt ───────────────────────────────────────────────
step "5/6  Patch tools_def.txt (GCC $GCC_MAJOR 兼容)"

mkdir -p "$WSDIR/Conf"

# 每次从 template 重新生成，确保幂等性
cp "$EDK2/BaseTools/Conf/tools_def.template" "$WSDIR/Conf/tools_def.txt"
cp "$EDK2/BaseTools/Conf/build_rule.template" "$WSDIR/Conf/build_rule.txt"
cp "$EDK2/BaseTools/Conf/target.template"     "$WSDIR/Conf/target.txt"

python3 - "$WSDIR/Conf/tools_def.txt" "$GCC_MAJOR" << 'PYEOF'
import re, sys

path, gcc_major = sys.argv[1], int(sys.argv[2])

with open(path) as f:
    lines = f.readlines()

# Patch 1: GCC 10-13 新增 warning→error 抑制
GCC13 = (
    " -Wno-implicit-function-declaration"
    " -Wno-error=implicit-function-declaration"
    " -Wno-error=incompatible-pointer-types"
    " -Wno-error=int-conversion"
    " -Wno-stringop-overflow"
    " -Wno-dangling-pointer"
    " -Wno-use-after-free"
    " -Wno-array-bounds"
    " -Wno-maybe-uninitialized"
    " -Wno-error=maybe-uninitialized"
    " -Wno-uninitialized"
)

# Patch 2: 移除 CC_FLAGS 和 DLINK_FLAGS 里的 -flto（GCC LTO 和 EDK2 freestanding 不兼容）
# CC_FLAGS 里 -flto 会让编译器插入隐式 memcpy/memset，而 EDK2 不链接 libc
LTO_DLINK_RE = re.compile(
    r'-flto\s+-Os\s+-L\S+\s+-llto-aarch64\s+-Wl,-plugin-opt=-pass-through=-llto-aarch64\s+-Wno-lto-type-mismatch'
)
LTO_CC_RE = re.compile(r'\s+-flto\b')

# Patch 3: 移除所有 -fstack-protector（EDK2 不链接 libssp）
SP_RE    = re.compile(r'-fstack-protector(?!-off|-ra)')
SGUARD_RE = re.compile(r'-mstack-protector-guard=\S+')

p1 = p2 = p3 = 0
out = []
for line in lines:
    s = line.rstrip('\r\n')

    # Patch 1: RELEASE AARCH64 CC_FLAGS — 添加 warning 抑制
    if re.match(r'^RELEASE_GCC[0-9A-Z]*_AARCH64_CC_FLAGS\s*=', s):
        if GCC13.split()[0] not in s:
            s += GCC13; p1 += 1

    # Patch 2a: CC_FLAGS 里的 -flto（RELEASE 和 DEBUG 都处理）
    if '_AARCH64_CC_FLAGS' in s and 'DEFINE' not in s and '=' in s:
        if LTO_CC_RE.search(s):
            s = LTO_CC_RE.sub('', s); p2 += 1

    # Patch 2b: DLINK_FLAGS 里的完整 LTO 链接选项组
    if re.match(r'^(RELEASE|DEBUG)_GCC[0-9A-Z]*_AARCH64_DLINK_FLAGS\s*=', s):
        if LTO_DLINK_RE.search(s):
            s = LTO_DLINK_RE.sub('-Os', s)

    # Patch 3: 所有 AARCH64 CC_FLAGS（DEBUG + RELEASE）移除 stack-protector
    if '_AARCH64_CC_FLAGS' in s and 'DEFINE' not in s and '=' in s:
        b = s
        s = SP_RE.sub('-fno-stack-protector', s)
        s = SGUARD_RE.sub('', s)
        if '-fno-stack-protector' not in s:
            s = s.rstrip() + ' -fno-stack-protector'
        s = re.sub(r'  +', ' ', s).rstrip()
        if s != b: p3 += 1

    out.append(s + '\n')

with open(path, 'w') as f:
    f.writelines(out)
print(f"  GCC{gcc_major} warnings: {p1} | no-LTO: {p2} | no-SSP: {p3} lines patched")
PYEOF

info "tools_def.txt 打补丁完成 ✓"

# ── STEP 6: Build environment + compile ──────────────────────────────────────
step "6/6  Compile + Package"

export EDK_TOOLS_PATH="$EDK2/BaseTools"
export WORKSPACE="$WSDIR"
export CONF_PATH="$WSDIR/Conf"
# WSDIR = edk2-rk3588 wrapper; inner edk2-rockchip has Platform/ + Silicon/
RK_INNER="$WSDIR/edk2-rockchip"
[ ! -d "$RK_INNER/Platform" ] && RK_INNER="$WSDIR"
export PACKAGES_PATH="$RK_INNER:$WSDIR:$EDK2:$SCRIPT_DIR/edk2-non-osi:$WSDIR/edk2-platforms:$WSDIR/edk2-rockchip-non-osi"
export PYTHONPATH="$EDK2/BaseTools/Source/Python"
export PATH="$EDK2/BaseTools/BinWrappers/PosixLike:$EDK2/BaseTools/Source/C/bin:$PATH"

# /Scripts/GccBase.lds 是 EDK2 生成的 GNUmakefile 里硬编码的绝对路径
[ ! -f /Scripts/GccBase.lds ] && {
    sudo mkdir -p /Scripts && sudo ln -sfn "$EDK2/BaseTools/Scripts/GccBase.lds" /Scripts/GccBase.lds \
    || { mkdir -p /Scripts && ln -sfn "$EDK2/BaseTools/Scripts/GccBase.lds" /Scripts/GccBase.lds; } \
    || warn "/Scripts/GccBase.lds 创建失败，Shell.efi 链接可能报错"
}

set +eu
source "$EDK2/edksetup.sh" BaseTools 2>/dev/null || true
set -eu
info "GenFw: $(which GenFw)  [$(file $(which GenFw) | grep -oE 'x86-64|aarch64')]"

if [ $JUST_PACKAGE -eq 0 ]; then
    warn "开始 EDK2 编译，预计 15-30 分钟..."
    set +e
    build -s -n "$(nproc)" -a AARCH64 -t GCC \
          -p "Platform/Radxa/ROCK4D/ROCK4D.dsc" \
          -b RELEASE -D FIRMWARE_VER="rk3576-rock4d-v0.1" \
          2>&1 | tee "$LOG"
    BUILD_EXIT="${PIPESTATUS[0]}"
    set -e
    [ "$BUILD_EXIT" -ne 0 ] && \
        error "编译失败 (exit $BUILD_EXIT)\n前几条错误:\n$(grep -E 'error [0-9A-F]+:|\.c:[0-9]+:.*error:' "$LOG" 2>/dev/null | head -8)\n\n完整日志: $LOG"
    info "EDK2 编译成功 ✓"
fi

# ── 打包成 SPI NOR 镜像 ───────────────────────────────────────────────────────

# EDK2 编译产物
FV="$WSDIR/Build/ROCK4D/RELEASE_GCC/FV/BL33_AP_UEFI.Fv"
[ -f "$FV" ] || error "找不到 BL33_AP_UEFI.Fv，编译是否成功？\n期望路径: $FV"

FV_KB=$(( $(stat -c%s "$FV" 2>/dev/null || stat -f%z "$FV") / 1024 ))
info "EDK2 固件: BL33_AP_UEFI.Fv  (${FV_KB}KB) ✓"

# ── 打包工作目录 ──────────────────────────────────────────────────────────────
PKGDIR=$(mktemp -d)
trap "rm -rf $PKGDIR" EXIT
info "打包工作目录: $PKGDIR"

# 1. 提取 BL31 各 PT_LOAD 段（extractbl31.py 输出 bl31_0x*.bin）
BL31=""
if [ -f "$BINDIR/bl31.elf" ]; then
    BL31="$BINDIR/bl31.elf"
    info "使用开源 TF-A BL31: $BL31"
elif [ -f "$BINDIR/rk3576_bl31_vendor.elf" ]; then
    BL31="$BINDIR/rk3576_bl31_vendor.elf"
    info "使用 vendor BL31: $BL31"
else
    # 从 rkbin 查找
    RKBIN_BL31=$(find "$MISC/rkbin" -name "rk3576_bl31*.elf" 2>/dev/null | head -1)
    [ -n "$RKBIN_BL31" ] && BL31="$RKBIN_BL31" && info "使用 rkbin BL31: $BL31"
fi
[ -z "$BL31" ] && error "找不到 BL31 (bl31.elf)，无法打包 FIT image"

cd "$PKGDIR"
python3 "$MISC/extractbl31.py" "$BL31"
cd "$SCRIPT_DIR"
# extractbl31.py 在 PKGDIR 内输出 bl31_0x*.bin（已 cd 进去执行）
info "BL31 段提取完成: $(ls $PKGDIR/bl31_0x*.bin 2>/dev/null | xargs -I{} basename {} | tr '\n' ' ')"

# 2. 复制 BL33 (EDK2)、DTB、stub bl32
cp "$FV" "$PKGDIR/BL33_AP_UEFI.Fv"
touch "$PKGDIR/bl32.bin"  # OP-TEE stub（不使用，但 ITS 引用了它）

# DTB：使用 SPL 传给 UEFI 用的 board DTB
DTB_SRC="$MISC/rk3576_spl.dtb"
[ ! -f "$DTB_SRC" ] && DTB_SRC="$SCRIPT_DIR/devicetree/vendor/rk3576-rock-4d.dtb"
[ -f "$DTB_SRC" ] && cp "$DTB_SRC" "$PKGDIR/rk3576-rock-4d.dtb" || {
    warn "找不到 DTB，创建空占位"
    touch "$PKGDIR/rk3576-rock-4d.dtb"
}

# 3. 生成 FIT image (.itb) — 使用 gen_fit_its.py 动态生成
DEVICE="rk3576-rock-4d"
BL31_ENTRY=$(python3 -c "
from elftools.elf.elffile import ELFFile
with open('$BL31','rb') as f:
    print(hex(ELFFile(f).header.e_entry))
")
info "BL31 entry: $BL31_ENTRY"
python3 "$MISC/gen_fit_its.py" "$PKGDIR" "$DEVICE" "$BL31_ENTRY"
[ -f "$PKGDIR/${DEVICE}_EFI.its" ] || error "gen_fit_its.py 未生成 ITS 文件"

# 找 mkimage —— 必须 ≥ 2020.07 (支持 -B 对齐 FIT header)
# Bundled misc/tools 的 mkimage 是 2017.09，会静默忽略 -B 导致 SPL hash 全错。
SYS_MKIMAGE=$(command -v mkimage 2>/dev/null || true)
if [ -n "$SYS_MKIMAGE" ]; then
    SYS_VER=$("$SYS_MKIMAGE" -V 2>&1 | grep -oE '[0-9]{4}\.[0-9]+' | head -1)
    SYS_MAJOR=${SYS_VER%.*}
    if [ -n "$SYS_MAJOR" ] && [ "$SYS_MAJOR" -ge 2020 ]; then
        MKIMAGE="$SYS_MKIMAGE"
        info "使用系统 mkimage: $MKIMAGE ($SYS_VER, 支持 -B)"
    fi
fi
if [ -z "${MKIMAGE:-}" ] || ! "$MKIMAGE" -E -B 0x1000 -V &>/dev/null; then
    MACHINE=$(uname -m)
    MKIMAGE="$MISC/tools/$MACHINE/mkimage"
    [ ! -x "$MKIMAGE" ] && MKIMAGE="$SYS_MKIMAGE"
    [ -z "$MKIMAGE" ] && {
        warn "mkimage 未找到，尝试安装 u-boot-tools..."
        sudo apt-get install -y u-boot-tools 2>/dev/null | tail -2
        MKIMAGE=$(command -v mkimage)
    }
fi
[ -z "$MKIMAGE" ] && error "找不到 mkimage，无法生成 FIT image"
info "mkimage: $MKIMAGE"

# 生成 FIT image
#   -E:        external data — payloads 放在 FIT header 之后；SPL 只 malloc
#              FIT header (~几 KB)，按 data-offset 把每段直接读到 load addr。
#   -B 0x1000: FIT header pad 到 4KB，让 SPL 算出的 ext_base 与 mkimage 写
#              出位置一致（无 -B 时 totalsize 不对齐导致 SPL 偏移错位）。
#   注：mkimage 2025.10 的 -p 是 "static position"（绝对偏移）而非旧版的
#   "padding alignment"，会让所有 external data 挤在固定位置导致重叠失败。
#   段间对齐由 BL31 segment 提取阶段 padding 到 16 字节解决（见 extractbl31.py）。
cd "$PKGDIR"
"$MKIMAGE" -E -B 0x1000 -f "${DEVICE}_EFI.its" "${DEVICE}_EFI.itb" 2>&1 | tail -5
cd "$SCRIPT_DIR"
[ -f "$PKGDIR/${DEVICE}_EFI.itb" ] || error "FIT image 生成失败"
ITB_KB=$(( $(stat -c%s "$PKGDIR/${DEVICE}_EFI.itb" 2>/dev/null || stat -f%z "$PKGDIR/${DEVICE}_EFI.itb") / 1024 ))
info "FIT image: ${DEVICE}_EFI.itb  (${ITB_KB}KB) ✓"

# ── 打包成 SPI NOR 镜像 (FIT 直通方案) ──────────────────────────────────────
#
# 启动链：BootROM -> SPL (idblock_mainline.bin) -> FIT
#           FIT 由 SPL 解析: 加载 BL31 各段 + EDK2 BL33 + DTB
#         BL31 (EL3) -> EDK2 (EL2, BL33) -> OS
#
# SPI NOR 16MB 布局：
#   0x000000  GPT (32KB, 可选)
#   0x008000  idblock_mainline.bin (DDR init blob + SPL, ~190KB)
#   0x060000  FIT image (BL31 + EDK2 + DTB) ← SPL 硬编码加载偏移
# ──────────────────────────────────────────────────────────────────────────────

info "组装 SPI NOR 镜像 (16MB) - FIT 直通方案..."
dd if=/dev/zero bs=1M count=16 of="$OUT_IMG" status=none

# 1. 写入 GPT (可选)
GPT="$MISC/rk3576_spi_nor_gpt.img"
[ ! -f "$GPT" ] && GPT="$MISC/rk3588_spi_nor_gpt.img"
if [ -f "$GPT" ]; then
    dd if="$GPT" of="$OUT_IMG" conv=notrunc status=none
    info "GPT: 写入 0x000000 ✓"
fi

# 2. 写入 idblock (DDR init + 极简 SPL)
#    必须使用 mainline SPL — 它会从 SPI 0x60000 读取并解析 FIT，加载
#    BL31/BL33/DTB 后跳转 BL31 入口；BL31 再交接给 EDK2 (BL33)。
IDBLOCK=""
for cand in "$BINDIR/idblock_mainline.bin" \
            "$WSDIR/idblock-mainline.bin" \
            "$WSDIR/idblock.bin"; do
    [ -f "$cand" ] && IDBLOCK="$cand" && break
done
[ -z "$IDBLOCK" ] && error "找不到 idblock (mainline SPL)，无法构建 FIT 启动链"
dd if="$IDBLOCK" of="$OUT_IMG" bs=1K seek=32 conv=notrunc status=none
IDB_KB=$(( $(stat -c%s "$IDBLOCK") / 1024 ))
info "idblock: 写入 0x008000 ✓ (${IDB_KB}KB, $IDBLOCK)"

# 3. 写入 FIT image (BL31 + EDK2 + DTB) — SPL 硬编码偏移 0x60000
#    上限 = NV-Var 区起点 0x7C0000 (RK3576.fdf) - 0x60000 = 7.375MB
FIT="$PKGDIR/${DEVICE}_EFI.itb"
[ -f "$FIT" ] || error "找不到 FIT image: $FIT"
FIT_SIZE=$(stat -c%s "$FIT")
FIT_MAX=$((0x7C0000 - 0x60000))
[ "$FIT_SIZE" -gt "$FIT_MAX" ] && \
    error "FIT image 过大 (${FIT_SIZE} > ${FIT_MAX})，缩减 EDK2 或调整布局"
dd if="$FIT" of="$OUT_IMG" bs=1K seek=384 conv=notrunc status=none
info "FIT image: 写入 0x060000 ✓ ($((FIT_SIZE/1024))KB / 上限 $((FIT_MAX/1024))KB)"

echo ""
echo -e "${GRN}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GRN}║          ROCK 4D EDK2 UEFI 固件打包完成！               ║${NC}"
echo -e "${GRN}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  镜像: ${CYN}$OUT_IMG${NC}  ($(du -h "$OUT_IMG" | cut -f1))"
echo ""
echo -e "  SPI NOR FIT 直通布局:"
echo    "    0x000000  GPT 分区表"
echo    "    0x008000  idblock (DDR init + mainline SPL)"
echo    "    0x060000  FIT image (BL31 + EDK2 BL33 + DTB)"
echo ""
echo -e "  启动链:"
echo    "    BootROM → SPL → FIT 解析 → BL31 (EL3) → EDK2 BL33 (EL2) → TianoCore"
echo ""
echo -e "${CYN}刷写 SPI NOR (MaskROM 模式，USB-C 连接):${NC}"
echo "  rkdeveloptool db binaries/rk3576_ddr.bin"
echo "  rkdeveloptool wl 0 rock4d-spi-edk2.img"
echo "  rkdeveloptool rd"
echo ""
echo -e "${CYN}串口 (1500000 8N1) 预期输出:${NC}"
echo "  U-Boot SPL ... → INFO: BL31 ... → TianoCore EDK2 / UEFI Interactive Shell"
