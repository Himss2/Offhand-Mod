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
        "kSetItemInHandSlotRva = 0xF579C50",
        "kStackIsNullRva = 0xFFA0F70",
        "kPlayerIsUsingItemRva = 0xF9E8D64",
        "kItemInUseStackRva = 0xF9E8D84",
        "kStackDiffersForUseRva = 0xFFA5B04",
        "kItemStackCopyCtorRva = 0xFF9D748",
        "kItemStackDtorRva = 0x85ADF98",
        "kUseTickSelectedBlockRva = 0xF9E70B8",
        "kUseTickResumeRva = 0xF9E71B8",
        "std::array<std::uint8_t, 28> kUseTickSelectedBlockFingerprint",
        "std::array<std::uint8_t, 20> kUseTickPatchPrefix",
        "0xE0, 0x03, 0x13, 0xAA",
        "0x3C, 0x00, 0x00, 0x14",
        "levi_offhand.use_tick_selected_stack_bridge",
        "kItemStackStorageSize = 0x98",
        "kItemStackCountOffset = 0x22",
        "kItemMaxStackSizeOffset = 0xA8",
        "kItemIdOffset = 0xAA",
        "kShearsItemId = 424",
        "kItemWeakPtrOffset = 0x08",
        "kItemGetMaxUseDurationVtableOffset = 0x30",
        "kItemGetAttackDamageVtableOffset = 0x130",
        "kItemIsUseableVtableOffset = 0xB0",
        "kItemRequiresInteractVtableOffset = 0x1A8",
        "kItemUseVtableOffset = 0x290",
        "kItemCanUseAsAttackVtableOffset = 0x298",
        "kItemUseOnVtableOffset = 0x418",
        "kBaseItemUseRva = 0xFF8429C",
        "kComponentItemUseRva = 0xFDA8274",
        "kWeaponItemNoopUseRva = 0xFD66F30",
        "kBaseItemRequiresInteractRva = 0xFF87F28",
        "kComponentItemRequiresInteractRva = 0xFDAA1FC",
        "kBaseItemUseOnRva = 0xFF84B84",
        "kComponentItemUseOnRva = 0xFDA8A20",
        "itemIsShears(",
        "itemId == kShearsItemId",
        "maxStackSize == 1",
        "kMainHand = 0",
        "kOffHand = 1",
        "resolveExactTarget(",
        "resolveKnownBuildTarget(",
        "resolveHookTarget(",
        "stable guard failed offhand=%d null=%d using=%d inUse=%d differs=%d copy=%d dtor=%d",
        "pre-hooked target detected; chaining live 1.26.51.1 target",
        "std::memcmp(",
    )

    require(
        router,
        "using BaseUseItemFn = bool (*)(void*, const void*, unsigned char);",
        "using UseItemOnBlockFn = std::uint32_t (*)(",
        "unsigned char hand",
    )
    require(header, "unsigned char hand")

    require(
        router,
        "useInputRepresentsSelected(",
        "stackIsNull(input) && stackIsNull(selected)",
        "!useInputRepresentsSelected(itemStack, mainStack)",
    )
    if '#include "swap/' in router or "SwapEngine::" in router or "SwapRuntime::" in router:
        raise AssertionError(
            "RightUseRouter must consume the live OFFHAND slot without depending on swap origin"
        )

    helper = function_body(router, "useTickSelectedStackBridge(")
    require(
        helper,
        "gUseTickSelectedOriginal(player)",
        "gSessionPlayer == player",
        "activeUseMatches(player, offStack)",
        "activeUseMatches(player, mainStack)",
        "return offStack",
    )
    require(
        router,
        "applyUseTickBridgePatch(",
        "std::array<std::uint8_t, 28> patch{}",
        "pl::memory::writeBytes(",
        "std::span<const std::uint8_t>(patch.data(), patch.size())",
        "revertUseTickBridgePatch()",
        "mUseTickPatchApplied = true",
    )
    if (
        'resolveHookTarget(\n        "Inventory::getItem"' in router
        or "mInventoryGetItemHook" in router
    ):
        raise AssertionError(
            "rejected #748 global Inventory::getItem hot-path hook must not return"
        )
    if "SwapEngine" in helper or "SwapRuntime" in helper:
        raise AssertionError(
            "native long-use tick bridge must depend only on live hand/use state"
        )

    base_use = function_body(router, "RightUseRouter::baseUseItemDetour(")
    require(
        base_use,
        "useInputRepresentsSelected(itemStack, mainStack)",
        "if (!stackClaimsMainhandRightClick(mainStack))",
        "ScopedActionHand offScope(ActionHand::OffHand, ActionKind::UseAir)",
        "original(gameMode, offSnapshot.get(), kOffHand)",
        "ScopedItemStackSnapshot offSnapshot(currentOff)",
        "OFFHAND long-use session pinned until release",
        "routeUseAction(",
        "activeUseMatches(player, mainStack)",
        "activeUseMatches(player, resultingOff)",
        "original(gameMode, itemStack, hand)",
    )
    if base_use.index("if (!stackClaimsMainhandRightClick(mainStack))") > base_use.index("routeUseAction("):
        raise AssertionError(
            "attack-only/no-owner MAINHAND must be classified before generic main-first routing"
        )
    if "itemStack != mainStack" in base_use or "itemStack == mainStack" in base_use:
        raise AssertionError("1.26.51.1 routing must not use ItemStack pointer identity")

    classifier = function_body(router, "stackClaimsMainhandRightClick(")
    if "isUseable(item)" in classifier:
        raise AssertionError(
            "ComponentItem::isUseable is too broad for MAINHAND priority"
        )
    if "requiresInteract(item)" in classifier:
        raise AssertionError(
            "generic ComponentItem::requiresInteract must not claim MAINHAND priority"
        )
    require(
        classifier,
        "kItemUseVtableOffset",
        "kBaseItemUseRva",
        "kComponentItemUseRva",
        "kItemRequiresInteractVtableOffset",
        "kBaseItemRequiresInteractRva",
        "kComponentItemRequiresInteractRva",
        "kItemUseOnVtableOffset",
        "kBaseItemUseOnRva",
        "kComponentItemUseOnRva",
        "kItemGetAttackDamageVtableOffset",
        "attackDamage",
        "kItemCanUseAsAttackVtableOffset",
        "maxUseDuration",
        "attackOnly",
        "itemIsShears(item)",
    )
    if classifier.index("itemIsShears(item)") > classifier.index("attackDamage"):
        raise AssertionError(
            "Shears MAINHAND ownership must be decided before attack-only fallback"
        )
    if classifier.index("specializedUse") > classifier.index("attackDamage"):
        raise AssertionError(
            "specialized native actions must be classified before axe-like attack fallback"
        )
    if classifier.index("attackDamage") > classifier.index("maxUseDuration"):
        raise AssertionError(
            "axe-like attack fallback must run before generic max-use duration"
        )

    use_block = function_body(router, "RightUseRouter::useItemOnBlockDetour(")
    require(
        use_block,
        "stackClaimsMainhandRightClick(mainStack, &yieldedAttackOnly)",
        "ScopedItemStackSnapshot offSnapshot(offStack)",
        "std::uint32_t offResult = 0;",
        "if (instance->mUseItemOnBlockPreHooked)",
        "ScopedActionHand offScope(ActionHand::OffHand, ActionKind::UseBlock)",
        "ScopedPlayer routedPlayer(player)",
        "if ((offResult & 1u) != 0u)",
        "MAINHAND had no right-click owner; block-use/place handled by OFFHAND first",
        "OffhandPlacementAnimation::instance().trigger()",
        "liveCount = stackCount(offStack)",
        "placedCount = stackCount(offSnapshot.get())",
        "gSetItemInHandSlot(",
        "OFFHAND placement count reconciled",
    )
    if "swap" in use_block.lower() or "Packet" in use_block:
        raise AssertionError("block-use must stay on Minecraft native hand routing")
    require(use_block, "mainAttempted", "mainResult != 0u", "mainFallback()",
            "stackClaimsMainhandRightClick(mainStack, nullptr, false)")

    # Test-2 has an explicit upper OFF bridge before the historical
    # classifier.  Keep the old transaction-safety assertions scoped to the
    # legacy fallback section rather than treating the upper bridge as a
    # classifier-order violation.
    legacy_start = use_block.index(
        "// Decide whether MAINHAND genuinely owns right-click"
    )
    legacy_block = use_block[legacy_start:]
    classifier_pos = legacy_block.index(
        "stackClaimsMainhandRightClick(mainStack, &yieldedAttackOnly)"
    )
    first_off_assignment = legacy_block.index("offResult = original(")
    if classifier_pos > first_off_assignment:
        raise AssertionError(
            "legacy MAINHAND capability must be decided before legacy OFFHAND use-on"
        )

    # The legacy pre-hook compatibility path and clean native path remain
    # mutually exclusive. The upper OFF bridge is a third, separately-scoped
    # call site and is validated by the upper arbitration contract above.
    if legacy_block.count("offResult = original(") != 2:
        raise AssertionError(
            "legacy block-use must retain exactly two mutually-exclusive OFFHAND call sites"
        )
    if legacy_block.count("offSnapshot.get()") < 2:
        raise AssertionError(
            "both legacy OFFHAND call sites must use the detached ItemStack snapshot"
        )
    if legacy_block.count("kOffHand,") < 2:
        raise AssertionError(
            "both legacy OFFHAND call sites must carry native hand=1"
        )

    scope_pos = legacy_block.index(
        "ScopedActionHand offScope(ActionHand::OffHand, ActionKind::UseBlock)"
    )
    prehook_pos = legacy_block.index("if (instance->mUseItemOnBlockPreHooked)")
    else_pos = legacy_block.index("} else {", prehook_pos)
    if not (prehook_pos < scope_pos < else_pos):
        raise AssertionError(
            "legacy selected-item spoof must exist only inside the pre-hook compatibility branch"
        )

    if "gameMode,\n        offStack,\n        blockPos" in legacy_block:
        raise AssertionError(
            "legacy block placement must not pass the live offhand slot as transaction snapshot"
        )
    # test-2 arbitration invariant: one physical right-click is owned at the
    # verified upper dispatcher. Lower block/air hooks may bridge the chosen
    # hand but must not independently run the other hand during that pass.
    require(
        router,
        "kUpperUseDispatcherRva = 0x97F85F8",
        "kUpperUseDispatcherFingerprint",
        "UpperUsePass::Main",
        "UpperUsePass::Off",
        "gUpperMainClaimed",
        "RightUseRouter::upperUseDetour(",
        "upper-use MAIN claimed",
        "upper-use OFF fallback",
        "stackDeclaresNativeAirUse(",
        "kItemIsFoodVtableOffset = 0xA0",
        "kItemIsThrowableVtableOffset = 0xA8",
        "kItemIsUseableVtableOffset = 0xB0",
    )
    require(
        header,
        "mUpperUseHook",
        "mUpperUseOriginal",
        "mUpperUseTarget",
    )

    accepted_pos = legacy_block.index("if ((offResult & 1u) != 0u)")
    writeback_pos = legacy_block.index("gSetItemInHandSlot(", accepted_pos)
    trigger_pos = legacy_block.index("OffhandPlacementAnimation::instance().trigger()")
    if not (accepted_pos < writeback_pos < trigger_pos):
        raise AssertionError(
            "OFFHAND count reconciliation must occur after accepted placement and before visual trigger"
        )
    accepted_return_pos = legacy_block.index("return offResult;", accepted_pos)
    if not (accepted_pos < trigger_pos < accepted_return_pos):
        raise AssertionError(
            "placement animation must trigger only inside accepted OFFHAND block-use"
        )

    pre_offhand = legacy_block[:first_off_assignment]
    if "const std::uint32_t mainResult = original(" in pre_offhand:
        raise AssertionError(
            "MAINHAND use-on must not prime transaction before OFFHAND fallback"
        )

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

    install = function_body(router, "bool RightUseRouter::install(")
    require(
        install,
        "offhandTarget = resolveExactTarget(",
        "nullTarget = resolveExactTarget(",
        "usingTarget = resolveExactTarget(",
        "inUseTarget = resolveExactTarget(",
        "differsTarget = resolveExactTarget(",
        "copyCtorTarget = resolveExactTarget(",
        "dtorTarget = resolveExactTarget(",
        "setHandExact = resolveExactTarget(",
        "kSetItemInHandSlotRva",
        'resolveHookTarget(\n        "Player::getSelectedItem"',
        'resolveHookTarget(\n        "GameMode::useItemOnBlock"',
        "&blockUsePreHooked",
        "mUseItemOnBlockPreHooked = blockUsePreHooked",
        'resolveHookTarget(\n        "GameMode::baseUseItem"',
        'resolveHookTarget(\n        "GameMode::releaseUsingItem"',
        'resolveHookTarget(\n        "upper right-use dispatcher"',
        "mUpperUseHook = std::make_unique<pl::memory::HookHandle>",
    )
    stable_guard_pos = install.index("stable guard failed")
    hook_target_pos = install.index('resolveHookTarget(\n        "Player::getSelectedItem"')
    if stable_guard_pos > hook_target_pos:
        raise AssertionError(
            "exact stable fingerprint guard must run before live hook-target fallback"
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

    # 1.26.51.1 upper dispatcher rejects an empty MAIN stack before
    # GameMode::baseUseItem. The gate remains bridged, but test-2 must route
    # the entire OFF fallback through the upper dispatcher so ComponentItem
    # throwable/food/hold use is no longer filtered by instant-only identity.
    require(
        router,
        "kUpperAirUseGateRva = 0x97F8E48",
        "kUpperAirUseGateFingerprint",
        "kUpperAirUseGatePatchName",
        "applyUpperAirUseGatePatch",
        "revertUpperAirUseGatePatch",
        "stackSupportsInstantOffhandAirUse",
    )
    base_use = function_body(router, "bool RightUseRouter::baseUseItemDetour(")
    require(
        base_use,
        "gUpperUsePass == UpperUsePass::Main",
        "gUpperUsePass == UpperUsePass::Off",
        "activeUseMatches(player, mainStack)",
        "stackDeclaresNativeAirUse(mainStack)",
        "finishOffhandUse(",
        "original(gameMode, itemStack, kOffHand)",
    )
    require(
        install,
        "upperAirUseGateTarget = resolveExactTarget(",
        "kUpperAirUseGateRva",
        "kUpperAirUseGateFingerprint",
        "applyUpperAirUseGatePatch(upperAirUseGateTarget)",
    )

    # Server-side ItemUseInventoryTransaction::handle hard-rejects
    # hand=1 with "offhand parity experiment not enabled" before entering the
    # otherwise hand-aware container-119/source-stack validation path.
    require(
        router,
        "kOffhandParityGateRva = 0x10021264",
        "kOffhandParityGateFingerprint",
        "kOffhandParityGatePatchName",
        "kOffhandParityGateAllowBranch",
        "0x24, 0x00, 0x00, 0x14",
        "applyOffhandParityGatePatch",
        "revertOffhandParityGatePatch",
    )
    require(
        install,
        "offhandParityGateTarget = resolveExactTarget(",
        "kOffhandParityGateRva",
        "kOffhandParityGateFingerprint",
        "applyOffhandParityGatePatch(offhandParityGateTarget)",
    )
    require(
        header,
        "mOffhandParityGatePatchApplied",
    )

    require(cmake, "src/runtime/RightUseRouter.cpp")

    print("v0268/1.26.51.1 native-hand right-use source contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

