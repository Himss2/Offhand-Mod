#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "runtime"
ACTION_GLOBS = (
    "HandAction*.hpp",
    "HandAction*.cpp",
    "ActionHandContext*.hpp",
    "ActionHandContext*.cpp",
    "NativeCapabilityProbe*.hpp",
    "NativeCapabilityProbe*.cpp",
)

PROHIBITED = {
    "ContainerValidation": "gameplay action routing must not hook ContainerValidation",
    "swapMainhand": "physical main/offhand stack swapping is forbidden",
    "swapOffhand": "physical main/offhand stack swapping is forbidden",
    "swapHeldStack": "physical held-stack swapping is forbidden",
    "ItemStackRequestAction": "do not synthesize inventory/action packets for routing",
    "InventoryTransactionPacket": "do not synthesize inventory/action packets for routing",
    "minecraft:sword": "capability routing must not use item-name tables",
    "minecraft:bow": "capability routing must not use item-name tables",
    "minecraft:pickaxe": "capability routing must not use item-name tables",
}


def action_sources() -> list[Path]:
    paths: set[Path] = set()
    for pattern in ACTION_GLOBS:
        paths.update(RUNTIME.glob(pattern))
    return sorted(paths)


def function_body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unclosed function {marker!r}")


def main() -> int:
    sources = action_sources()
    combined = "\n".join(path.read_text(errors="replace") for path in sources)

    for token, reason in PROHIBITED.items():
        if token in combined:
            raise AssertionError(f"prohibited action-routing token {token!r}: {reason}")

    router = RUNTIME / "HandActionRouter.cpp"
    probe = RUNTIME / "NativeCapabilityProbe.cpp"
    core = RUNTIME / "HandActionRouterCore.hpp"
    if not router.exists() or not probe.exists() or not core.exists():
        raise AssertionError("v0.2.68 production router/probe/core source is missing")

    router_text = router.read_text(errors="replace")
    probe_text = probe.read_text(errors="replace")
    core_text = core.read_text(errors="replace")

    # Runtime regression: the GameMode entry prologues used by this exact
    # binary occur many times, so HandActionRouter must never perform a global
    # resolveSignature() scan and then hope the first match has the right RVA.
    # Resolve module-base + exact RVA and verify bytes in-place, exactly like
    # NativeCapabilityProbe already does.
    if "pl::memory::resolveSignature(" in router_text:
        raise AssertionError(
            "HandActionRouter must not use ambiguous global signature scanning; "
            "resolve exact RVA + fingerprint instead"
        )
    for token in (
        "minecraftModuleBase(",
        "std::memcmp(",
        "resolveExactTarget(",
    ):
        if token not in router_text:
            raise AssertionError(f"HandActionRouter exact-RVA resolver missing {token!r}")

    # Temporary low-volume device diagnostics are required until the first
    # device run proves which semantic layer is failing. They live in the pure
    # router core so reaching these markers proves the native detour made it
    # past its precondition checks. Host tests compile the non-Android branch.
    for token in (
        "[ActionDiag] use route",
        "[ActionDiag] attack route mainReal=%d offReal=%d",
        "[ActionDiag] mining route mainSuitable=%d offSuitable=%d",
    ):
        if token not in core_text:
            raise AssertionError(f"runtime action diagnostic missing {token!r}")

    for token in (
        "kBaseUseItemRva = 0xEF75578",
        "kReleaseUsingItemRva = 0xEF76108",
        "baseUseItemDetour(",
        "selectedItemDetour(",
        "releaseUsingItemDetour(",
        "routeUseAction(",
        "ScopedActionHand",
        "currentActionSession()",
        "featureEnabled",
        "mOriginal",
    ):
        if token not in router_text:
            raise AssertionError(f"HandActionRouter missing required marker {token!r}")

    selected = function_body(router_text, "HandActionRouter::selectedItemDetour(")
    for token in (
        "currentScopedAction()",
        "ActionHand::OffHand",
        "currentActionSession()",
        "offhandStackForPlayer",
        "itemInUseStack",
        "stackMatchesForUse",
    ):
        if token not in selected:
            raise AssertionError(f"selected-item detour is not native/scope-guarded: missing {token!r}")
    if "if (!offhandOwned)" not in selected or "return original(player);" not in selected:
        raise AssertionError("selected-item detour lacks explicit vanilla passthrough")

    base_use = function_body(router_text, "HandActionRouter::baseUseItemDetour(")
    if base_use.index("original(gameMode, mainStack)") > base_use.index("original(gameMode, offStack)"):
        raise AssertionError("right-click order must remain MAIN then OFF")
    if "playerIsUsingItem(player)" not in base_use:
        raise AssertionError("long-use sessions must be entered only from native Player use state")

    required_probe_markers = (
        "kSelectedItemRva = 0xF0B900C",
        "kOffhandSlotRva = 0xEC9D62C",
        "kStackIsNullRva = 0xF63E760",
        "kPlayerIsUsingItemRva = 0xF0B8094",
        "kItemInUseStackRva = 0xF0B80B4",
        "kStackDiffersForUseRva = 0xF6443F4",
        "kGameModePlayerOffset = sizeof(void*)",
        "resolveExactTarget",
        "validatePlayerObject",
        "belongsToMinecraft",
    )
    for token in required_probe_markers:
        if token not in probe_text:
            raise AssertionError(f"NativeCapabilityProbe missing exact ABI marker {token!r}")

    # Combat capability comes from the Item virtual getAttackDamage slot, not
    # from item identifiers or generic punch success.
    for token in (
        "kAttackRva = 0xEF721E4",
        "attackDetour(",
        "routeAttackAction(",
    ):
        if token not in router_text:
            raise AssertionError(f"native attack router missing required marker {token!r}")

    attack = function_body(router_text, "HandActionRouter::attackDetour(")
    for token in (
        "realCombatCapability(mainStack)",
        "realCombatCapability(offStack)",
        "ScopedRoutedPlayer",
        "routeAttackAction(",
        "original(gameMode, entity, playPredictiveSound, hitPosition)",
    ):
        if token not in attack:
            raise AssertionError(f"attack detour missing capability/fallback marker {token!r}")

    for token in (
        "kItemStackItemOffset = sizeof(void*)",
        "kItemGetAttackDamageSlot = 38",
        "itemFromStack(",
        "realCombatCapability(",
    ):
        if token not in probe_text:
            raise AssertionError(f"native combat probe missing exact ABI marker {token!r}")

    # Mining must pin the chosen stack for the complete native GameMode
    # destroy lifecycle. Suitability is target-sensitive and obtained from
    # Item::getDestroySpeed(stack, block), never from item-name tables.
    for token in (
        "kStartDestroyBlockRva = 0xEF72684",
        "kDestroyBlockRva = 0xEF72C18",
        "kContinueDestroyBlockRva = 0xEF72F9C",
        "kStopDestroyBlockRva = 0xEF7398C",
        "startDestroyBlockDetour(",
        "destroyBlockDetour(",
        "continueDestroyBlockDetour(",
        "stopDestroyBlockDetour(",
        "routeMiningStart(",
        "ActionSessionKind::Mining",
    ):
        if token not in router_text:
            raise AssertionError(f"native mining router missing required marker {token!r}")

    start_mining = function_body(router_text, "HandActionRouter::startDestroyBlockDetour(")
    for token in (
        "blockAt(player, blockPos)",
        "realMiningCapability(mainStack, targetBlock)",
        "realMiningCapability(offStack, targetBlock)",
        "routeMiningStart(",
        "ActionSessionKind::Mining",
        "stackItemIdentity(offStack)",
        "targetIdentityForBlockPos(blockPos)",
    ):
        if token not in start_mining:
            raise AssertionError(f"start-destroy detour missing mining marker {token!r}")

    for marker in (
        "HandActionRouter::continueDestroyBlockDetour(",
        "HandActionRouter::destroyBlockDetour(",
        "HandActionRouter::stopDestroyBlockDetour(",
    ):
        body = function_body(router_text, marker)
        for token in (
            "ActionSessionKind::Mining",
            "ActionHand::OffHand",
            "targetIdentityForBlockPos(blockPos)",
            "stackItemIdentity(offStack)",
            "ScopedRoutedPlayer",
        ):
            if token not in body:
                raise AssertionError(f"{marker} missing pinned mining lifecycle marker {token!r}")

    if "session.finish()" not in function_body(router_text, "HandActionRouter::destroyBlockDetour("):
        raise AssertionError("destroyBlock must finish the pinned mining session")
    if "session.cancel()" not in function_body(router_text, "HandActionRouter::stopDestroyBlockDetour("):
        raise AssertionError("stopDestroyBlock must cancel the pinned mining session")

    selected = function_body(router_text, "HandActionRouter::selectedItemDetour(")
    for token in (
        "ActionSessionKind::Mining",
        "stackItemIdentity(offStack)",
    ):
        if token not in selected:
            raise AssertionError(f"selected-item detour lacks mining-session validation {token!r}")

    for token in (
        "kActorBlockSourceRva = 0xEC844CC",
        "kBlockSourceGetBlockSlot = 2",
        "kItemGetDestroySpeedSlot = 89",
        "blockAt(",
        "realMiningCapability(",
        "stackItemIdentity(",
    ):
        if token not in probe_text:
            raise AssertionError(f"native mining probe missing exact ABI marker {token!r}")

    for forbidden in (
        "dlsym(",
        "_ZNK6Player15getSelectedItemEv",
        "_ZNK5Actor14getOffhandSlotEv",
        "_ZNK13ItemStackBase6isNullEv",
        "_ZNK13ItemStackBase5getIdEv",
        "_ZNK6Player11isUsingItemEv",
        "stackItemId(",
    ):
        if forbidden in probe_text or forbidden in router_text:
            raise AssertionError(
                f"v0.2.68 must not depend on unavailable exported accessor {forbidden!r}"
            )

    print(
        "v0.2.68 action routing source contract passed: "
        "exact-RVA hand/action access, MAIN-first item-use, native active-stack validation, "
        "native combat routing, target-sensitive pinned mining lifecycle, and device diagnostics"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())