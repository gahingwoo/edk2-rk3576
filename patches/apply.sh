#!/usr/bin/env bash
#
# Apply this project's local modifications to the vendored EDK2 core tree.
#
# The edk2/ checkout is gitignored (see the repo .gitignore) because it is a
# multi-gigabyte upstream tree.  That means any edit made directly inside it is
# invisible to git and is lost the moment the tree is re-cloned or wiped.  Two
# of the changes below are not debug scaffolding but functional fixes, so
# losing them produces a firmware that builds and boots differently with no
# diagnostic.  Keeping them here, as patches, is what makes the build
# reproducible.
#
# Usage:
#   apply.sh [--with-debug] [--check] [EDK2_ROOT]
#
#   --with-debug   also apply the optional 0003/0004 UART-tracing patches
#   --check        report status only; apply nothing (exit 1 if any required
#                  patch is missing)
#   EDK2_ROOT      defaults to <repo>/edk2_port/edk2
#
# Re-running is safe: a patch that is already applied is detected and skipped.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

set -euo pipefail

PATCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2_ROOT="${PATCH_DIR}/../edk2"
WITH_DEBUG=0
CHECK_ONLY=0

# The upstream tianocore/edk2 commit these patches were generated against; it
# is the submodule pointer of edk2-porting/edk2-rk3588 at the time of writing.
# Recorded so a future context mismatch has an obvious first thing to check.
EXPECT_EDK2_SHA="46548b1adac82211d8d11da12dd914f41e7aa775"

while [ $# -gt 0 ]; do
  case "$1" in
    --with-debug) WITH_DEBUG=1 ;;
    --check)      CHECK_ONLY=1 ;;
    -h|--help)    sed -n '2,26p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)            EDK2_ROOT="$1" ;;
  esac
  shift
done

if [ ! -d "${EDK2_ROOT}/ArmPlatformPkg" ]; then
  echo "error: ${EDK2_ROOT} does not look like an EDK2 tree" >&2
  exit 1
fi

echo "EDK2 core patches"
echo "  tree     : ${EDK2_ROOT}"
echo "  expected : tianocore/edk2 @ ${EXPECT_EDK2_SHA}"
echo

missing=0

for patch in "${PATCH_DIR}"/[0-9][0-9][0-9][0-9]-*.patch; do
  name="$(basename "${patch}")"

  # Files named ...-DEBUG-... are tracing aids, not fixes.  They are useful
  # when chasing a silent hang and pure noise otherwise, so they are opt-in.
  optional=0
  case "${name}" in *-DEBUG-*) optional=1 ;; esac

  if [ "${optional}" = "1" ] && [ "${WITH_DEBUG}" = "0" ]; then
    printf '  %-64s skipped (debug)\n' "${name}"
    continue
  fi

  # Already applied?  A cleanly reverse-applying patch is one that is present.
  if patch -p1 -d "${EDK2_ROOT}" --dry-run --reverse --force -s <"${patch}" >/dev/null 2>&1; then
    printf '  %-64s already applied\n' "${name}"
    continue
  fi

  if [ "${CHECK_ONLY}" = "1" ]; then
    printf '  %-64s NOT APPLIED\n' "${name}"
    [ "${optional}" = "0" ] && missing=1
    continue
  fi

  if ! patch -p1 -d "${EDK2_ROOT}" --forward --no-backup-if-mismatch -s <"${patch}"; then
    echo "error: failed to apply ${name}" >&2
    echo "       the vendored edk2 tree may not be at ${EXPECT_EDK2_SHA}" >&2
    exit 1
  fi
  printf '  %-64s applied\n' "${name}"
done

if [ "${missing}" = "1" ]; then
  echo
  echo "error: one or more REQUIRED patches are not applied to ${EDK2_ROOT}" >&2
  echo "       run: $0" >&2
  exit 1
fi

echo
echo "EDK2 core tree is up to date."
