#!/usr/bin/env python3
from __future__ import annotations

import json
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
    manifest_path = ROOT / "manifest.json"

    for path in (
        router_path,
        header_path,
        core_path,
        levi_path,
        cmake_path,
        manifest_path,
    ):
        if not path.exists():
            raise AssertionError(f"required 1.26.51.1 right-use file missing: {path}")

    router = router_path.read_text(errors="replace")
    header = header_path.read_text(errors="replace")
    core = core_path.read_text(errors="replace")
    levi = levi_path.read_text(errors="replace")
    cmake = cmake_path.read_text(errors="replace")
    manifest = json.loads(manifest_path.read_text(errors="replace"))

    supported_versions = set(manifest.get("minecraft_versions", []))
    required_versions = {"1.26.45.1", "1.26.51.1"}
    missing_versions = required_versions - supported_versions
    if missing_versions:
        raise AssertionError(
            "manifest missing supported Minecraft versions: "
            + ", ".join(sorted(missing_versions))
        )
    if "1.26.50.1" in supported_versions:
        raise AssertionError(
            "manifest must not claim unvalidated Minecraft version 1.26.50.1"
        )

    combined = "\n".join((router, header))
    for token, reason in PROHIBITED.items():
        if token in combined:
            raise AssertionError(f"prohibited token {token!r}: {reason}")
    if "26.50.1" in combined:
        raise AssertionError("right-use source must identify the validated binary as 1.26.51.1")

    require(
        router,
        "712509dc14ccc233e91f267937dfb46ecdcc4b68",
        "b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4",
        "kUseItemOnBlockRva = 0xF8A1CC4",
        "kBaseUseItemRva = 0xF8A285C",
        "kReleaseUsingItemRva = 0xF8A3204",
        "kSelectedItemRva = 0xF9F7824",
        "kOffhandSlotRva = 0xF579C2C",
        "kStackIsNullRva = 0xFFA0F70",
        "kPlayerIsUsingItemRva = 0xF9E8D64",
        "kItemInUseStackRva = 0xF9E8D84",
        "kStackDiffersForUseRva = 0xFFA5B04",
        "kItemStackCopyCtorRva = 0xFF9D748",
        "kItemStackDtorRva = 0x85ADF98",
        "kItemStackStorageSize = 0x98",
        "kItemWeakPtrOffset = 0x08",
        "kItemGetMaxUseDurationVtableOffset = 0x30",
        "kItemIsUseableVtableOffset = 0xB0",
        "kItemRequiresInteractVtableOffset = 0x1A8",
        "kItemUseOnVtableOffset = 0x410",
        "kBaseItemUseOnRva = 0xFF84B7C",
        "kComponentItemUseOnRva = 0xFDA89E4",
        "kMainHand = 0",
        "kOffHand = 1",
        "resolveExactTarget(",
        "std::memcmp(",
    )

    require(
        router,
        "using BaseUseItemFn = bool (*)(void*, const void*, unsigned char);",
        "using UseItemOnBlockFn = std::uint32_t (*)(",
        "unsigned char hand",
    )
    require(header, "unsigned char hand")

    base_use = function_body(router, "RightUseRouter::baseUseItemDetour(")
    require(
        base_use,
        "stacksMatch(itemStack, mainStack)",
        "routeUseAction(",
        "activeUseMatches(player, mainStack)",
        "activeUseMatches(player, offStack)",
        "original(gameMode, offStack, kOffHand)",
        "original(gameMode, itemStack, hand)",
    )
    if "itemStack != mainStack" in base_use or "itemStack == mainStack" in base_use:
        raise AssertionError("1.26.51.1 routing must not use ItemStack pointer identity")

    use_block = function_body(router, "RightUseRouter::useItemOnBlockDetour(")
    require(
        use_block,
        "const std::uint32_t mainResult = original(",
        "stackClaimsMainhandRightClick(mainStack)",
        "kOffHand",
        "offResult",
        "if ((offResult & 1u) != 0u)",
        "ScopedItemStackSnapshot offSnapshot(offStack)",
        "MAINHAND passed; block-use/place handled by OFFHAND",
        "return mainResult;",
        "const std::uint32_t offResult = original(",
        "gameMode,\n        offSnapshot.get(),\n        blockPos,\n        face,\n        hitPos,\n        kOffHand,",
    )
    if "swap" in use_block.lower() or "Packet" in use_block:
        raise AssertionError("block-use must stay on Minecraft native hand routing")
    if "if ((mainResult & 1u) != 0u)" in use_block:
        raise AssertionError(
            "generic MAINHAND wrapper success must not suppress capability-based OFFHAND fallback"
        )
    if "ScopedActionHand" in use_block or "ScopedPlayer" in use_block:
        raise AssertionError(
            "instant block placement must not spoof selectedItem/offhand scope"
        )
    if "gameMode,\n        offStack,\n        blockPos" in use_block:
        raise AssertionError(
            "block placement must not pass the live offhand slot as transaction snapshot"
        )
    if "upperUseDetour" in combined or "mUpperUseHook" in combined:
        raise AssertionError("broad upper-use replay must not be installed")
    if use_block.count("const std::uint32_t mainResult = original(") != 1:
        raise AssertionError("block-use must execute exactly one explicit MAINHAND attempt")

    route = function_body(core, "UseRouteResult routeUseAction(")
    main = route.index("ScopedActionHand scope(ActionHand::MainHand")
    off = route.index("ScopedActionHand scope(ActionHand::OffHand")
    if main > off:
        raise AssertionError("right-use order must be MAINHAND then OFFHAND")

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
            raise AssertionError(f"staged 1.26.51.1 path must not install {forbidden}")

    auto_pos = levi.index("AutoInsertRouting::instance().install(context)")
    right_pos = levi.index("RightUseRouter::instance().install(context)")
    if auto_pos > right_pos:
        raise AssertionError("baseline probe should run before right-use")
    if "return false;" in levi[auto_pos:right_pos]:
        raise AssertionError("legacy storage failure must not abort 1.26.51.1 right-use install")

    require(cmake, "src/runtime/RightUseRouter.cpp")

    print("v0268/1.26.51.1 native-hand right-use source contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
