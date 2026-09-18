#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FROZEN_HEAD_BLOBS = {
    "src/render/OffhandBlockRenderPatch.cpp": "588113539e731f6de92d7055a6cec4e3f302cc1f",
    "src/render/NativeAttachmentFix.hpp": "a5cf88b8cde4bd602be038c657d4af19917b6c37",
    "src/render/OffhandBlockRenderPatch.hpp": "ea45606173d0eed19b3e86bc93b77e91abd8b2fc",
}

REQUIRED_NAMED_RVAS = {
    "kRenderItemRva": "0xB2F0F60",
    "kDefaultTransformRva": "0xA619618",
    "kMatrixMultiplyRva": "0x98B49A0",
    "kItemStackMatchesRva": "0xFF86E80",
    "kHandEquipPredicateRva": "0xFFA6140",
    "kFirstPersonDataDrivenRenderRva": "0xA79F2C0",
    "kGetOffhandStackRva": "0xF579CA4",
    "kPrepareAttachmentRva": "0x9F013F0",
    "kAttachmentBindingModeRva": "0xFA51F60",
    "kResolveOwnerBoneByNameRva": "0xB42719C",
    "kDrawAttachmentRva": "0x9F03EAC",
    "kComposeAttachmentBoneMatrixRva": "0xFA528A4",
    "kFinalOffhandMatrixTopRva": "0x110AFF88",
}


REQUIRED_CPP_TARGETS = (
    "0xB2F0F60",   # RenderItem
    "0xA619618",   # default item transform
    "0x98B49A0",   # matrix multiply
    "0xFF86E80",   # ItemStack match
    "0xFFA6140",   # hand-equip predicate
    "0xB2FC0BC",   # offhand dispatch callsite
    "0xA79F2C0",   # first-person data-driven renderer
    "0xB2FBE9C",   # first-person data-driven callsite
    "0xF579CA4",   # Actor offhand ItemStack getter
    "0xA6FFAE0",   # TPP offhand RenderItem callsite
    "0xB2F1034",   # renderItem attachable-enabled callsite
    "0xA6FFBA4",   # attachable state
    "0x9F013F0",   # prepare attachment
    "0xFA51F60",   # attachment binding mode
    "0x9F020F0",
    "0x9F02148",
    "0xB42719C",   # resolve owner bone
    "0x9F0210C",
    "0x9F02184",
    "0x9F03EAC",   # draw attachment
    "0xFA528A4",   # compose attachment matrix
    "0x9F5A6D0",   # compose callsite
    "0x110AFF88",  # final matrix top
    "0xB2F7ADC",   # final matrix return
    "0x134BEF38",  # bow id
    "0x134BEF60",  # crossbow id
    "0x134BF140",  # trident id
    "0x134C0668",  # fishing rod id
)


def git_blob_sha(data: bytes) -> str:
    prefix = f"blob {len(data)}\0".encode()
    return hashlib.sha1(prefix + data).hexdigest()


def head_file(path: str) -> bytes:
    return subprocess.check_output(["git", "show", f"HEAD:{path}"], cwd=ROOT)


def main() -> int:
    for path, expected in FROZEN_HEAD_BLOBS.items():
        actual = git_blob_sha(head_file(path))
        if actual != expected:
            raise AssertionError(
                f"renderer source changed: {path}: expected {expected}, got {actual}"
            )

    generator = ROOT / "scripts" / "generate_render_126511_compat.py"
    if not generator.exists():
        raise AssertionError("missing build-only 1.26.51.1 renderer compatibility generator")

    cmake = (ROOT / "CMakeLists.txt").read_text(errors="replace")
    for token in (
        "generate_render_126511_compat.py",
        "generated/render/OffhandBlockRenderPatch.cpp",
        "LEVI_OFFHAND_GENERATED_ROOT",
    ):
        if token not in cmake:
            raise AssertionError(f"CMake missing renderer compatibility marker {token!r}")

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "generated"
        subprocess.check_call(
            [
                "python3",
                str(generator),
                "--source-root",
                str(ROOT),
                "--output-root",
                str(out),
            ],
            cwd=ROOT,
        )
        generated_cpp = (out / "render" / "OffhandBlockRenderPatch.cpp").read_text()
        for token in REQUIRED_CPP_TARGETS:
            if token not in generated_cpp:
                raise AssertionError(f"generated 1.26.51.1 renderer missing {token}")

        # Do not accept the audit comment as proof that runtime constants were
        # translated.  Assert the actual constexpr assignment by identifier.
        for name, value in REQUIRED_NAMED_RVAS.items():
            pattern = re.compile(
                rf"\\b{re.escape(name)}\\s*=\\s*{re.escape(value)}\\b"
            )
            if not pattern.search(generated_cpp):
                raise AssertionError(
                    f"generated renderer did not bind {name} to {value}"
                )

        if "Minecraft Bedrock 1.26.45.1" in generated_cpp:
            raise AssertionError("generated renderer must identify 1.26.51.1 target")

    # The generator must never modify the tracked renderer sources in place.
    for path, expected in FROZEN_HEAD_BLOBS.items():
        actual = git_blob_sha(head_file(path))
        assert actual == expected

    print("v0.2.68 1.26.51.1 build-only renderer compatibility contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
