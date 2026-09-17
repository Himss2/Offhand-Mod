#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "runtime"

PROHIBITED = {
    "ContainerValidation": "right-use must not hook ContainerValidation",
    "swapMainhand": "physical stack swapping is forbidden",
    "swapOffhand": "physical stack swapping is forbidden",
    "swapHeldStack": "physical stack swapping is forbidden",
    "ItemStackRequestAction": "do not synthesize inventory packets",
    "InventoryTransactionPacket": "do not synthesize inventory packets",
}


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


def require(text: str, *tokens: str) -> None:
    for token in tokens:
        if token not in text:
            raise AssertionError(f"missing required marker {token!r}")


def main() -> int:
    router_path = RUNTIME / "RightUseRouter.cpp"
    header_path = RUNTIME / "RightUseRouter.hpp"
    core_path = RUNTIME / "HandActionRouterCore.hpp"
    levi_path = ROOT / "src" / "LeviOffhandMod.cpp"
    cmake_path = ROOT / "CMakeLists.txt"

    for path in (router_path, header_path, core_path, levi_path, cmake_path):
        if not path.exists():
            raise AssertionError(f"required 26.50.1 right-use file missing: {path}")

    router = router_path.read_text(errors="replace")
    header = header_path.read_text(errors="replace")
    core = core_path.read_text(errors="replace")
    levi = levi_path.read_text(errors="replace")
    cmake = cmake_path.read_text(errors="replace")

    combined = "\n".join((router, header))
    for token, reason in PROHIBITED.items():
        if token in combined:
            raise AssertionError(f"prohibited token {token!r}: {reason}")

    require(
        router,
        "712509dc14ccc233e91f267937dfb46ecdcc4b68",
        "b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4",
        "kBaseUseItemRva = 0xF8A285C",
        "kReleaseUsingItemRva = 0xF8A3204",
        "kSelectedItemRva = 0xF9F7824",
        "kOffhandSlotRva = 0xF579C2C",
        "kStackIsNullRva = 0xFFA0F70",
        "kPlayerIsUsingItemRva = 0xF9E8D64",
        "kItemInUseStackRva = 0xF9E8D84",
        "kStackDiffersForUseRva = 0xFFA5B04",
        "resolveExactTarget(",
        "std::memcmp(",
    )

    require(
        router,
        "using BaseUseItemFn = bool (*)(void*, const void*, unsigned char);",
        "unsigned char useContext",
        "original(gameMode, itemStack, useContext)",
        "original(gameMode, offStack, useContext)",
    )
    require(header, "unsigned char useContext")

    base_use = function_body(router, "RightUseRouter::baseUseItemDetour(")
    require(
        base_use,
        "stacksMatch(itemStack, mainStack)",
        "routeUseAction(",
        "original(gameMode, offStack, useContext)",
        "original(gameMode, itemStack, useContext)",
    )
    if "itemStack != mainStack" in base_use or "itemStack == mainStack" in base_use:
        raise AssertionError("26.50.1 routing must not use ItemStack pointer identity")

    route = function_body(core, "UseRouteResult routeUseAction(")
    off = route.index("ScopedActionHand scope(ActionHand::OffHand")
    main = route.index("ScopedActionHand scope(ActionHand::MainHand")
    if off > main:
        raise AssertionError("right-use order must be OFFHAND then MAINHAND")

    require(
        router,
        "gSessionPlayer",
        "gSessionGameMode",
        "gPlayerIsUsingItem(player)",
        "gItemInUseStack(player)",
        "stacksMatch(active, offStack)",
        "RightUseRouter::releaseUsingItemDetour",
    )

    require(
        levi,
        "RightUseRouter::instance().install(context)",
        "left-click remains native mainhand",
    )
    for forbidden in (
        "HandActionRouter::instance().install(context)",
        "NativeSemanticBridge::instance().install(context)",
    ):
        if forbidden in levi:
            raise AssertionError(f"staged 26.50.1 path must not install {forbidden}")

    auto_pos = levi.index("AutoInsertRouting::instance().install(context)")
    right_pos = levi.index("RightUseRouter::instance().install(context)")
    if auto_pos > right_pos:
        raise AssertionError("baseline probe should run before right-use")
    if "return false;" in levi[auto_pos:right_pos]:
        raise AssertionError("legacy storage failure must not abort 26.50.1 right-use install")

    require(cmake, "src/runtime/RightUseRouter.cpp")

    print("v0268/26.50.1 right-use source contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
