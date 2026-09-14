#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import struct
import sys
import zipfile
from pathlib import Path

EXPECTED_SHA256 = "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"
EXPECTED_BUILD_ID = bytes.fromhex("868e275cb295e9a275bb29d2258edc2f7dc48761")
ITEM_CTOR_FLAG_RVA = 0xF65A3BC
ITEM_CTOR_FLAG_BYTES = bytes.fromhex("09 0A 80 52")
GET_ALLOW_RVA = 0xF644930
GET_ALLOW_BYTES = bytes.fromhex(
    "08 04 40 F9 88 00 00 B4 00 01 40 F9 40 00 00 B4 "
    "D6 8B 00 14 E0 03 1F 2A C0 03 5F D6"
)
OFFHAND_VALIDATION_RVA = 0xF6F5D10
OFFHAND_VALIDATION_PREFIX = bytes.fromhex(
    "FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 68 8C 40 39"
)
OFFHAND_RTTI = b"26OffhandContainerValidation\x00"
PLANNER_RVA = 0xF024024
PLANNER_PREFIX = bytes.fromhex(
    "FF 83 05 D1 FD 7B 10 A9 FC 6F 11 A9 FA 67 12 A9 "
    "F8 5F 13 A9 F6 57 14 A9 F4 4F 15 A9 FD 03 04 91"
)
PLANNER_CALLS = (0xF023868, 0xF02E09C, 0xF03B900)
STRIDE_RVA = 0xF0240EC
STRIDE_BYTES = bytes.fromhex("18 83 00 91")


def read_input(path: Path) -> bytes:
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            return archive.read("libminecraftpe.so")
    return path.read_bytes()


def load_segments(data: bytes) -> list[tuple[int, int, int, int]]:
    assert data[:4] == b"\x7fELF", "expected ELF"
    assert data[4] == 2 and data[5] == 1, "expected ELF64 little-endian"
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    out = []
    for index in range(phnum):
        off = phoff + index * phentsize
        p_type, flags, file_off, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from(
            "<IIQQQQQQ", data, off
        )
        if p_type == 1:
            out.append((vaddr, file_off, filesz, flags))
    return out


def rva_to_file_offset(segments: list[tuple[int, int, int, int]], rva: int) -> int:
    for vaddr, file_off, filesz, _flags in segments:
        if vaddr <= rva < vaddr + filesz:
            return file_off + rva - vaddr
    raise AssertionError(f"RVA 0x{rva:X} not in file-backed PT_LOAD")


def assert_bytes(data: bytes, segments, rva: int, expected: bytes, label: str) -> None:
    off = rva_to_file_offset(segments, rva)
    actual = data[off : off + len(expected)]
    assert actual == expected, f"{label} mismatch @ 0x{rva:X}: {actual.hex(' ')}"


def decode_bl_target(address: int, instruction: int) -> int | None:
    if instruction & 0xFC000000 != 0x94000000:
        return None
    imm26 = instruction & 0x03FFFFFF
    if imm26 & 0x02000000:
        imm26 -= 0x04000000
    return address + (imm26 << 2)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so-or-zip")
        return 2
    data = read_input(Path(sys.argv[1]))
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA256, f"SHA mismatch: {digest}"
    assert EXPECTED_BUILD_ID in data, "expected GNU Build ID bytes not found"

    segments = load_segments(data)
    assert_bytes(data, segments, ITEM_CTOR_FLAG_RVA, ITEM_CTOR_FLAG_BYTES, "Item::Item flags")
    assert_bytes(data, segments, GET_ALLOW_RVA, GET_ALLOW_BYTES, "getAllowOffHand")
    assert_bytes(
        data,
        segments,
        OFFHAND_VALIDATION_RVA,
        OFFHAND_VALIDATION_PREFIX,
        "OffhandContainerValidation",
    )
    assert data.count(OFFHAND_RTTI) >= 1, "OffhandContainerValidation RTTI missing"
    assert_bytes(data, segments, PLANNER_RVA, PLANNER_PREFIX, "auto-insert planner")
    assert_bytes(data, segments, STRIDE_RVA, STRIDE_BYTES, "destination stride")

    hits: list[int] = []
    for vaddr, file_off, filesz, flags in segments:
        if not (flags & 1):
            continue
        blob = data[file_off : file_off + filesz]
        for rel in range(0, len(blob) - 3, 4):
            instruction = struct.unpack_from("<I", blob, rel)[0]
            address = vaddr + rel
            if decode_bl_target(address, instruction) == PLANNER_RVA:
                hits.append(address)
    assert tuple(hits) == PLANNER_CALLS, f"planner callsites mismatch: {[hex(x) for x in hits]}"

    print("v0.2.62 native routing binary contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
