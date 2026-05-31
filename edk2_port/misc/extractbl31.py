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
                # Pad to 512-byte boundary. Two requirements:
                # 1. MMC external FIT: data-offsets must be 512-byte aligned
                #    so the SPL can use mmc_load_image_raw_sector() to read
                #    each segment without sub-block addressing.
                # 2. SFC DMA: transfer length >= 4 bytes, cache-line safe.
                # 512 satisfies both. The padding lies inside BL31's BSS
                # region (memsz > filesz) which BL31 zero-clears on entry.
                pad = (512 - (len(data) % 512)) % 512
                if pad:
                    data += b'\x00' * pad
                with open(file_name, "wb") as atf:
                    atf.write(data)

generate_atf_binary(sys.argv[1])
