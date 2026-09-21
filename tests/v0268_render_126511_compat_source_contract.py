#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCE_FILES = (
    "src/render/OffhandBlockRenderPatch.cpp",
    "src/render/NativeAttachmentFix.hpp",
    "src/render/OffhandBlockRenderPatch.hpp",
)

REQUIRED_NAMED_RVAS = {
    "kRenderItemRva": "0xB2F0F60",
    "kDefaultTransformRva": "0xA619618",
    "kMatrixMultiplyRva": "0x98B49A0",
    "kItemStackMatchesRva": "0xFF86E80",
    "kHandEquipPredicateRva": "0xFFA6140",
    "kRenderFirstPersonRva": "0xB2FB6C0",
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
    "0x9B369C8": "0x9F01338",
    "0x9B377FC": "0x9F0216C",
    "0x9B3783C": "0x9F021AC",
    "0x9B378B8": "0x9F02228",
    "0x9B37870": "0x9F021E0",
    "0x9B37CB0": "0x9F02614",

    "0xF14355C": "0xFA6F684",
    "0xF147CC0": "0xFA51F78",
    "0x9B368D4": "0x9F01244",
    "0xEC8A478": "0xF56616C",
    "0xEE63508": "0xF7AA2DC",
    "0xEEA721C": "0xF7728A8",
    "0x2652B1D": "0x272F25E",
    "0xADE96B0": "0xB2FB6C0",
}


REQUIRED_CPP_TARGETS = (
    "0xB2F0F60",   # RenderItem
    "0xA619618",   # default item transform
    "0x98B49A0",   # matrix multiply
    "0xFF86E80",   # ItemStack match
    "0xFFA6140",   # hand-equip predicate
    "0xB2FC0BC",   # offhand dispatch callsite
    "0xB2FB6C0",   # ItemInHandRenderer::renderFirstPerson
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


def main() -> int:
    before = {path: (ROOT / path).read_bytes() for path in SOURCE_FILES}
    cpp = before[SOURCE_FILES[0]].decode()
    header = before[SOURCE_FILES[1]]

    hand_start = cpp.index("handEquipPredicateDetour(")
    hand_end = cpp.index("using RenderItemRouteFn", hand_start)
    hand_equip = cpp[hand_start:hand_end]
    for token in (
        "family==ToolFamily::Bow",
        "family==ToolFamily::Crossbow",
        "[GenericLeftFppRoute] %s genericDispatch=1",
    ):
        if token not in hand_equip:
            raise AssertionError(f"Crossbow/Bow FPP admission missing {token!r}")

    off_start = cpp.index("renderOffhandDetour(")
    off_end = cpp.index("blockRenderPredicateDetour(", off_start)
    render_offhand = cpp[off_start:off_end]

    # Build #470 Banner routing is intentionally restored, but ONLY with the
    # 1.26.51.1 member layout recovered from the new BannerItem constructor.
    for token in (
        "StackBlockOverride",
        "kBannerWallBlockOffset",
        "kBannerStandingBlockOffset",
        "build470 block bridge restored for 1.26.51.1",
    ):
        if token not in render_offhand and token not in cpp:
            raise AssertionError(
                f"Banner build470 block-path routing missing {token!r}"
            )

    compact_cpp = re.sub(r"\\s+", "", cpp)
    for token in (
        "kBannerWallBlockOffset=0x1D0",
        "kBannerStandingBlockOffset=0x1D8",
    ):
        if token not in compact_cpp:
            raise AssertionError(
                f"Banner 1.26.51.1 layout missing {token!r}"
            )

    for stale in (
        "kBannerWallBlockOffset=0x1C0",
        "kBannerStandingBlockOffset=0x1C8",
    ):
        if stale in compact_cpp:
            raise AssertionError(
                f"stale build-470 Banner layout returned: {stale}"
            )

    item_start = cpp.index("itemTransformDetour(")
    item_end = cpp.index("renderObjectDetour(", item_start)
    item_transform = cpp[item_start:item_end]
    for token in (
        "gBridgeDepth!=0",
        "kBannerScale",
        "kBannerShiftX",
        "kBannerShiftY",
        "kBannerYawDegrees",
        "[TransformFix] Banner build470 pose active",
    ):
        if token not in item_transform:
            raise AssertionError(
                f"Banner build470 FPP transform missing {token!r}"
            )

    if "bannerBridgeTransform" in item_transform:
        raise AssertionError(
            "Banner must use build470 block-path routing, not the later bridge-depth exception"
        )

    # Exact MAINHAND arm-height source identified in renderFirstPerson.
    # Keep OFFHAND height fields and all gameplay routing out of this detour.
    for token in (
        "kRenderFirstPersonRva=0xADE96B0",
        "kRenderFirstPersonFingerprint",
        "kMainhandHeightOffset=0x180",
        "kMainhandOldHeightOffset=0x184",
        "renderFirstPersonDetour(",
        "gMainhandStableHeight",
        "[PlacementVisual] MAINHAND player_arm_height frozen",
        "Placement visual: MAINHAND arm-height freeze armed",
    ):
        if token not in compact_cpp and token not in cpp:
            raise AssertionError(
                f"MAINHAND arm-height freeze missing {token!r}"
            )

    freeze_start = cpp.index("renderFirstPersonDetour(")
    freeze_end = cpp.index("using FinalMatrixTopFn", freeze_start)
    freeze_body = cpp[freeze_start:freeze_end]
    compact_freeze = re.sub(r"\s+", "", freeze_body)

    for token in (
        "writeValue<float>(self,kMainhandHeightOffset,frozenHeight)",
        "writeValue<float>(self,kMainhandOldHeightOffset,frozenHeight)",
        "writeValue<float>(self,kMainhandHeightOffset,liveHeight)",
        "writeValue<float>(self,kMainhandOldHeightOffset,liveOldHeight)",
        "OffhandPlacementAnimation::instance().progress()",
        "original(self,renderContext,prevProjection,itemFlags)",
    ):
        if token not in compact_freeze:
            raise AssertionError(
                f"MAINHAND arm-height freeze scope missing {token!r}"
            )

    for forbidden in (
        "LocalPlayer::swing",
        "RightUseRouter",
        "useItemOnBlock",
        "baseUseItem",
    ):
        if forbidden in freeze_body:
            raise AssertionError(
                f"MAINHAND renderer freeze touched gameplay path {forbidden!r}"
            )

    # The renderer and attachment helper must come from the same accepted overlay.
    assert git_blob_sha(header) == "deb12b1f33aa36e91d143d996ea92c415604f128"
    fn = cpp[cpp.index("    itemTransformDetour("):]
    guard_end = fn.index("        const void* item=")
    assert "placementAnimated" not in fn[:guard_end], "early vanilla guard must not use an undeclared lambda"
    assert fn.index("const auto placementAnimated=") < fn.index("return placementAnimated(")

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
        generated_header = (out / "render" / "NativeAttachmentFix.hpp").read_text()
        combined = re.sub(r"//[^\n]*|/\*.*?\*/", "", generated_cpp + generated_header, flags=re.S)
        for old, new in REQUIRED_ARCHIVED_LITERAL_MAP.items():
            assert old not in combined, f"stale archived RVA {old}"
            assert new in combined, f"missing translated RVA {new}"
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
            '0.12f*impulse',
            '-20.0f*wave',
            '9.0f*wave',
            '7.0f*wave',
        ):
            if token not in generated_cpp:
                raise AssertionError(
                    f"generated renderer missing placement-animation marker {token!r}"
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
    for path, expected in before.items():
        assert (ROOT / path).read_bytes() == expected, f"generator modified {path}"

    print("v0.2.68 1.26.51.1 build-only renderer compatibility contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

