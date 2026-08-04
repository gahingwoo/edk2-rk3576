#!/usr/bin/env python3
"""
Patch a generated Conf/tools_def.txt so EDK2 builds with a modern GCC.

This used to be a heredoc in the middle of the build script.  It is a
separate file now so it can be read, diffed and tested on its own.

Three fixes, all of them long-standing EDK2-vs-new-GCC problems:

  1. GCC 10-13 turned a batch of warnings into errors.  The rockchip and
     TianoCore sources trip several of them; suppress those rather than
     patching hundreds of call sites.

  2. -flto has to go, from CC_FLAGS and from the DLINK_FLAGS group.  With
     LTO on, GCC emits implicit memcpy/memset calls, and EDK2 links no libc.

  3. -fstack-protector has to go: EDK2 links no libssp either.

Usage:  tools_def_patch.py <path-to-tools_def.txt> <gcc-major-version>
"""
import re
import sys

GCC_WARNING_SUPPRESSIONS = (
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

LTO_DLINK_RE = re.compile(
    r'-flto\s+-Os\s+-L\S+\s+-llto-aarch64\s+'
    r'-Wl,-plugin-opt=-pass-through=-llto-aarch64\s+-Wno-lto-type-mismatch'
)
LTO_CC_RE = re.compile(r'\s+-flto\b')
STACK_PROTECTOR_RE = re.compile(r'-fstack-protector(?!-off|-ra)')
STACK_GUARD_RE = re.compile(r'-mstack-protector-guard=\S+')

RELEASE_CC_FLAGS_RE = re.compile(r'^RELEASE_GCC[0-9A-Z]*_AARCH64_CC_FLAGS\s*=')
DLINK_FLAGS_RE = re.compile(r'^(RELEASE|DEBUG)_GCC[0-9A-Z]*_AARCH64_DLINK_FLAGS\s*=')


def is_aarch64_cc_flags(line):
    return '_AARCH64_CC_FLAGS' in line and 'DEFINE' not in line and '=' in line


def patch(lines):
    warnings = no_lto = no_ssp = 0
    out = []

    for line in lines:
        s = line.rstrip('\r\n')

        if RELEASE_CC_FLAGS_RE.match(s):
            if GCC_WARNING_SUPPRESSIONS.split()[0] not in s:
                s += GCC_WARNING_SUPPRESSIONS
                warnings += 1

        if is_aarch64_cc_flags(s) and LTO_CC_RE.search(s):
            s = LTO_CC_RE.sub('', s)
            no_lto += 1

        if DLINK_FLAGS_RE.match(s) and LTO_DLINK_RE.search(s):
            s = LTO_DLINK_RE.sub('-Os', s)

        if is_aarch64_cc_flags(s):
            before = s
            s = STACK_PROTECTOR_RE.sub('-fno-stack-protector', s)
            s = STACK_GUARD_RE.sub('', s)
            if '-fno-stack-protector' not in s:
                s = s.rstrip() + ' -fno-stack-protector'
            s = re.sub(r'  +', ' ', s).rstrip()
            if s != before:
                no_ssp += 1

        out.append(s + '\n')

    return out, warnings, no_lto, no_ssp


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())

    path, gcc_major = sys.argv[1], sys.argv[2]

    with open(path) as f:
        lines = f.readlines()

    out, warnings, no_lto, no_ssp = patch(lines)

    with open(path, 'w') as f:
        f.writelines(out)

    print("  GCC%s warnings: %d | no-LTO: %d | no-SSP: %d lines patched"
          % (gcc_major, warnings, no_lto, no_ssp))


if __name__ == '__main__':
    main()
