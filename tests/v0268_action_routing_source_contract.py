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
    if not router.exists() or not probe.exists():
        raise AssertionError("v0.2.68 production router/probe source is missing")

    router_text = router.read_text(errors="replace")
    probe_text = probe.read_text(errors="replace")

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
        "exact-RVA native hand access, MAIN-first item-use, native active-stack validation"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())