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
            "upper-use, attack-callback, and destroy-rate boundaries"
        )

    bridge = bridge_cpp.read_text(errors="replace")
    probe = probe_cpp.read_text(errors="replace")

    for token in (
        "kUpperUseDispatcherRva = 0x9432794",
        "kAttackCallbackRva = 0xEF886A8",
        "kDestroyRateContextRva = 0xF08CC44",
        "kBaseUseItemRva = 0xEF75578",
        "kReleaseUsingItemRva = 0xEF76108",
        "kPlayerGameModeGetterRva = 0xF0CD850",
        "upperUseDetour(",
        "attackCallbackDetour(",
        "destroyRateContextDetour(",
        "selectedItemDetour(",
        "releaseUsingItemDetour(",
        "currentScopedAction()",
        "currentActionSession()",
        "kDestroyRateStackOffset = 0x10",
        "kDestroyRateContextCopySize = 0x20",
        "gSemanticScopePlayer",
        "player == gSemanticScopePlayer",
        "[SemanticBridge] upper-use OFFHAND retry handled",
        "[SemanticBridge] attack callback scoped to OFFHAND",
        "[SemanticBridge] destroy-rate context redirected to OFFHAND",
        "[ActionDiag] attack selected-item bridge=OFFHAND",
    ):
        require(bridge, token, "NativeSemanticBridge")

    # The upper dispatcher must first run unchanged and retry only after the
    # native MAIN path returns unhandled. The retry owns an OFFHAND action scope,
    # while the selected-item hook supplies the actual offhand stack.
    for token in (
        "const bool mainHandled = original(controller, inputFlags, interaction, target)",
        "if (mainHandled || player == nullptr)",
        "ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir)",
        "offHandled = original(controller, inputFlags, interaction, target)",
    ):
        require(bridge, token, "NativeSemanticBridge upper-use retry")

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
        "scoped destroy-rate stack redirect, upper-use retry, deferred attack callback, "
        "release ownership, and player-owned selected-item routing"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
