# Shared helpers for the scripts in this directory.  Sourced, not executed.

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YLW=$'\033[1;33m'; CYN=$'\033[0;36m'; NC=$'\033[0m'

info() { echo "${GRN}[INFO]${NC}  $*"; }
warn() { echo "${YLW}[WARN]${NC}  $*" >&2; }
ok()   { echo "${GRN}[ OK ]${NC}  $*"; }
die()  { echo "${RED}[FAIL]${NC} $*" >&2; exit 1; }
step() { echo; echo "${CYN}== $* ==${NC}"; }

# Where the pinned TianoCore checkout lives.  It is a submodule; an override
# is honoured so a shared checkout can be reused without re-cloning 400 MB.
edk2_dir() {
    echo "${EDK2_DIR:-$ROOT/third_party/edk2}"
}

# The other upstream trees the build needs on PACKAGES_PATH.
deps_packages_path() {
    local base="${DEPS_DIR:-$ROOT/third_party}"
    echo "$base/edk2-non-osi:$base/edk2-platforms:$base/edk2-rockchip-non-osi"
}
