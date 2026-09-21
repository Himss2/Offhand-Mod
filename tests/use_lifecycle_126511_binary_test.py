#!/usr/bin/env python3
"""Verify the supplied 1.26.51.1 ELF, production guards and action identities.

No external packages; file RVAs are translated through ELF PT_LOAD segments.
This establishes ABI evidence, not successful native transactions/gameplay.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = 'b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4'

def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: right_use_126511_binary_test.py /path/to/libminecraftpe.so')
    data = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(data).hexdigest() == EXPECTED, 'wrong Minecraft binary'
    assert data[:6] == b'\x7fELF\x02\x01', 'expected little-endian ELF64'
    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    phsize, phnum, shsize, shnum = struct.unpack_from('<HHHH', data, 54)
    segments = [struct.unpack_from('<IIQQQQQQ', data, phoff+i*phsize) for i in range(phnum)]
    def at(rva, size):
        for kind, flags, offset, vaddr, paddr, filesz, memsz, align in segments:
            if kind == 1 and vaddr <= rva and rva+size <= vaddr+filesz:
                return data[offset+rva-vaddr:offset+rva-vaddr+size]
        raise AssertionError(f'unmapped RVA {rva:#x}')
    relocations = {}
    for i in range(shnum):
        section = struct.unpack_from('<IIQQQQIIQQ', data, shoff+i*shsize)
        if section[1] == 4: # SHT_RELA
            offset, size = section[4:6]
            for address, info, addend in struct.iter_unpack('<QQq', data[offset:offset+size]):
                if info == 1027: # R_AARCH64_RELATIVE, symbol zero
                    relocations[address] = addend
    src = (ROOT/'src/runtime/RightUseRouter.cpp').read_text()
    expected_functions = {
        'CompleteUsingItem': (0xF9E8094, 'fd 7b bb a9 fc 67 01 a9 f8 5f 02 a9 f6 57 03 a9'),
        'SetSelectedItem': (0xF9F7850, 'fd 7b bb a9 fc 67 01 a9 f8 5f 02 a9 f6 57 03 a9'),
        'StopUsingItem': (0xF9E86C0, 'fd 7b bb a9 fc 0b 00 f9 f8 5f 02 a9 f6 57 03 a9'),
        'HandTransaction': (0xF9E9DFC, 'ff 43 02 d1 fd 7b 05 a9 f7 33 00 f9 f6 57 07 a9'),
    }
    for name, (rva, fingerprint) in expected_functions.items():
        assert f'k{name}Rva = 0x{rva:X};' in src
        assert at(rva, 16) == bytes.fromhex(fingerprint), name
    for callsite, target in {
        0xF9E7450: 0xF9E8094, # use tick -> completion
        0xF9E83B4: 0xF6E4A64, # completion keeps native callback envelope
        0xFA05ED0: 0xFF9F520, # completion callback -> useTimeDepleted
        0xF8A3354: 0xF9E9DFC, # release -> native hand wrapper
        0xF9E9E9C: 0xF6E4A64,
        0xAAD03A4: 0xF9FC7C8, # LocalPlayer -> recording OFF setter
        0xF9FC86C: 0x1001EC24, # record InventoryAction
    }.items():
        word = int.from_bytes(at(callsite,4), 'little')
        assert word & 0xFC000000 == 0x94000000
        imm = word & 0x3FFFFFF
        if imm & 0x2000000: imm -= 0x4000000
        assert callsite + 4*imm == target, hex(callsite)
    # Player implementations all inherit the hooked MAIN setter, while OFF
    # dispatch must stay virtual to retain LocalPlayer/ServerPlayer behavior.
    for base, setter in [(0x12D08C38,0xAAD0360),(0x12F65BF8,0xEC4B7E0),(0x130343D0,0xF9FC7C8)]:
        assert relocations[base+0x268] == 0xF9F7850
        assert relocations[base+0x278] == setter
    # Native release hardcodes main zero; completion tail-calls MAIN setter.
    assert at(0xF8A3350,4) == bytes.fromhex('e1 03 1f 2a')
    assert at(0xFA05EF8,4) == bytes.fromhex('02 35 41 f9')
    assert at(0xF9FC81C,4) == bytes.fromhex('e8 0e 80 52') # container 119
    print('PASS: consumption/release ABI, native envelope calls, virtual slot setters and OFF InventoryAction')

if __name__ == '__main__':
    main()
