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

UPPER_USE_RVA = 0x9432794
UPPER_USE_PROLOGUE = bytes.fromhex(
    "ff 43 05 d1 fd 7b 0f a9 fc 6f 10 a9 fa 67 11 a9"
)
UPPER_USE_CALLS = {
    0x9432880: 0xF0B900C,  # Player::getSelectedItem
    0x9432D68: 0xEF75B9C,  # GameMode::baseUseItemAsAttack
    0x9433030: 0xEF75578,  # GameMode::baseUseItem
}

ATTACK_CALLBACK_RVA = 0xEF886A8
ATTACK_CALLBACK_PROLOGUE = bytes.fromhex(
    "ff c3 01 d1 fd 7b 03 a9 f7 23 00 f9 f6 57 05 a9"
)
ATTACK_CALLBACK_CALLS = {
    0xEF886DC: 0xF0B900C,  # deferred callback -> Player::getSelectedItem
}
ATTACK_CALLBACK_VTABLE_RELOC = 0x12270128

DESTROY_RATE_CONTEXT_RVA = 0xF08CC44
DESTROY_RATE_CONTEXT_PROLOGUE = bytes.fromhex(
    "ff 03 02 d1 e9 23 02 6d fd 7b 03 a9 f9 23 00 f9"
)
DESTROY_RATE_CONTEXT_CALLER = {0xF0BBD90: DESTROY_RATE_CONTEXT_RVA}
# This exact pair-load proves context+0x08 = Block* and context+0x10 = ItemStack*.
DESTROY_RATE_STACK_LOAD_RVA = 0xF08CD18
DESTROY_RATE_STACK_LOAD = 0xA940CE88  # ldp x8, x19, [x20,#0x8]
DESTROY_RATE_STACK_BLOCK_CALL = {0xF08CDA4: 0xF89A340}

PLAYER_GAMEMODE_GETTER_RVA = 0xF0CD850
PLAYER_GAMEMODE_GETTER = bytes.fromhex("00 f4 44 f9 c0 03 5f d6")

CAN_DESTROY_SPECIAL_RELOCS = {
    0x122E2028: 0xF665914,  # Item base -> false
    0x122E4050: 0xF6B3EE0,  # DiggerItem override
    0x122C62C8: 0xF4C7B20,  # WeaponItem override
    0x122C5120: 0xF665914,  # Trident -> base false
}
CAN_DESTROY_SPECIAL_FINGERPRINTS = {
    0xF665914: bytes.fromhex("e0 03 1f 2a c0 03 5f d6"),
    0xF6B3EE0: bytes.fromhex("fd 7b be a9 f4 4f 01 a9 fd 03 00 91"),
    0xF4C7B20: bytes.fromhex("fd 7b be a9 f4 4f 01 a9 fd 03 00 91"),
}


def fail(message: str) -> None:
    raise AssertionError(message)


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def decode_bl_target(data: bytes, rva: int) -> int | None:
    insn = struct.unpack_from("<I", data, rva)[0]
    if insn & 0xFC000000 != 0x94000000:
        return None
    imm26 = insn & 0x03FFFFFF
    return rva + (sign_extend(imm26, 26) << 2)


def build_id(path: Path) -> str:
    out = subprocess.check_output(["readelf", "-n", str(path)], text=True)
    m = re.search(r"Build ID:\s*([0-9a-fA-F]+)", out)
    if not m:
        fail("GNU Build ID not found")
    return m.group(1).lower()


def relative_relocations(path: Path) -> dict[int, int]:
    out = subprocess.check_output(["readelf", "-rW", str(path)], text=True)
    result: dict[int, int] = {}
    pattern = re.compile(
        r"^([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+R_AARCH64_RELATIVE\s+([0-9a-fA-F]+)\s*$"
    )
    for line in out.splitlines():
        m = pattern.match(line.strip())
        if m:
            result[int(m.group(1), 16)] = int(m.group(2), 16)
    return result


def direct_callers(data: bytes, target: int) -> list[int]:
    callers: list[int] = []
    end = TEXT_START + TEXT_SIZE
    for rva in range(TEXT_START, end, 4):
        if decode_bl_target(data, rva) == target:
            callers.append(rva)
    return callers


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        fail("exact 1.26.45.1 SHA-256 mismatch")
    if build_id(path) != EXPECTED_BUILD_ID:
        fail("exact 1.26.45.1 Build ID mismatch")

    if data[UPPER_USE_RVA:UPPER_USE_RVA + len(UPPER_USE_PROLOGUE)] != UPPER_USE_PROLOGUE:
        fail("upper-use dispatcher fingerprint mismatch")
    for callsite, target in UPPER_USE_CALLS.items():
        actual = decode_bl_target(data, callsite)
        if actual != target:
            fail(f"upper-use BL 0x{callsite:X}: expected 0x{target:X}, got {actual!r}")

    if data[
        ATTACK_CALLBACK_RVA:
        ATTACK_CALLBACK_RVA + len(ATTACK_CALLBACK_PROLOGUE)
    ] != ATTACK_CALLBACK_PROLOGUE:
        fail("deferred attack callback fingerprint mismatch")
    for callsite, target in ATTACK_CALLBACK_CALLS.items():
        actual = decode_bl_target(data, callsite)
        if actual != target:
            fail(
                f"deferred attack callback BL 0x{callsite:X}: "
                f"expected 0x{target:X}, got {actual!r}"
            )

    if data[
        DESTROY_RATE_CONTEXT_RVA:
        DESTROY_RATE_CONTEXT_RVA + len(DESTROY_RATE_CONTEXT_PROLOGUE)
    ] != DESTROY_RATE_CONTEXT_PROLOGUE:
        fail("destroy-rate context fingerprint mismatch")
    for callsite, target in DESTROY_RATE_CONTEXT_CALLER.items():
        actual = decode_bl_target(data, callsite)
        if actual != target:
            fail(f"destroy-rate caller mismatch at 0x{callsite:X}: {actual!r}")
    if direct_callers(data, DESTROY_RATE_CONTEXT_RVA) != [0xF0BBD90]:
        fail("destroy-rate context no longer has the proven unique caller")

    actual_word = struct.unpack_from("<I", data, DESTROY_RATE_STACK_LOAD_RVA)[0]
    if actual_word != DESTROY_RATE_STACK_LOAD:
        fail(
            f"destroy-rate context stack-load fingerprint changed: "
            f"0x{actual_word:08X}"
        )
    for callsite, target in DESTROY_RATE_STACK_BLOCK_CALL.items():
        actual = decode_bl_target(data, callsite)
        if actual != target:
            fail(f"block+stack destroy-rate call mismatch at 0x{callsite:X}: {actual!r}")

    if data[
        PLAYER_GAMEMODE_GETTER_RVA:
        PLAYER_GAMEMODE_GETTER_RVA + len(PLAYER_GAMEMODE_GETTER)
    ] != PLAYER_GAMEMODE_GETTER:
        fail("Player game-mode getter fingerprint mismatch")

    relocs = relative_relocations(path)
    if relocs.get(ATTACK_CALLBACK_VTABLE_RELOC) != ATTACK_CALLBACK_RVA:
        fail(
            "deferred attack callback vtable relocation mismatch: "
            f"expected 0x{ATTACK_CALLBACK_RVA:X}, "
            f"got {relocs.get(ATTACK_CALLBACK_VTABLE_RELOC)!r}"
        )

    for address, expected in CAN_DESTROY_SPECIAL_RELOCS.items():
        actual = relocs.get(address)
        if actual != expected:
            fail(
                f"canDestroySpecial vtable relocation 0x{address:X}: "
                f"expected 0x{expected:X}, got {actual!r}"
            )
    for rva, fingerprint in CAN_DESTROY_SPECIAL_FINGERPRINTS.items():
        if data[rva:rva + len(fingerprint)] != fingerprint:
            fail(f"canDestroySpecial target fingerprint mismatch at 0x{rva:X}")

    print(
        "v0.2.68 semantic bridge binary contract passed: upper-use dispatcher, "
        "deferred attack selected-stack callback, destroy-rate context stack slot, "
        "Player game-mode ownership, and canDestroySpecial ABI"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
