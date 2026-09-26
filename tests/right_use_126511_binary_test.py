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
    constants = dict(re.findall(r'constexpr std::uintptr_t k(\w+)Rva = (0x[0-9A-F]+);', src))
    guards = re.findall(r'k(\w+)Fingerprint\{([^}]+)\}', src)
    assert len(guards) == 13, 'review new/removed guards explicitly'
    for name, body in guards:
        expected = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', body))
        assert at(int(constants[name], 16), len(expected)) == expected, name
    weapon_use = int(constants['WeaponItemNoopUse'], 16)
    assert relocations[0x1306D7D8+0x290] == weapon_use
    assert at(weapon_use, 8) == bytes.fromhex('e0 03 01 aa c0 03 5f d6'), 'WeaponItem use must be a no-op'
    for base, use_name, use_on_name in (
        (0x1307B730, 'BaseItemUse', 'BaseItemUseOn'),
        (0x1306EE98, 'ComponentItemUse', 'ComponentItemUseOn'),
    ):
        assert relocations[base+0x290] == int(constants[use_name], 16)
        assert relocations[base+0x418] == int(constants[use_on_name], 16)
    for callsite, target in {
        0x9F01338: 0xFFA6140,
        0x9F0216C: 0xFA6F684, 0x9F021AC: 0xFA6F684, 0x9F02228: 0xFA6F684,
        0x9F021E0: 0xFA51F78, 0x9F02614: 0xFA51F78,
    }.items():
        word = int.from_bytes(at(callsite, 4), 'little')
        assert word & 0xFC000000 == 0x94000000, f"not BL: {callsite:#x}"
        immediate = word & 0x3FFFFFF
        if immediate & 0x2000000:
            immediate -= 0x4000000
        assert callsite + immediate*4 == target, f"call target mismatch: {callsite:#x}"
    print('PASS: 1.26.51.1 binary identity, 13 guards, WeaponItem no-op, Item/ComponentItem use ABI')

if __name__ == '__main__':
    main()
