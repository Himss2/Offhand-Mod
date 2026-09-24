from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

button = (ROOT / "src/ui/SwapButton.cpp").read_text(errors="replace")
runtime = (ROOT / "src/swap/SwapRuntime.cpp").read_text(errors="replace")
runtime_h = (ROOT / "src/swap/SwapRuntime.hpp").read_text(errors="replace")
engine = (ROOT / "src/swap/SwapEngine.cpp").read_text(errors="replace")
engine_h = (ROOT / "src/swap/SwapEngine.hpp").read_text(errors="replace")
router = (ROOT / "src/runtime/RightUseRouter.cpp").read_text(errors="replace")
mod = (ROOT / "src/LeviOffhandMod.cpp").read_text(errors="replace")
cmake = (ROOT / "CMakeLists.txt").read_text(errors="replace")

for token in (
    'ButtonBuilder(kButtonId, "Swap Item")',
    '.label("F")',
    'ButtonBehavior::Click',
):
    if token not in button:
        raise AssertionError(f"SwapButton missing {token}")

# UI is code-only for now and may later be replaced by image-backed styling.
if "SwapEngine" in button or "ItemStack" in button:
    raise AssertionError("SwapButton UI must not contain native swap/storage logic")

for token in (
    "kClientPreFrameTickRva=0x9803334",
    "preFrameDetour(",
    "SwapEngine::instance().selectedStack(player)",
    "SwapEngine::instance().swap(player,selected)",
    "F swap queued for MINECRAFT MAIN",
):
    if token not in runtime.replace(" ", "") and token not in runtime:
        raise AssertionError(f"SwapRuntime missing {token}")

for token in (
    "kOffhandSlotRva=0xF579C2C",
    "kStackIsNullRva=0xFFA0F70",
    "kItemStackCopyCtorRva=0xFF9D748",
    "kItemStackDtorRva=0x85ADF98",
    "kSetItemInHandSlotRva=0xF579C50",
    "kSetSelectedItemRva=0xF9F7850",
    "kEmptyItemRva=0x134C6780",
    "kPlayerSelectedStateOffset=0x570",
    "kSelectedStateFlagOffset=0xB0",
    "kSelectedStateContainerOffset=0xB8",
    "kSelectedStateIndexOffset=0x10",
    "kContainerGetItemVtableOffset=0x40",
    "selectedStack(",
    "mSetSelectedItem(player,mEmptyItem)",
    "mSetSelectedItem(player,offSnap.get())",
    "kSetOffhandRawRva=0xF579C24",
    "kStackDescriptorFromItemRva=0xFF73E8C",
    "kInventoryActionDtorRva=0x8D623A4",
    "kInventoryTransactionAddActionRva=0x1001EC24",
    "kPlayerInventoryTransactionManagerOffset=0x9B8",
    "kPlayerItemStackNetManagerOffset=0xA00",
    "kOffhandLegacyContainerId=0x77",
    "OffhandInventoryAction",
    "legacyInventoryTransactionAvailable(",
):
    if token not in engine.replace(" ", "") and token not in engine:
        raise AssertionError(f"SwapEngine missing {token}")

# RightUseRouter installs before swap and hooks Player::setSelectedItem.
# Swap must exact-validate the rest of the ABI, then accept only the known
# executable 1.26.51.1 setter RVA when its prologue is already chained.
for token in (
    "resolveSetSelectedTarget(",
    "stableBuild",
    "setSelectedChainedLive",
    "base+kSetSelectedItemRva",
    "mapped(live,PF_X)",
    "Player::setSelectedItem pre-hooked; chaining live",
):
    if token not in engine:
        raise AssertionError(
            f"SwapEngine missing hook-aware setSelected resolution: {token}"
        )

if "const auto setSel=resolve(kSetSelectedItemRva,kSetSelectedItemFingerprint)" in engine.replace(" ", ""):
    raise AssertionError(
        "SwapEngine must not require an unmodified setSelected prologue after RightUseRouter installs"
    )

setter_start = router.index("void RightUseRouter::setSelectedItemDetour")
setter_end = router.index("void RightUseRouter::releaseUsingItemDetour", setter_start)
setter_body = router[setter_start:setter_end]
if "original(player, stack);" not in setter_body:
    raise AssertionError(
        "hook-aware swap setter requires RightUseRouter setSelected detour to pass through outside scoped OFF writeback"
    )

# Critical fix: never validate/call the RightUseRouter-hooked getSelectedItem entry.
for forbidden in (
    "kSelectedItemRva",
    "kSelectedItemFingerprint",
    "gGetSelectedItem",
    "Player::getSelectedItem(",
):
    if forbidden in runtime or forbidden in engine:
        raise AssertionError(
            f"isolated swap must not depend on hooked selected-item entry: {forbidden}"
        )

# F swap must preserve the proven 44a mutation order, but the mutation
# itself now runs inside Minecraft's Player-aware client legacy predictive
# guard. OFF writes must use the public native hand setter while the negative
# request id is alive; post-settlement healer pulses and raw OFF writes are
# forbidden from the swap body.
compact_engine = engine.replace(" ", "").replace("\n", "")
swap_body = engine[engine.index("bool SwapEngine::swap"):]

for token in (
    "kTryBeginClientLegacyTransactionRva=0xF88A434",
    "ClientLegacyPredictiveGuard",
    "gTryBeginClientLegacyTransaction(player)",
    "[SwapEngine][predictive-guard]",
    "[SwapEngine][native-predictive-swap]",
):
    if token not in compact_engine and token not in engine:
        raise AssertionError(f"native predictive swap missing {token}")

for forbidden in (
    "normalizeDestinationHandWithNativeLegacyRequest(",
    "offAction.submit(player)",
    "gSetOffhandRaw(player",
    "RequestSlotInfo",
    "LegacyRequestScope",
    "makePlace(",
    "makeSwap(",
):
    if forbidden in swap_body:
        raise AssertionError(
            f"swap body reintroduced rejected/post-heal boundary: {forbidden}"
        )

guard_pos = compact_engine.index(
    "NativeClientLegacyScopenativeScope=gTryBeginClientLegacyTransaction(player)"
)
off_pos = compact_engine.index("if(offEmpty)")
if guard_pos > off_pos:
    raise AssertionError("predictive guard must open before any swap mutation")

off_empty_start = compact_engine.index("if(offEmpty){")
main_empty_start = compact_engine.index("}elseif(mainEmpty){", off_empty_start)
off_empty_body = compact_engine[off_empty_start:main_empty_start]
off_sequence=(
    "mSetSelectedItem(player,mEmptyItem)",
    "mSetItemInHandSlot(player,kOffHand,main.get())",
)
positions=[off_empty_body.index(x) for x in off_sequence]
if positions != sorted(positions):
    raise AssertionError("MAIN->OFF order must remain MAIN clear -> native OFF write")

occupied_marker = "}else{Snapshotmain("
occupied_start = compact_engine.index(occupied_marker, main_empty_start)
main_empty_body = compact_engine[main_empty_start:occupied_start]
main_sequence=(
    "mSetItemInHandSlot(player,kOffHand,mEmptyItem)",
    "mSetSelectedItem(player,offSnap.get())",
)
positions=[main_empty_body.index(x) for x in main_sequence]
if positions != sorted(positions):
    raise AssertionError("OFF->MAIN order must remain native OFF clear -> MAIN fill")

occupied_body = compact_engine[occupied_start:]
occupied_sequence=(
    "mSetSelectedItem(player,mEmptyItem)",
    "mSetItemInHandSlot(player,kOffHand,main.get())",
    "mSetSelectedItem(player,offSnap.get())",
)
positions=[occupied_body.index(x) for x in occupied_sequence]
if positions != sorted(positions):
    raise AssertionError(
        "occupied 44a order changed: MAIN clear -> native OFF write -> MAIN refill"
    )

finish_pos=compact_engine.index("constboolrequestClosed=guard.finish()")
settle_pos=compact_engine.index("constboolsettled=legacyTransactionSettled(player)",finish_pos)
if finish_pos > settle_pos:
    raise AssertionError("predictive request must close before final settlement check")

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
    "RequestSlotInfo",
    "ItemStackNetManager request bridge",
):
    if forbidden in runtime or forbidden in swap_body:
        raise AssertionError(f"swap must not synthesize {forbidden}")

for forbidden in (
    '#include "swap/SwapRuntime.hpp"',
    '#include "swap/SwapEngine.hpp"',
    "SwapRuntime::instance()",
    "SwapEngine::instance()",
):
    if forbidden in router:
        raise AssertionError(
            f"RightUseRouter must remain independent from swap: {forbidden}"
        )

right_use_install = mod.index("RightUseRouter::instance().install(context)")
swap_install = mod.index("SwapRuntime::instance().install(context)")
if right_use_install > swap_install:
    raise AssertionError("RightUseRouter must install before optional swap runtime")

if "SwapRuntime::instance().requestSwap()" not in mod:
    raise AssertionError("SwapButton callback must stay queue-only")

for path in (
    "src/swap/SwapRuntime.cpp",
    "src/swap/SwapEngine.cpp",
    "src/ui/SwapButton.cpp",
):
    if path not in cmake:
        raise AssertionError(f"CMake missing {path}")

if "src/runtime/OffhandSwapRuntime.cpp" in cmake:
    raise AssertionError("legacy mixed swap runtime must stay uncompiled")

print("v0.2.68 isolated swap UI/runtime/engine contract passed")

# Native 1.26.51.1 client legacy request normalization discovered from #659.
for marker in (
    "kTryBeginClientLegacyRequestRva=0xF88D960",
    "[SwapEngine][native-client-normalize] MAIN->OFF",
    "[SwapEngine][native-client-normalize] OFF->MAIN",
    "[SwapEngine][native-client-legacy] hand=",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(f"native client legacy normalization missing {marker}")

swap_body = engine[engine.index("bool SwapEngine::swap"):]
compact = swap_body.replace(" ", "").replace("\n", "")
off_start = compact.index("if(offEmpty){")
main_start = compact.index("if(mainEmpty){", off_start)
occ_start = compact.index("Snapshotmain(", main_start)
off_body = compact[off_start:main_start]
main_body = compact[main_start:occ_start]
occupied_body = compact[occ_start:]

for body, sequence in (
    (off_body, (
        "mSetSelectedItem(player,mEmptyItem)",
        "offAction.submit(player)",
        "gSetOffhandRaw(player,main.get())",
        "legacyTransactionSettled(player)",
        "normalizeDestinationHandWithNativeLegacyRequest(",
    )),
    (main_body, (
        "offAction.submit(player)",
        "gSetOffhandRaw(player,mEmptyItem)",
        "mSetSelectedItem(player,offSnap.get())",
        "legacyTransactionSettled(player)",
        "normalizeDestinationHandWithNativeLegacyRequest(",
    )),
):
    positions=[body.index(x) for x in sequence]
    if positions != sorted(positions):
        raise AssertionError("native client legacy normalization must run after #630 settlement")

if "normalizeDestinationHandWithNativeLegacyRequest(" in occupied_body:
    raise AssertionError("occupied #630 path must remain untouched")
