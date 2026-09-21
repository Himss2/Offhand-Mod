from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

button = (ROOT / "src/ui/SwapButton.cpp").read_text(errors="replace")
runtime = (ROOT / "src/runtime/OffhandSwapRuntime.cpp").read_text(errors="replace")
header = (ROOT / "src/runtime/OffhandSwapRuntime.hpp").read_text(errors="replace")
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

for token in (
    "kOffhandSlotRva = 0xF579C2C",
    "kStackIsNullRva = 0xFFA0F70",
    "kItemStackCopyCtorRva = 0xFF9D748",
    "kItemStackDtorRva = 0x85ADF98",
    "kSetItemInHandSlotRva = 0xF579C50",
    "kSetSelectedItemRva = 0xF9F7850",
    "kSetSelectedItemFingerprint",
    "kItemStackStorageSize = 0x98",
    "kClientPreFrameTickRva = 0x9803334",
    "kSelectedItemRva = 0xF9F7824",
    "kEmptyItemRva = 0x134C6780",
    "kClientGetLocalPlayerVtableOffset = 0x100",
    "clientPreFrameTickDetour",
    "currentThreadName",
    "ScopedPreFrameSwapPump",
    "gPreFrameSwapPumpDepth",
    "ClientInstance::preFrameTick pumping queued F swap (thread=%s)",
    "pending F swap rejected outside preFrameTick pump (thread=%s)",
    "draining queued F swap on MINECRAFT MAIN",
    "requestSwap()",
    "hasPendingSwap()",
    "processPendingSwap(",
    "mSwapRequested.store(true",
    "[SwapRuntime] F swap queued for MINECRAFT MAIN",
    "[SwapRuntime] swapped selected hotbar <-> OFFHAND without transient duplicates",
):
    if token not in runtime:
        raise AssertionError(f"OffhandSwapRuntime missing {token}")

for token in (
    "void requestSwap() noexcept",
    "bool hasPendingSwap() const noexcept",
    "bool processPendingSwap(",
    "std::atomic_bool mSwapRequested",
    "SetSelectedItemFn mSetSelectedItem",
):
    if token not in header:
        raise AssertionError(f"OffhandSwapRuntime.hpp missing {token}")

request_start = runtime.index("void OffhandSwapRuntime::requestSwap()")
request_end = runtime.index(
    "bool OffhandSwapRuntime::hasPendingSwap()",
    request_start,
)
request_body = runtime[request_start:request_end]
for forbidden in (
    "mItemStackCopyCtor(",
    "mSetItemInHandSlot(",
    "mGetOffhandSlot(",
    "ItemStackSnapshot",
):
    if forbidden in request_body:
        raise AssertionError(
            f"UI-thread requestSwap must not touch Minecraft state: {forbidden}"
        )

process_start = runtime.index("bool OffhandSwapRuntime::processPendingSwap(")
process_body = runtime[process_start:]
if "hasPendingSwap()" not in process_body:
    raise AssertionError("processPendingSwap must enforce pending request gate")
if "if (gPreFrameSwapPumpDepth == 0)" not in process_body:
    raise AssertionError("swap execution must be hard-gated to ClientInstance::preFrameTick")
if "mSwapRequested.compare_exchange_strong" in process_body.split("if (gPreFrameSwapPumpDepth == 0)")[0]:
    raise AssertionError("calls outside the preFrameTick pump must not consume the queued swap request")
if "mSetItemInHandSlot(player, kMainHand" in process_body:
    raise AssertionError(
        "swap must never write MAINHAND through carried-item setter"
    )
if "mSetSelectedItem(player, offStack)" not in process_body:
    raise AssertionError("main->empty-offhand must clear selected hotbar exactly once")
if process_body.count("mSetSelectedItem(player, offSnapshot.get())") < 2:
    raise AssertionError("offhand->main paths must write selected hotbar through setSelectedItem")
if process_body.count("mSetItemInHandSlot(player, kOffHand") < 3:
    raise AssertionError("all swap directions must write offhand only through hand=1")

if "mSetSelectedItem(player, gEmptyItem)" not in process_body:
    raise AssertionError(
        "occupied<->occupied swap must clear MAIN through native EMPTY_ITEM before moving OFF"
    )
if "gEmptyItem == nullptr || !mStackIsNull(gEmptyItem)" not in process_body:
    raise AssertionError("occupied swap must validate native EMPTY_ITEM before mutation")

if "mSetItemInHandSlot(player, kOffHand, gEmptyItem)" in process_body:
    raise AssertionError(
        "candidate must keep the user-tested 44a sequence; "
        "do not reintroduce the later clear-both derivative"
    )

occupied_start = process_body.rfind(
    "} else {\n        ItemStackSnapshot mainSnapshot("
)
if occupied_start < 0:
    raise AssertionError("occupied<->occupied swap branch missing")
occupied_body = process_body[occupied_start:]

occupied_sequence = (
    "mSetSelectedItem(player, gEmptyItem)",
    "mSetItemInHandSlot(player, kOffHand, mainSnapshot.get())",
    "mSetSelectedItem(player, offSnapshot.get())",
)
occupied_positions = [occupied_body.index(token) for token in occupied_sequence]
if occupied_positions != sorted(occupied_positions):
    raise AssertionError(
        "occupied swap order changed from 44a: MAIN empty -> OFF gets MAIN -> MAIN gets old OFF"
    )

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
):
    if forbidden in runtime:
        raise AssertionError(f"swap runtime must not synthesize {forbidden}")

if "processPendingSwap(" in router or "hasPendingSwap()" in router:
    raise AssertionError(
        "RightUseRouter selected-item hook must not execute or drain F swaps"
    )

for token in (
    "gClientPreFrameTickHook",
    "gGetSelectedItem",
    "localPlayerFromClient",
    "swap.processPendingSwap(player, selected)",
):
    if token not in runtime:
        raise AssertionError(
            f"OffhandSwapRuntime frame pump missing: {token}"
        )

if "observePlayer(player)" in router:
    raise AssertionError("swap must not cache a LocalPlayer pointer across threads")

right_use_install = mod.index("RightUseRouter::instance().install(context)")
swap_install = mod.index("OffhandSwapRuntime::instance().install(context)")
if right_use_install > swap_install:
    raise AssertionError(
        "RightUseRouter must install before the optional swap extension"
    )

for forbidden in (
    '#include "runtime/OffhandSwapRuntime.hpp"',
    "OffhandSwapRuntime::instance()",
    "processPendingSwap(",
    "hasPendingSwap()",
):
    if forbidden in router:
        raise AssertionError(
            f"RightUseRouter must remain independent from swap: {forbidden}"
        )

if "OffhandSwapRuntime::instance().requestSwap()" not in mod:
    raise AssertionError("SwapButton callback must only queue the swap request")
if "OffhandSwapRuntime::instance().swapNow()" in mod:
    raise AssertionError("SwapButton callback must not execute Minecraft swap synchronously")

if "src/runtime/OffhandSwapRuntime.cpp" not in cmake:
    raise AssertionError("CMakeLists.txt missing OffhandSwapRuntime.cpp")

print("v0.2.68 swap button ClientInstance frame-pump contract passed")
