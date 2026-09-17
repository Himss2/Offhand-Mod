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
            "NativeSemanticBridge legacy source must remain available for later "
            "26.50.1 ABI revalidation"
        )

    bridge = bridge_cpp.read_text(errors="replace")
    probe = probe_cpp.read_text(errors="replace")

    # Keep the proven 1.26.45.1 implementation intact as RE reference. It is
    # deliberately not installed by the staged 26.50.1 runtime path.
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
        require(bridge, token, "NativeSemanticBridge legacy source")

    for token in (
        "const bool mainHandled = original(controller, inputFlags, interaction, target)",
        "if (mainHandled || player == nullptr)",
        "ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir)",
        "offHandled = original(controller, inputFlags, interaction, target)",
    ):
        require(bridge, token, "NativeSemanticBridge legacy upper-use retry")

    for token in (
        "std::array<std::byte, kDestroyRateContextCopySize> localContext",
        "std::memcpy(localContext.data(), context, localContext.size())",
        "localContext.data() + kDestroyRateStackOffset",
        "original(localContext.data())",
    ):
        require(bridge, token, "NativeSemanticBridge legacy destroy-rate redirect")

    for forbidden in (
        "swapMainhand",
        "swapOffhand",
        "ContainerValidation",
        "InventoryTransactionPacket",
        "ItemStackRequestAction",
    ):
        if forbidden in bridge:
            raise AssertionError(f"semantic bridge contains forbidden token {forbidden!r}")

    for token in (
        "kItemCanDestroySpecialSlot = 34",
        "CanDestroySpecialFn",
        "canDestroySpecial",
        "canDestroySpecial(item, block)",
    ):
        require(probe, token, "NativeCapabilityProbe legacy mining suitability")

    require(cmake, "src/runtime/NativeSemanticBridge.cpp", "CMakeLists.txt")
    require(cmake, "src/runtime/RightUseRouter.cpp", "CMakeLists.txt")
    require(mod, '#include "runtime/RightUseRouter.hpp"', "LeviOffhandMod")
    require(mod, "RightUseRouter::instance().install(context)", "LeviOffhandMod")

    # Do not activate old semantic attack/mining hooks against 26.50.1 until
    # their complete ABI has been independently revalidated.
    for forbidden in (
        '#include "runtime/NativeSemanticBridge.hpp"',
        "NativeSemanticBridge::instance().install(context)",
        "HandActionRouter::instance().install(context)",
    ):
        if forbidden in mod:
            raise AssertionError(
                f"26.50.1 staged runtime must not activate legacy action path {forbidden!r}"
            )

    print(
        "v0.2.68/26.50.1 semantic staging contract passed: legacy RE source retained, "
        "legacy attack/mining bridge inactive, verified RightUseRouter active"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
