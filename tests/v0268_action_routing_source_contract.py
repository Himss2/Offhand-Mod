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
}


def action_sources() -> list[Path]:
    paths: set[Path] = set()
    for pattern in ACTION_GLOBS:
        paths.update(RUNTIME.glob(pattern))
    return sorted(paths)


def main() -> int:
    sources = action_sources()
    combined = "\n".join(path.read_text(errors="replace") for path in sources)

    for token, reason in PROHIBITED.items():
        if token in combined:
            raise AssertionError(f"prohibited action-routing token {token!r}: {reason}")

    router = RUNTIME / "HandActionRouter.cpp"
    if router.exists():
        router_text = router.read_text(errors="replace")
        required = (
            "ScopedActionHand",
            "mOriginal",
            "featureEnabled",
        )
        for token in required:
            if token not in router_text:
                raise AssertionError(
                    f"HandActionRouter exists but required scoped/fallback marker {token!r} is absent"
                )

    print(
        "v0.2.68 action routing source contract passed: "
        "no forbidden swap/ContainerValidation/synthetic-packet architecture"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
