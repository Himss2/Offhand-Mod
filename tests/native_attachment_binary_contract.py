import hashlib
import struct
import sys
from pathlib import Path


EXPECTED_SHA256 = (
    "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"
)


def load_virtual_bytes(binary: bytes, address: int, size: int) -> bytes:
    assert binary[:4] == b"\x7fELF", "not an ELF file"
    assert binary[4] == 2, "expected ELF64"
    assert binary[5] == 1, "expected little-endian ELF"

    program_header_offset = struct.unpack_from("<Q", binary, 0x20)[0]
    program_header_size = struct.unpack_from("<H", binary, 0x36)[0]
    program_header_count = struct.unpack_from("<H", binary, 0x38)[0]

    for index in range(program_header_count):
        offset = program_header_offset + index * program_header_size
        segment_type, _flags, file_offset, virtual_address = struct.unpack_from(
            "<IIQQ", binary, offset
        )
        file_size = struct.unpack_from("<Q", binary, offset + 0x20)[0]

        if (
            segment_type == 1
            and virtual_address <= address
            and address + size <= virtual_address + file_size
        ):
            start = file_offset + address - virtual_address
            return binary[start:start + size]

    raise AssertionError(f"RVA 0x{address:X} is outside file-backed PT_LOAD")


def branch_target(binary: bytes, address: int, link: bool) -> int:
    instruction = int.from_bytes(
        load_virtual_bytes(binary, address, 4), "little"
    )
    expected_opcode = 0x94000000 if link else 0x14000000
    assert instruction & 0xFC000000 == expected_opcode, (
        f"0x{address:X} is not {'BL' if link else 'B'}"
    )

    immediate = instruction & 0x03FFFFFF
    if immediate & 0x02000000:
        immediate -= 0x04000000
    return address + immediate * 4


def main() -> None:
    assert len(sys.argv) == 2, "usage: native_attachment_binary_contract.py LIB"
    path = Path(sys.argv[1])
    binary = path.read_bytes()

    assert hashlib.sha256(binary).hexdigest() == EXPECTED_SHA256, (
        "libminecraftpe.so SHA-256 does not match the 1.26.45.1 target"
    )

    assert load_virtual_bytes(binary, 0x9B36358, 4) == bytes.fromhex(
        "c8008052"
    ), "slot-6 MOV fingerprint changed"
    assert branch_target(binary, 0x9B36370, link=True) == 0x9B368D4
    assert branch_target(binary, 0x9B3680C, link=False) == 0x9B36A80
    assert branch_target(binary, 0x9B36A18, link=False) == 0x9B3A228
    assert branch_target(binary, 0xA2C837C, link=True) == 0x9B36A80
    assert branch_target(binary, 0xA2C87BC, link=True) == 0x9B3A228
    assert branch_target(binary, 0x9B3779C, link=True) == 0xAF3A1E4
    assert branch_target(binary, 0x9B37814, link=True) == 0xAF3A1E4
    assert branch_target(binary, 0x9B254C8, link=True) == 0xF147ED0
    assert branch_target(binary, 0xADE9E9C, link=True) == 0xA31662C

    fingerprints = {
        0x9B36A80: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
        0xAF3A1E4: "ff4302d1fd7b03a9fc6f04a9fa6705a9",
        0x9B3A228: "ff0305d1e86b00fdfd7b0ea9fc6f0fa9",
        0xF147ED0: "08784339a8000034008441ad028c42ad",
    }

    for address, expected in fingerprints.items():
        assert load_virtual_bytes(binary, address, 16) == bytes.fromhex(
            expected
        ), f"entry fingerprint changed at 0x{address:X}"

    print(
        "native attachment binary contract passed: "
        "1 slot, 9 branches, 4 entry fingerprints"
    )


if __name__ == "__main__":
    main()
