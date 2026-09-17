#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = [
    ROOT / "src" / "LeviOffhandMod.cpp",
    ROOT / "src" / "runtime" / "NativeOffhandPolicy.cpp",
    ROOT / "src" / "runtime" / "NativeOffhandPolicy.hpp",
    ROOT / "src" / "runtime" / "AutoInsertRouting.cpp",
    ROOT / "src" / "runtime" / "AutoInsertRouting.hpp",
    ROOT / "src" / "runtime" / "AutoInsertRoutingCore.hpp",
    ROOT / "CMakeLists.txt",
]

forbidden = (
    "ContainerScreenValidation::tryTransfer",
    "ContainerScreenValidation::trySwap",
    "kManualSetPathSignature",
    "kTryTransferSignature",
    "kTrySwapSignature",
    "allowOffhandDetour",
    "gManualSetDepth",
    "gTransferDepth",
    "gAutoAddDepth",
    "OffhandValidationHook",
)

required = {
    "src/runtime/NativeOffhandPolicy.cpp": (
        # 1.26.45.1 verified legacy policy.
        "0xF65A3BC",
        "09 0A 80 52",
        "09 1A 80 52",
        # 1.26.51.1 moved allow-offhand semantics to Item+0x1C8 and the
        # ItemStackBase native query at this exact RVA.
        "kAllowOffhandQueryRva126511 = 0xFFA60F0",
        "08 21 47 39",
        "20 00 80 52 C0 03 5F D6",
        "kAllowOffhandQuerySignature126511",
        "kPatchedAllowQueryPrefix",
        "levi_offhand.item_allow_offhand",
    ),
    "src/runtime/AutoInsertRouting.cpp": (
        "0xF024024",
        "filterDestinations(",
        "original(",
        "std::nothrow",
    ),
    "src/runtime/AutoInsertRoutingCore.hpp": (
        "kOffhandContainer = 34",
        "kDestinationRecordSize = 0x20",
        "filterDestinations(",
        "alignas(8)",
    ),
}

texts: dict[str, str] = {}
for path in PRODUCTION:
    assert path.exists(), f"missing production file: {path.relative_to(ROOT)}"
    texts[str(path.relative_to(ROOT))] = path.read_text(encoding="utf-8")

joined = "\n".join(texts.values())
for token in forbidden:
    assert token not in joined, f"legacy storage token remains: {token}"

for rel, tokens in required.items():
    text = texts[rel]
    for token in tokens:
        assert token in text, f"missing {token!r} in {rel}"

policy_text = texts["src/runtime/NativeOffhandPolicy.cpp"]
assert "0xFF7D070" not in policy_text, (
    "1.26.51.1 must not treat the obsolete Item+0x112 constructor flag as allow-offhand"
)
assert "Item+0x1C8" in policy_text
assert "isCurrentQueryTarget(current)" in policy_text

assert not (ROOT / "src/runtime/OffhandValidationHook.cpp").exists()
assert not (ROOT / "src/runtime/OffhandValidationHook.hpp").exists()
print("v0.2.62 native routing source contract passed")

mod_text = texts["src/LeviOffhandMod.cpp"]
assert mod_text.index("AutoInsertRouting::instance().install(context)") < mod_text.index(
    "NativeOffhandPolicy::instance().install(context)"
), "routing guard probe must run before native capability"

disable_start = mod_text.index("bool disable(pl::mod::ModContext& context)")
unload_start = mod_text.index("bool unload(pl::mod::ModContext& context)")
disable_body = mod_text[disable_start:unload_start]

# v0.2.68 keeps installed components alive until unload, but disables them
# through the local singleton references established in disable().  Accept the
# current spelling instead of requiring the older fully-qualified call form.
assert "AutoInsertRouting::instance().uninstall(context)" not in disable_body, (
    "disable must retain routing guard until unload"
)
assert "NativeOffhandPolicy::instance().uninstall(context)" not in disable_body, (
    "disable must revert through feature toggle, not tear down storage state"
)
assert "auto& autoInsert=runtime::AutoInsertRouting::instance();" in disable_body
assert "if(autoInsert.installed())" in disable_body
assert "autoInsert.setFeatureEnabled(false);" in disable_body
assert "auto& policy=runtime::NativeOffhandPolicy::instance();" in disable_body
assert "if(policy.installed())" in disable_body
assert "policy.setFeatureEnabled(false);" in disable_body
print("v0.2.62 lifecycle fail-safe contract passed")
