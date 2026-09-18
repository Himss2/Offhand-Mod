#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path


RVA_REPLACEMENTS = {
    # Core held-item render pipeline.
    "0xADDEA08": "0xB2F0F60",
    "0xA1DEE04": "0xA619618",
    "0x94E96F4": "0x98B49A0",
    "0xF63BE90": "0xFF86E80",
    "0xF644970": "0xFFA6140",
    "0xADEA0BC": "0xB2FC0BC",
    "0xA31662C": "0xA79F2C0",
    "0xADE9E9C": "0xB2FBE9C",
    "0xEC9D62C": "0xF579CA4",
    "0xA32F030": "0xA6FFAE0",
    "0xADDEADC": "0xB2F1034",
    "0xA32F0F4": "0xA6FFBA4",

    # Native attachment pipeline.
    "0x9B36A80": "0x9F013F0",
    "0xF147CB0": "0xFA51F60",
    "0x9B37780": "0x9F020F0",
    "0x9B377D8": "0x9F02148",
    "0xAF3A1E4": "0xB42719C",
    "0x9B3779C": "0x9F0210C",
    "0x9B37814": "0x9F02184",
    "0x9B3A228": "0x9F03EAC",
    "0xF147ED0": "0xFA528A4",
    "0x9B254C8": "0x9F5A6D0",
    "0x107CC804": "0x110AFF88",
    "0xADE56D8": "0xB2F7ADC",

    # Item singleton globals.  The 1.26.51.1 item registry preserves the
    # relative layout of this group while the group moved by +0xDCDF80.
    "0x126F0FB8": "0x134BEF38",  # bow
    "0x126F0FE0": "0x134BEF60",  # crossbow
    "0x126F11C0": "0x134BF140",  # trident
    "0x126F26E8": "0x134C0668",  # fishing rod
    "0x126F4858": "0x134C27D8",  # copper spear
    "0x126F4B00": "0x134C2A80",  # diamond spear
    "0x126F50C8": "0x134C3048",  # golden spear
    "0x126F53E8": "0x134C3368",  # iron spear
    "0x126F5898": "0x134C3818",  # netherite spear
    "0x126F6130": "0x134C40B0",  # stone spear
    "0x126F6590": "0x134C4510",  # wooden spear
}


NAMED_RVA_REPLACEMENTS = {
    # Core targets used by install()/validation.  Replace by symbol name so
    # archived v0.2.67 renderer snapshots cannot retain a stale RVA merely
    # because their old literal differs from the current tracked source.
    "kRenderItemRva": "0xB2F0F60",
    "kDefaultTransformRva": "0xA619618",
    "kMatrixMultiplyRva": "0x98B49A0",
    "kItemStackMatchesRva": "0xFF86E80",
    "kHandEquipPredicateRva": "0xFFA6140",
    "kOffDispatchCallsiteRva": "0xB2FC0BC",
    "kFirstPersonDataDrivenRenderRva": "0xA79F2C0",
    "kFirstPersonDataDrivenCallsiteRva": "0xB2FBE9C",
    "kGetOffhandStackRva": "0xF579CA4",
    "kThirdPersonOffhandRenderItemCallsiteRva": "0xA6FFAE0",
    "kRenderItemAttachableEnabledCallsiteRva": "0xB2F1034",
    "kAttachableStateRva": "0xA6FFBA4",

    # Native attachment pipeline.
    "kPrepareAttachmentRva": "0x9F013F0",
    "kAttachmentBindingModeRva": "0xFA51F60",
    "kAttachmentBindingModeFirstCallsiteRva": "0x9F020F0",
    "kAttachmentBindingModeSecondCallsiteRva": "0x9F02148",
    "kResolveOwnerBoneByNameRva": "0xB42719C",
    "kResolveOwnerBoneFirstCallsiteRva": "0x9F0210C",
    "kResolveOwnerBoneSecondCallsiteRva": "0x9F02184",
    "kDrawAttachmentRva": "0x9F03EAC",
    "kComposeAttachmentBoneMatrixRva": "0xFA528A4",
    "kComposeAttachmentBoneMatrixCallsiteRva": "0x9F5A6D0",
    "kFinalOffhandMatrixTopRva": "0x110AFF88",
    "kFinalOffhandMatrixReturnRva": "0xB2F7ADC",

    # Item singleton globals.
    "kBowIdRva": "0x134BEF38",
    "kCrossbowIdRva": "0x134BEF60",
    "kTridentIdRva": "0x134BF140",
    "kFishingRodIdRva": "0x134C0668",
    "kCopperSpearIdRva": "0x134C27D8",
    "kDiamondSpearIdRva": "0x134C2A80",
    "kGoldenSpearIdRva": "0x134C3048",
    "kIronSpearIdRva": "0x134C3368",
    "kNetheriteSpearIdRva": "0x134C3818",
    "kStoneSpearIdRva": "0x134C40B0",
    "kWoodenSpearIdRva": "0x134C4510",
}


SIGNATURE_REPLACEMENTS = {
    "kBlockPredicateSignature": bytes.fromhex(
        "FD 7B BE A9 "
        "F3 0B 00 F9 "
        "FD 03 00 91 "
        "F3 03 00 AA "
        "79 5E CC 97 "
        "A0 00 00 36 "
        "E0 03 1F 2A "
        "F3 0B 40 F9 "
        "FD 7B C2 A8 "
        "C0 03 5F D6 "
        "60 E2 01 91 "
        "F3 0B 40 F9 "
        "FD 7B C2 A8 "
        "27 DA 4B 15"
    ),
    "kCanTessellateSignature": bytes.fromhex(
        "FD 7B BE A9 "
        "F4 4F 01 A9 "
        "FD 03 00 91 "
        "F3 03 00 AA "
        "E5 A6 32 95 "
        "E0 01 00 B4 "
        "75 94 CD 97 "
        "C3 97 CD 97 "
        "F4 03 00 2A "
        "E0 03 13 AA "
        "C4 A6 32 95 "
        "C0 00 00 B4 "
        "E0 03 14 2A "
        "84 36 CF 97"
    ),
    "kRenderObjectSignature": bytes.fromhex(
        "FD 7B BA A9 "
        "FC 6F 01 A9 "
        "FA 67 02 A9 "
        "F8 5F 03 A9 "
        "F6 57 04 A9 "
        "F4 4F 05 A9 "
        "FD 03 00 91 "
        "FF 43 08 D1 "
        "E3 0F 00 F9 "
        "5A D0 3B D5"
    ),
    "kHandEquipPredicateSignature": bytes.fromhex(
        "08 04 40 F9 "
        "68 02 00 B4 "
        "FD 7B BD A9 "
        "F5 0B 00 F9 "
        "F4 4F 02 A9 "
        "FD 03 00 91 "
        "08 01 40 F9 "
        "08 06 00 B4 "
        "09 01 40 F9 "
        "F3 03 00 AA "
        "E0 03 08 AA "
        "29 2D 40 F9 "
        "20 01 3F D6"
    ),
}


# These two signatures remained byte-for-byte stable in 1.26.51.1.  Keeping
# them listed here makes the compatibility boundary explicit and lets the
# generator fail if a future source snapshot no longer contains the same
# semantic hook names.
UNCHANGED_SIGNATURES = {
    "kRenderOffhandSignature",
    "kItemTransformSignature",
}


def format_signature(name: str, raw: bytes) -> str:
    chunks = [raw[index:index + 4] for index in range(0, len(raw), 4)]
    lines = [f"        constexpr char {name}[]=\n"]
    for index, chunk in enumerate(chunks):
        text = " ".join(f"{byte:02X}" for byte in chunk)
        suffix = " " if index + 1 < len(chunks) else ""
        terminator = ";" if index + 1 == len(chunks) else ""
        lines.append(f'            "{text}{suffix}"{terminator}\n')
    return "".join(lines)


def replace_signature(source: str, name: str, raw: bytes) -> str:
    pattern = re.compile(
        rf"[ \t]*constexpr\s+char\s+{re.escape(name)}\[\]\s*=\s*"
        rf"(?:(?:\"[^\"]*\")\s*)+;",
        re.MULTILINE,
    )
    match = pattern.search(source)
    if match is None:
        raise RuntimeError(f"renderer source missing signature {name}")
    return source[:match.start()] + format_signature(name, raw).rstrip("\n") + source[match.end():]


def require_signature_name(source: str, name: str) -> None:
    if f"constexpr char {name}[]" not in source:
        raise RuntimeError(f"renderer source missing unchanged signature {name}")


def replace_rvas(source: str) -> str:
    for old, new in RVA_REPLACEMENTS.items():
        # Historical renderer snapshots do not all use every target.  Replace
        # literals first for callsites/header constants that do not have a
        # stable C++ identifier across snapshots.
        source = source.replace(old, new)

    # Critical runtime constants are replaced by identifier as well.  This is
    # intentionally independent of the old literal value: the CI renderer is
    # reconstructed from archived overlays, so its pre-update RVA can differ
    # from the copy tracked at HEAD.
    for name, new in NAMED_RVA_REPLACEMENTS.items():
        pattern = re.compile(
            rf"(\\b{re.escape(name)}\\s*=\\s*)0x[0-9A-Fa-f]+"
        )
        source, count = pattern.subn(rf"\\g<1>{new}", source)
        if count > 1:
            raise RuntimeError(f"renderer source has duplicate RVA constant {name}")

    return source


def compatibility_audit_comment() -> str:
    lines = [
        "// GENERATED FILE - DO NOT EDIT.\n",
        "// Minecraft Bedrock 1.26.51.1 renderer compatibility target map.\n",
        "// The tracked src/render implementation remains unchanged; only\n",
        "// binary targets/signatures are translated for this build.\n",
    ]
    for old, new in RVA_REPLACEMENTS.items():
        lines.append(f"//   {old} -> {new}\n")
    lines.append("\n")
    return "".join(lines)


def translate_cpp(source: str) -> str:
    source = source.replace("1.26.45.1", "1.26.51.1")
    source = replace_rvas(source)
    for name in UNCHANGED_SIGNATURES:
        require_signature_name(source, name)
    for name, raw in SIGNATURE_REPLACEMENTS.items():
        source = replace_signature(source, name, raw)
    return compatibility_audit_comment() + source


def translate_header(source: str) -> str:
    source = source.replace("1.26.45.1", "1.26.51.1")
    return replace_rvas(source)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the 1.26.51.1 renderer compatibility copy without editing src/render"
    )
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    output_root = args.output_root.resolve()
    source_render = source_root / "src" / "render"
    output_render = output_root / "render"

    cpp_path = source_render / "OffhandBlockRenderPatch.cpp"
    fix_path = source_render / "NativeAttachmentFix.hpp"
    public_header_path = source_render / "OffhandBlockRenderPatch.hpp"
    for path in (cpp_path, fix_path, public_header_path):
        if not path.is_file():
            raise RuntimeError(f"missing renderer source: {path}")

    output_render.mkdir(parents=True, exist_ok=True)

    cpp = translate_cpp(cpp_path.read_text(encoding="utf-8"))
    fix = translate_header(fix_path.read_text(encoding="utf-8"))

    (output_render / cpp_path.name).write_text(cpp, encoding="utf-8")
    (output_render / fix_path.name).write_text(fix, encoding="utf-8")
    shutil.copyfile(public_header_path, output_render / public_header_path.name)

    print(f"Generated Minecraft 1.26.51.1 renderer compatibility source: {output_render}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
