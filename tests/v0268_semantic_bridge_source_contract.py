#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "runtime"


def require(text: str, token: str, where: str) -> None:
    if token not in text:
        raise AssertionError(f"{where} missing required marker {token!r}")


def main() -> int:
    bridge_hpp = RUNTIME / "NativeSemanticBridge.hpp"
    bridge_cpp = RUNTIME / "NativeSemanticBridge.cpp"
    probe_cpp = RUNTIME / "NativeCapabilityProbe.cpp"
    cmake = (ROOT / "CMakeLists.txt").read_text(errors="replace")
    mod = (ROOT / "src" / "LeviOffhandMod.cpp").read_text(errors="replace")

    if not bridge_hpp.exists() or not bridge_cpp.exists():
        raise AssertionError(
            "NativeSemanticBridge production source is required for the proven "
            "upper-use and destroy-rate boundaries"
        )

    bridge = bridge_cpp.read_text(errors="replace")
    probe = probe_cpp.read_text(errors="replace")

    for token in (
        "kUpperUseDispatcherRva = 0x9432794",
        "kDestroyRateContextRva = 0xF08CC44",
        "kBaseUseItemRva = 0xEF75578",
        "kReleaseUsingItemRva = 0xEF76108",
        "upperUseDetour(",
        "destroyRateContextDetour(",
        "selectedItemDetour(",
        "releaseUsingItemDetour(",
        "currentScopedAction()",
        "currentActionSession()",
        "kDestroyRateStackOffset = 0x10",
        "kDestroyRateContextCopySize = 0x20",
        "[SemanticBridge] upper-use OFFHAND retry handled",
        "[SemanticBridge] destroy-rate context redirected to OFFHAND",
        "[ActionDiag] attack nativeResult=",
    ):
        require(bridge, token, "NativeSemanticBridge")

    # The native destroy-rate context must be copied and edited locally. The
    # original Minecraft context/inventory may not be mutated to fake selection.
    for token in (
        "std::array<std::byte, kDestroyRateContextCopySize> localContext",
        "std::memcpy(localContext.data(), context, localContext.size())",
        "localContext.data() + kDestroyRateStackOffset",
        "original(localContext.data())",
    ):
        require(bridge, token, "NativeSemanticBridge destroy-rate redirect")

    for forbidden in (
        "swapMainhand",
        "swapOffhand",
        "ContainerValidation",
        "InventoryTransactionPacket",
        "ItemStackRequestAction",
    ):
        if forbidden in bridge:
            raise AssertionError(f"semantic bridge contains forbidden token {forbidden!r}")

    # Device evidence proved getDestroySpeed()>1 alone misclassified a real
    # Pickaxe+Stone pair. Suitability must also query native canDestroySpecial.
    for token in (
        "kItemCanDestroySpecialSlot = 34",
        "CanDestroySpecialFn",
        "canDestroySpecial",
        "canDestroySpecial(item, block)",
    ):
        require(probe, token, "NativeCapabilityProbe mining suitability")

    require(cmake, "src/runtime/NativeSemanticBridge.cpp", "CMakeLists.txt")
    require(mod, '#include "runtime/NativeSemanticBridge.hpp"', "LeviOffhandMod")
    require(mod, "NativeSemanticBridge::instance().install(context)", "LeviOffhandMod")
    require(mod, "NativeSemanticBridge::instance().setFeatureEnabled(enabled)", "LeviOffhandMod")
    require(mod, "NativeSemanticBridge::instance().uninstall(context)", "LeviOffhandMod")

    print(
        "v0.2.68 semantic bridge source contract passed: native mining suitability, "
        "scoped destroy-rate stack redirect, upper-use retry, and attack diagnostics"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
