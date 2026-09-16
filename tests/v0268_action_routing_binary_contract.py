#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"
EXPECTED_BUILD_ID = "868e275cb295e9a275bb29d2258edc2f7dc48761"
TEXT_START = 0x06121060
TEXT_SIZE = 0x0BA6EED4

BOUNDARIES = {
    "GameMode::_attack": {
        "rva": 0xEF721E4,
        "prologue": bytes.fromhex("ff 43 07 d1 fd 7b 17 a9 fc c3 00 f9 fa 67 19 a9"),
        "refs": [(0xEF72368, 0x122700F8)],
    },
    "GameMode::baseUseItem": {
        "rva": 0xEF75578,
        "prologue": bytes.fromhex("ff 43 04 d1 fd 7b 0d a9 fc 5f 0e a9 f6 57 0f a9"),
        "refs": [(0xEF75684, 0x12270308), (0xEF756B0, 0x12270388)],
    },
    "GameMode::baseUseItemAsAttack": {
        "rva": 0xEF75B9C,
        "prologue": bytes.fromhex("ff 83 04 d1 fd 7b 0d a9 fc 73 00 f9 f8 5f 0f a9"),
        "refs": [(0xEF75CB4, 0x12270408), (0xEF75CE0, 0x12270488)],
    },
    "GameMode::releaseUsingItem": {
        "rva": 0xEF76108,
        "prologue": bytes.fromhex("ff 03 04 d1 fd 7b 0c a9 f7 6b 00 f9 f6 57 0e a9"),
        "refs": [(0xEF76298, 0x12270508), (0xEF762A4, 0x12270588)],
    },
    "GameMode::interact": {
        "rva": 0xEF71B7C,
        "prologue": bytes.fromhex("fd 7b bb a9 fc 67 01 a9 f8 5f 02 a9 f6 57 03 a9"),
        "refs": [(0xEF71CC4, 0x1226FFF8)],
    },
    "GameMode::startDestroyBlock": {
        "rva": 0xEF72684,
        "prologue": bytes.fromhex("ff 83 01 d1 fd 7b 01 a9 f9 13 00 f9 f8 5f 03 a9"),
        "refs": [],
        "words": [(0xEF72704, 0x390002BF)],
        "bl_targets": [(0xEF72734, 0xEF729FC)],
    },
}
REQUIRED = {
    "GameMode::_attack",
    "GameMode::baseUseItem",
    "GameMode::baseUseItemAsAttack",
    "GameMode::releaseUsingItem",
    "GameMode::interact",
    "GameMode::startDestroyBlock",
}

# Exact accessors used by NativeCapabilityProbe. Shipping Android does not
# expose these through dlsym, so production resolves module-base + RVA and
# refuses to enable if any instruction fingerprint differs.
ACCESSOR_FINGERPRINTS = {
    "Player::getSelectedItem": (
        0xF0B900C,
        bytes.fromhex("08 b8 42 f9 09 c1 42 39 89 00 00 34 e0 b1 01 f0"),
    ),
    "Actor::getOffhandSlot": (
        0xEC9D62C,
        bytes.fromhex("fd 7b bf a9 fd 03 00 91 00 20 00 91 03 30 10 94"),
    ),
    "ItemStackBase::isNull": (
        0xF63E760,
        bytes.fromhex("08 8c 40 39 08 05 00 34 fd 7b be a9 f3 0b 00 f9"),
    ),
    "Player::isUsingItem": (
        0xF0B8094,
        bytes.fromhex("fd 7b bf a9 fd 03 00 91 00 60 1b 91 b0 19 16 94"),
    ),
    "Player::getItemInUseStack": (
        0xF0B80B4,
        bytes.fromhex("00 60 1b 91 c0 03 5f d6"),
    ),
    "ItemStackBase::differsForUse": (
        0xF6443F4,
        bytes.fromhex("08 88 40 39 29 88 40 39 1f 01 09 6b 01 01 00 54"),
    ),
}

# Item vtable slot 38 (0x130) is getAttackDamage(). These relative relocations
# prove that base Item returns zero while WeaponItem, DiggerItem and TridentItem
# override the same slot with real attack-damage implementations.
COMBAT_VTABLE_RELOCS = {
    0x122E2048: 0xF667504,  # Item::getAttackDamage -> 0
    0x122C62E8: 0xF4C7B00,  # WeaponItem::getAttackDamage
    0x122E4070: 0xF6B3880,  # DiggerItem::getAttackDamage
    0x122C5140: 0xF46CAEC,  # TridentItem::getAttackDamage -> 8
}
COMBAT_FUNCTION_FINGERPRINTS = {
    0xF667504: bytes.fromhex("e0 03 1f 2a c0 03 5f d6"),
    0xF4C7B00: bytes.fromhex("00 c8 41 b9 c0 03 5f d6"),
    0xF6B3880: bytes.fromhex("00 d8 41 b9 c0 03 5f d6"),
    0xF46CAEC: bytes.fromhex("00 01 80 52 c0 03 5f d6"),
}


def fail(message: str) -> None:
    raise AssertionError(message)


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def decode_adrp_add_target(data: bytes, rva: int) -> int | None:
    adrp = struct.unpack_from("<I", data, rva)[0]
    add = struct.unpack_from("<I", data, rva + 4)[0]
    if adrp & 0x9F000000 != 0x90000000:
        return None
    if add & 0x7F000000 != 0x11000000:
        return None
    adrp_rd = adrp & 31
    add_rn = (add >> 5) & 31
    if adrp_rd != add_rn:
        return None
    immlo = (adrp >> 29) & 3
    immhi = (adrp >> 5) & 0x7FFFF
    page_delta = sign_extend((immhi << 2) | immlo, 21) << 12
    base = (rva & ~0xFFF) + page_delta
    imm12 = (add >> 10) & 0xFFF
    shift = 12 if ((add >> 22) & 1) else 0
    return base + (imm12 << shift)


def decode_bl_target(data: bytes, rva: int) -> int | None:
    insn = struct.unpack_from("<I", data, rva)[0]
    if insn & 0xFC000000 != 0x94000000:
        return None
    imm26 = insn & 0x03FFFFFF
    return rva + (sign_extend(imm26, 26) << 2)


def get_build_id(path: Path) -> str:
    output = subprocess.check_output(["readelf", "-n", str(path)], text=True)
    match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", output)
    if not match:
        fail("GNU Build ID not found")
    return match.group(1).lower()


def relative_relocations(path: Path) -> dict[int, int]:
    output = subprocess.check_output(["readelf", "-rW", str(path)], text=True)
    relocations: dict[int, int] = {}
    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+R_AARCH64_RELATIVE\s+([0-9a-fA-F]+)\s*$"
    )
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if match:
            relocations[int(match.group(1), 16)] = int(match.group(2), 16)
    return relocations


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    data = path.read_bytes()
    actual_sha = hashlib.sha256(data).hexdigest()
    if actual_sha != EXPECTED_SHA256:
        fail(f"SHA-256 mismatch: {actual_sha}")
    actual_build_id = get_build_id(path)
    if actual_build_id != EXPECTED_BUILD_ID:
        fail(f"Build ID mismatch: {actual_build_id}")

    missing = sorted(REQUIRED - BOUNDARIES.keys())
    if missing:
        fail(f"missing required verified boundary: {', '.join(missing)}")

    text_end = TEXT_START + TEXT_SIZE
    for name, spec in BOUNDARIES.items():
        rva = int(spec["rva"])
        prologue = bytes(spec["prologue"])
        if not (TEXT_START <= rva < text_end):
            fail(f"{name}: RVA is outside .text")
        if data[rva:rva + len(prologue)] != prologue:
            fail(f"{name}: prologue fingerprint mismatch at 0x{rva:X}")
        for ref_rva, target in spec["refs"]:
            actual = decode_adrp_add_target(data, ref_rva)
            if actual != target:
                fail(
                    f"{name}: ADRP+ADD reference mismatch at 0x{ref_rva:X}: "
                    f"expected 0x{target:X}, got {actual!r}"
                )
        for word_rva, expected_word in spec.get("words", []):
            actual_word = struct.unpack_from("<I", data, word_rva)[0]
            if actual_word != expected_word:
                fail(
                    f"{name}: instruction fingerprint mismatch at 0x{word_rva:X}: "
                    f"expected 0x{expected_word:08X}, got 0x{actual_word:08X}"
                )
        for call_rva, expected_target in spec.get("bl_targets", []):
            actual_target = decode_bl_target(data, call_rva)
            if actual_target != expected_target:
                fail(
                    f"{name}: BL target mismatch at 0x{call_rva:X}: "
                    f"expected 0x{expected_target:X}, got {actual_target!r}"
                )

    for name, (rva, fingerprint) in ACCESSOR_FINGERPRINTS.items():
        if data[rva:rva + len(fingerprint)] != fingerprint:
            fail(f"{name}: exact accessor fingerprint mismatch at 0x{rva:X}")

    for rva, fingerprint in COMBAT_FUNCTION_FINGERPRINTS.items():
        if data[rva:rva + len(fingerprint)] != fingerprint:
            fail(f"combat virtual target fingerprint mismatch at 0x{rva:X}")

    relocations = relative_relocations(path)
    for slot_address, expected_target in COMBAT_VTABLE_RELOCS.items():
        actual_target = relocations.get(slot_address)
        if actual_target != expected_target:
            fail(
                f"Item getAttackDamage vtable relocation mismatch at 0x{slot_address:X}: "
                f"expected 0x{expected_target:X}, got {actual_target!r}"
            )

    print(
        "v0.2.68 action routing binary contract passed: "
        "attack/interact/use/release/start-destroy boundaries, exact hand/use accessors, "
        "and Item::getAttackDamage slot-38 combat ABI proven; "
        "full mining lifecycle remains fail-closed until continue/finalize/cancel are proven"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
