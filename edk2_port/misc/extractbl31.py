#!/usr/bin/env python3
#
# Script to extract PT_LOAD segments from bl31.elf. Derived from
# Rockchip's make_fit_atf.py

import os
import sys

# pip3 install pyelftools / apt install python3-pyelftools
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.segments import Segment, InterpSegment, NoteSegment

ELF_SEG_P_TYPE='p_type'
ELF_SEG_P_PADDR='p_paddr'

def generate_atf_binary(bl31_file_name):
    with open(bl31_file_name, "rb") as bl31_file:
        bl31 = ELFFile(bl31_file)

        num = bl31.num_segments()
        for i in range(num):
            seg = bl31.get_segment(i)
            if ('PT_LOAD' == seg.__getitem__(ELF_SEG_P_TYPE)):
                paddr = seg.__getitem__(ELF_SEG_P_PADDR)
                file_name = 'bl31_0x%08x.bin' % paddr
                data = seg.data()
                # Pad to 16-byte boundary. RK3576 SPL's SFC DMA requires
                # transfer length to be aligned (>=4 bytes; round to 16 for
                # cache-line safety). Without padding, the last partial word
                # in RAM is not actually written by DMA, and the FIT SHA256
                # over the unpadded source will not match the loaded RAM
                # contents → "Bad hash value for atf-3 image" failure.
                # The padding lies inside BL31's BSS region (memsz > filesz)
                # which BL31 itself zero-clears on entry — fully safe.
                pad = (16 - (len(data) % 16)) % 16
                if pad:
                    data += b'\x00' * pad
                with open(file_name, "wb") as atf:
                    atf.write(data)

generate_atf_binary(sys.argv[1])
