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
    "mSetItemInHandSlot(player,kOffHand,main.get())",
    "mSetSelectedItem(player,offSnap.get())",
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

# One-empty swap synchronization:
# each native setter must receive Minecraft's canonical EMPTY_ITEM when
# clearing its own storage. Never borrow the null-like ItemStack object from
# the opposite live slot/container.
compact_engine = engine.replace(" ", "")

off_empty_start = compact_engine.index("if(offEmpty){")
main_empty_start = compact_engine.index("if(mainEmpty){", off_empty_start)
off_empty_body = compact_engine[off_empty_start:main_empty_start]

for token in (
    "mSetSelectedItem(player,mEmptyItem)",
    "mSetItemInHandSlot(player,kOffHand,main.get())",
):
    if token not in off_empty_body:
        raise AssertionError(
            f"MAIN->OFF one-empty synchronization missing {token}"
        )
if "mSetSelectedItem(player,off)" in off_empty_body:
    raise AssertionError(
        "MAIN->OFF must not clear selected storage with OFFHAND's live empty stack object"
    )

occupied_marker = "Snapshotmain(mItemStackCopyCtor,mItemStackDtor,selected);\nSnapshot"
occupied_start = compact_engine.index(occupied_marker, main_empty_start)
main_empty_body = compact_engine[main_empty_start:occupied_start]

for token in (
    "mSetItemInHandSlot(player,kOffHand,mEmptyItem)",
    "mSetSelectedItem(player,offSnap.get())",
):
    if token not in main_empty_body:
        raise AssertionError(
            f"OFF->MAIN one-empty synchronization missing {token}"
        )
if "mSetItemInHandSlot(player,kOffHand,selected)" in main_empty_body:
    raise AssertionError(
        "OFF->MAIN must not clear OFFHAND with selected-slot live empty stack object"
    )

# Preserve the exact user-tested occupied 44a exchange. The canonical OFFHAND
# clear above is valid only in the MAIN-empty branch; occupied A/B must not
# reintroduce the rejected clear-both intermediate state.
occupied_body = compact_engine[occupied_start:]
occupied_sequence = (
    "mSetSelectedItem(player,mEmptyItem)",
    "mSetItemInHandSlot(player,kOffHand,main.get())",
    "mSetSelectedItem(player,offSnap.get())",
)
positions = [occupied_body.index(token) for token in occupied_sequence]
if positions != sorted(positions):
    raise AssertionError(
        "occupied 44a order changed: MAIN empty -> OFF gets old MAIN -> MAIN gets old OFF"
    )
if "mSetItemInHandSlot(player,kOffHand,mEmptyItem)" in occupied_body:
    raise AssertionError(
        "occupied A/B swap must not use the rejected clear-both derivative"
    )

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
):
    if forbidden in runtime or forbidden in engine:
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
