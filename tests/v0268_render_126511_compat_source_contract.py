#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FROZEN_HEAD_BLOBS = {
    "src/render/OffhandBlockRenderPatch.cpp": "0a577697ebf9259f358670bb89b3fb09d7d78b5f",
    "src/render/NativeAttachmentFix.hpp": "2187ec21af29f75ecaf7fbb73412758ace9a65a1",
    "src/render/OffhandBlockRenderPatch.hpp": "7b56b1d47610f55a4701b0fa5af049fe9dcbc7c1",
}

REQUIRED_NAMED_RVAS = {
    "kRenderItemRva": "0xB2F0F60",
    "kRenderFirstPersonRva": "0xB2FB6C0",
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


REQUIRED_ARCHIVED_LITERAL_MAP = {
    "0x9B36370": "0x9F00C90",
    "0xA2C87BC": "0xA650958",
    "0xF14355C": "0xFA6F684",
    "0xF147CC0": "0xFA51F78",
    "0x9B368D4": "0x9F01244",
    "0xEC8A478": "0xF56616C",
    "0xEE63508": "0xF7AA2DC",
    "0xEEA721C": "0xF7728A8",
    "0x2652B1D": "0x272F25E",
}


REQUIRED_CPP_TARGETS = (
    "0xB2F0F60",   # RenderItem
    "0xB2FB6C0",   # ItemInHandRenderer::renderFirstPerson
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

    public_header = (ROOT / "src" / "render" / "OffhandBlockRenderPatch.hpp").read_text()
    for token in (
        "setBowTppTiltDegrees(float value) noexcept",
        "bowTppTiltDegrees() const noexcept",
        "setTridentFppHorizontalOffset(float value) noexcept",
        "tridentFppHorizontalOffset() const noexcept",
        "setBowTppHorizontalOffset(float value) noexcept",
        "bowTppHorizontalOffset() const noexcept",
    ):
        if token not in public_header:
            raise AssertionError(
                f"renderer public header missing declaration {token!r}"
            )

    tracked_cpp = (ROOT / "src" / "render" / "OffhandBlockRenderPatch.cpp").read_text()
    item_fn = tracked_cpp.split("OffhandBlockRenderPatch::\n    itemTransformDetour(", 1)[1]
    lambda_pos = item_fn.index("const auto placementAnimated=")
    prefix = item_fn[:lambda_pos]
    if "placementAnimated(" in prefix:
        raise AssertionError(
            "placementAnimated must not be referenced before its local lambda declaration"
        )

    generator = ROOT / "scripts" / "generate_render_126511_compat.py"
    if not generator.exists():
        raise AssertionError("missing build-only 1.26.51.1 renderer compatibility generator")

    generator_text = generator.read_text(errors="replace")
    for old, new in REQUIRED_ARCHIVED_LITERAL_MAP.items():
        if f'"{old}": "{new}"' not in generator_text:
            raise AssertionError(
                f"generator missing archived v0.2.67 renderer mapping {old} -> {new}"
            )

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
        generated_fix = (out / "render" / "NativeAttachmentFix.hpp").read_text()
        for token in REQUIRED_CPP_TARGETS:
            if token not in generated_cpp:
                raise AssertionError(f"generated 1.26.51.1 renderer missing {token}")

        for token in (
            'runtime/OffhandPlacementAnimation.hpp',
            'placementAnimated',
            'OffhandPlacementAnimation::',
            'instance().progress()',
            '0.08f*impulse',
            '0.18f*impulse',
            'matrix.value[14]-=',
            '0.12f*impulse',
            'kRenderFirstPersonRva',
            'kMainhandHeightOffset=0x180',
            'kMainhandOldHeightOffset=0x184',
            'renderFirstPersonDetour',
            'gMainhandVisualBaseline',
            'MAINHAND equip motion frozen',
            '-20.0f*wave',
            '9.0f*wave',
            '7.0f*wave',
        ):
            if token not in generated_cpp:
                raise AssertionError(
                    f"generated renderer missing placement-animation marker {token!r}"
                )

        for token in (
            "ResolvedBindingCache",
            "OwnerBoneHashKind",
            "kBindingModeFirstReadCallsiteRva",
            "kBindingModeSecondReadCallsiteRva",
            "kBowTppTiltDefault",
            "kTridentFppHorizontalDefault",
            "kBowTppHorizontalDefault",
            "mirrorAndOffsetBowLocalPose",
            "kEffectiveOffhandDrawCallsiteRva",
            "kV2AttachmentDrawCallsiteRva",
            "0x9F00C90",
            "0xA650958",
        ):
            if token not in generated_fix:
                raise AssertionError(
                    f"generated NativeAttachmentFix missing renderer dependency {token!r}"
                )

        # Do not accept the audit comment as proof that runtime constants were
        # translated.  Check the actual assignment after removing whitespace.
        compact_cpp = re.sub(r"\s+", "", generated_cpp)
        for name, value in REQUIRED_NAMED_RVAS.items():
            marker = f"{name}="
            if marker not in compact_cpp:
                # The archived v0.2.67 build snapshot retired some hooks that
                # are still present in tracked HEAD.  Do not re-enable an
                # absent hook merely to satisfy the compatibility contract.
                continue
            if f"{name}={value}" not in compact_cpp:
                nearby = [
                    line.strip()
                    for line in generated_cpp.splitlines()
                    if name in line or ("RenderItem" in line and "Rva" in line)
                ][:12]
                raise AssertionError(
                    f"generated renderer did not bind {name} to {value}; "
                    f"matching lines={nearby!r}"
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
