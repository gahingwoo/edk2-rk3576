#!/usr/bin/env python3
"""
patch_tools_def.py — 修复 edk2 Conf/tools_def.txt 的 GCC 10-13 兼容性问题

用法:
    python3 patch_tools_def.py /path/to/edk2-rockchip/Conf/tools_def.txt

每次从 template 重建 tools_def.txt 后运行此脚本。
"""
import re, sys, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "edk2-rockchip/Conf/tools_def.txt"

with open(path) as f:
    lines = f.readlines()

GCC13 = (
    " -Wno-implicit-function-declaration"
    " -Wno-error=implicit-function-declaration"
    " -Wno-error=incompatible-pointer-types"
    " -Wno-error=int-conversion"
    " -Wno-stringop-overflow"
    " -Wno-dangling-pointer"
    " -Wno-use-after-free"
    " -Wno-array-bounds"
)

LTO_RE    = re.compile(r'-flto\s+-Os\s+-L\S+\s+-llto-aarch64\s+-Wl,-plugin-opt=-pass-through=-llto-aarch64\s+-Wno-lto-type-mismatch')
SP_RE     = re.compile(r'-fstack-protector(?!-off|-ra)')
SGUARD_RE = re.compile(r'-mstack-protector-guard=\S+')

p1 = p2 = p3 = 0
out = []
for line in lines:
    s = line.rstrip('\r\n')
    if re.match(r'^RELEASE_GCC[0-9A-Z]*_AARCH64_CC_FLAGS\s*=', s):
        if GCC13.split()[0] not in s:
            s += GCC13; p1 += 1
    if re.match(r'^RELEASE_GCC[0-9A-Z]*_AARCH64_DLINK_FLAGS\s*=', s):
        if LTO_RE.search(s):
            s = LTO_RE.sub('-Os', s); p2 += 1
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
print(f"Patched: GCC13 warnings={p1}, no-LTO={p2}, no-SSP={p3}")
