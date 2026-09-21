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

# Keep exact user-tested 44a occupied exchange; no clear-both derivative.
if "mSetItemInHandSlot(player,kOffHand,mEmptyItem)" in engine.replace(" ", ""):
    raise AssertionError("do not reintroduce OFFHAND clear-both derivative")

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
