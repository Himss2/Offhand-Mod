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
    "LegacyInventoryAction",
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

# F swap must use one normal InventoryTransaction composed from the native
# selected-hotbar action (container 0) and one explicit OFFHAND action
# (container 119).  Do not open an ItemStackRequest or use the public OFF setter,
# because LocalPlayer suppresses its container-119 action in modern mode.
compact_engine = engine.replace(" ", "").replace("\n", "")
swap_body = engine[engine.index("bool SwapEngine::swap"):]

for forbidden in (
    "mSetItemInHandSlot(",
    "RequestSlotInfo",
    "LegacyRequestScope",
    "makePlace(",
    "makeSwap(",
):
    if forbidden in swap_body:
        raise AssertionError(
            f"F swap must not use rejected transaction boundary: {forbidden}"
        )

for token in (
    "mStorage[4]=static_cast<std::byte>(containerId)",
    "kInventoryActionSlotOffset=0x0C",
    "gInventoryTransactionAddAction(manager,mStorage.data(),0)",
    "kHotbarLegacyContainerId=0x00",
    "kOffhandLegacyContainerId=0x77",
    "kInventoryActionSize=0x1E0",
    "kInventoryActionOldDescriptorOffset=0x10",
    "kInventoryActionNewDescriptorOffset=0x60",
    "kInventoryActionOldStackOffset=0xB0",
    "kInventoryActionNewStackOffset=0x148",
):
    if token not in compact_engine and token not in engine:
        raise AssertionError(f"legacy OFF InventoryAction bridge missing {token}")

off_empty_start = compact_engine.index("if(offEmpty){")
main_empty_start = compact_engine.index("if(mainEmpty){", off_empty_start)
off_empty_body = compact_engine[off_empty_start:main_empty_start]
for token in (
    "mSetSelectedItem(player,mEmptyItem)",
    "offAction.submit(player)",
    "gSetOffhandRaw(player,main.get())",
):
    if token not in off_empty_body:
        raise AssertionError(f"MAIN->OFF transaction order missing {token}")
positions = [off_empty_body.index(token) for token in (
    "mSetSelectedItem(player,mEmptyItem)",
    "offAction.submit(player)",
    "gSetOffhandRaw(player,main.get())",
)]
if positions != sorted(positions):
    raise AssertionError("MAIN->OFF must record hotbar then OFF action before raw OFF write")

occupied_marker = "Snapshotmain(mItemStackCopyCtor,mItemStackDtor,selected);Snapshot"
occupied_start = compact_engine.index(occupied_marker, main_empty_start)
main_empty_body = compact_engine[main_empty_start:occupied_start]
for token in (
    "offAction.submit(player)",
    "gSetOffhandRaw(player,mEmptyItem)",
    "mSetSelectedItem(player,offSnap.get())",
):
    if token not in main_empty_body:
        raise AssertionError(f"OFF->MAIN transaction order missing {token}")
positions = [main_empty_body.index(token) for token in (
    "offAction.submit(player)",
    "gSetOffhandRaw(player,mEmptyItem)",
    "hotbarFillAction.submit(player)",
    "mSetSelectedItem(player,offSnap.get())",
)]
if positions != sorted(positions):
    raise AssertionError(
        "OFF->MAIN must record/write OFF, explicitly record HOTBAR fill, then native selected setter"
    )

occupied_body = compact_engine[occupied_start:]
occupied_sequence = (
    "mSetSelectedItem(player,mEmptyItem)",
    "offAction.submit(player)",
    "gSetOffhandRaw(player,main.get())",
    "hotbarFillAction.submit(player)",
    "mSetSelectedItem(player,offSnap.get())",
)
positions = [occupied_body.index(token) for token in occupied_sequence]
if positions != sorted(positions):
    raise AssertionError(
        "occupied 44a transaction order changed: hotbar clear -> OFF action/write -> hotbar refill"
    )

if engine.count("legacyTransactionSettled(player)") != 3:
    raise AssertionError("all three non-empty F paths must verify transaction settlement")

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
    "RequestSlotInfo",
    "ItemStackNetManager request bridge",
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

# Exact RE supersedes the old #659 post-settlement healer. Predictive slot
# ownership must be recorded while the Player-aware request is active; a
# second public hand write after settlement is both redundant and a regression
# risk (build #666 proved public OFF writes can duplicate/break placement).
if "normalizeDestinationHandWithNativeLegacyRequest(" in engine:
    raise AssertionError(
        "post-settlement hand normalizer must stay removed from RE-balanced swap"
    )
if "[SwapEngine][native-client-normalize]" in engine:
    raise AssertionError("obsolete native-client-normalize logging remains")

# Regression: F swap must participate in the same ItemStackNetManager
# legacy touched-slot bookkeeping used by native player-container mutations.
# The old #645 diagnosis was invalid because it passed Player* where
# _addLegacyTransactionRequestSetItemSlot requires ItemStackNetManagerScreen&.
for marker in (
    "kTryBeginClientLegacyTransactionRva=0xF88A434",
    "kRecordLegacySlotRva=0xF88CE8C",
    "kInventoryContainerType=-1",
    "kHandContainerType=19",
    "kGetTopScreenRva=0xF88AA24",
    "kGetTopScreenFingerprint",
    "recordChangedSlot(",
    "[SwapEngine][legacy-screen-slots]",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(
            f"screen-aware legacy touched-slot bookkeeping missing {marker}"
        )

swap_body = engine[engine.index("bool SwapEngine::swap"):]
compact_swap = swap_body.replace(" ", "").replace("\n", "")

for required in (
    "recordChangedSlot(screen,kInventoryContainerType,selectedSlot)",
    "recordChangedSlot(screen,kHandContainerType,kOffhandLocalSlot)",
):
    if required not in compact_swap:
        raise AssertionError(f"paired legacy slot bookkeeping missing {required}")

if "recordChangedSlot(player" in compact_swap:
    raise AssertionError(
        "Player* must never be passed as ItemStackNetManagerScreen&"
    )

# Preserve the working #662 storage/action architecture. The bookkeeping fix
# must not reintroduce the public hand setter that caused build #666 duplication
# and broke OFF block placement.
if "mSetItemInHandSlot(" in compact_swap:
    raise AssertionError(
        "screen-slot bookkeeping must not change the #662 OFF storage writer"
    )

# Exact-binary RE correction for the final_action/std::function object:
# every return path of 0xF88A434 -> 0xF88D960 stores the scope base itself at
# scope+0x20 (0xF88A480, 0xF88D9E8, 0xF88DA40). For this exact wrapper the
# callable is therefore ALWAYS inline. If the pointer is external, the object
# has been relocated/corrupted and must fail closed rather than call heap
# destroy (+0x28) on a stack address.
for marker in (
    "kNativeFunctionDestroyInlineVtableOffset=0x20",
    "callable!=static_cast<void*>(scope.storage.data())",
    "native scope self-pointer mismatch",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(
            f"inline-only native legacy-scope cleanup missing {marker}"
        )

finish_start = engine.index("[[nodiscard]] bool finishNativeClientLegacyScope")
finish_end = engine.index(
    "[[nodiscard]] int selectedHotbarSlot",
    finish_start
)
finish_body = engine[finish_start:finish_end]
if "kNativeFunctionDestroyHeapVtableOffset" in finish_body:
    raise AssertionError(
        "exact 1.26.51.1 final_action cleanup must never heap-destroy a relocated scope"
    )

# Never dereference the native callable vtable before proving it belongs to a
# mapped module region. This is especially important for the Player-aware
# legacy scope because failure occurs before any swap mutation.
screen_scope_start = engine.index("class LegacyScreenSlotScope")
screen_scope_end = engine.index(
    "[[nodiscard]] bool legacyInventoryTransactionAvailable",
    screen_scope_start
)
screen_scope_body = engine[screen_scope_start:screen_scope_end]
vtable_decl = screen_scope_body.index(
    "const void* callableVtable=read<const void*>(callable,0,nullptr);"
)
mapped_guard = screen_scope_body.index(
    "mapped(reinterpret_cast<std::uintptr_t>(callableVtable),0)"
)
invoke_read = screen_scope_body.index(
    "read<NativeScopeCallableFn>(",
    vtable_decl
)
if mapped_guard > invoke_read:
    raise AssertionError(
        "legacy screen scope dereferences callable vtable before mapped guard"
    )

# Exact RE helpers must not gate SwapRuntime::installed(), but an individual
# F request must fail closed BEFORE any storage mutation if one of those
# helpers is unavailable. Do not fall back to a mixed #662 transaction after
# opening/expecting predictive bookkeeping.
install_start = engine.index("bool SwapEngine::install")
uninstall_start = engine.index("void SwapEngine::uninstall", install_start)
install_body = engine[install_start:uninstall_start]
stable_start = install_body.index("const bool stableBuild=")
stable_end = install_body.index("bool setSelectedChainedLive", stable_start)
stable_body = install_body[stable_start:stable_end]
for forbidden in (
    "tryLegacyTransaction",
    "recordLegacySlot",
):
    if forbidden in stable_body:
        raise AssertionError(
            f"RE helper must not gate SwapRuntime installation: {forbidden}"
        )

ready_start = engine.index("bool SwapEngine::ready() const noexcept")
ready_end = engine.index("const void* SwapEngine::selectedStack", ready_start)
ready_body = engine[ready_start:ready_end]
for forbidden in (
    "gTryBeginClientLegacyTransaction",
    "gRecordLegacySlot",
):
    if forbidden in ready_body:
        raise AssertionError(
            f"RE helper must not gate ready(): {forbidden}"
        )

swap_start = engine.index("bool SwapEngine::swap")
swap_body_full = engine[swap_start:]
for marker in (
    "exact RE helper unavailable; swap rejected before mutation",
    "native request/screen invalid; swap rejected before mutation",
    "paired registration failed; swap rejected before mutation",
):
    if marker not in swap_body_full:
        raise AssertionError(f"fail-closed RE swap guard missing {marker}")

if "using #662 baseline" in swap_body_full:
    raise AssertionError(
        "RE-balanced candidate must not fall back to mixed #662 mutation"
    )

# Exact-binary RE correction from setPlayerContainer @ 0xF88A664.
# Its legacy branch does NOT call 0xF88AA24. It reads the active
# ItemStackNetManagerScreen directly from manager+0x38:
#   screenStack = [manager+0x38]
#   index       = [screenStack+0x20]
#   map         = [screenStack+0x08]
#   block       = map[((index >> 6) & ~7)]
#   screen      = block[index & 0x1ff]
# This path is valid in normal gameplay where the separate 0xF88AA24 lookup
# can legitimately return null.
for forbidden in (
    "kGetTopScreenRva",
    "kGetTopScreenFingerprint",
    "gGetTopScreen",
    "GetTopScreenFn",
):
    if forbidden in engine:
        raise AssertionError(
            f"legacy request screen must not depend on unrelated top-screen resolver: {forbidden}"
        )

for marker in (
    "kItemStackNetManagerScreenStackOffset=0x38",
    "kScreenStackMapOffset=0x08",
    "kScreenStackIndexOffset=0x20",
    "currentLegacyRequestScreen(",
    "index>>6",
    "index&0x1FF",
    "[SwapEngine][legacy-screen-slots] direct screen=",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(
            f"setPlayerContainer screen-stack RE contract missing {marker}"
        )

# Exact RE: ItemStackNetManagerBase::setPlayerContainer @ 0xF88A664
# returns false for an empty ItemStack before touched-slot bookkeeping.
# Selected-container setter @ 0xF9DA2DC therefore falls back to 0xF8834D4
# only for MAIN->EMPTY. A MAIN->NONEMPTY write inside an active legacy
# request succeeds natively and skips that fallback, so F swap must supply
# the missing hotbar InventoryAction explicitly in those directions.
for marker in (
    "kInventoryActionSlotOffset=0x0C",
    "kHotbarLegacyContainerId=0x00",
    "LegacyInventoryAction",
    "hotbarFillAction",
    "[SwapEngine][re-balanced]",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(f"RE-balanced transaction missing {marker}")

swap_body = engine[engine.index("bool SwapEngine::swap"):]
compact_swap = swap_body.replace(" ", "").replace("\n", "")

# MAIN->OFF ends MAIN at EMPTY, so the selected setter must retain its native
# fallback action. Do not manually duplicate that hotbar clear.
off_start = compact_swap.index("if(offEmpty){")
main_start = compact_swap.index("if(mainEmpty){", off_start)
occupied_start = compact_swap.index("Snapshotmain(", main_start)
off_body = compact_swap[off_start:main_start]
main_body = compact_swap[main_start:occupied_start]
occupied_body = compact_swap[occupied_start:]

if "hotbarFillAction.submit(player)" in off_body:
    raise AssertionError("MAIN->OFF must not duplicate native MAIN->EMPTY fallback action")

# OFF->MAIN writes a NONEMPTY selected stack while the request is active, so
# native setPlayerContainer succeeds and skips legacy fallback. We must add
# EMPTY->B ourselves before the selected write.
for token in (
    "hotbarFillAction.submit(player)",
    "mSetSelectedItem(player,offSnap.get())",
):
    if token not in main_body:
        raise AssertionError(f"OFF->MAIN missing {token}")
if main_body.index("hotbarFillAction.submit(player)") > main_body.index(
    "mSetSelectedItem(player,offSnap.get())"
):
    raise AssertionError("OFF->MAIN hotbar action must precede native selected write")

# Occupied 44a: first A->EMPTY is native fallback; final EMPTY->B is native
# setPlayerContainer and needs an explicit legacy hotbar action.
for token in (
    "mSetSelectedItem(player,mEmptyItem)",
    "offAction.submit(player)",
    "hotbarFillAction.submit(player)",
    "mSetSelectedItem(player,offSnap.get())",
):
    if token not in occupied_body:
        raise AssertionError(f"occupied RE-balanced path missing {token}")
positions=[occupied_body.index(x) for x in (
    "mSetSelectedItem(player,mEmptyItem)",
    "offAction.submit(player)",
    "hotbarFillAction.submit(player)",
    "mSetSelectedItem(player,offSnap.get())",
)]
if positions != sorted(positions):
    raise AssertionError(
        "occupied order must remain MAIN clear -> OFF action -> explicit MAIN fill action -> MAIN fill"
    )

# Exact crash regression from build #690 / tombstone:
# NativeClientLegacyScope contains a self-pointer at +0x20 when constructed
# inline by 0xF88A434/0xF88D960. Byte-copying or assigning that aggregate
# relocates the storage but leaves the internal pointer aimed at the temporary,
# causing finishNativeClientLegacyScope() to select heap destroy (+0x28) and
# delete stack memory.
if "mScope=gTryBeginClientLegacyTransaction(player)" in engine.replace(" ", ""):
    raise AssertionError(
        "native legacy scope must never be copy-assigned after construction"
    )
for marker in (
    "NativeClientLegacyScope nativeScope=",
    "gTryBeginClientLegacyTransaction(player)",
    "LegacyScreenSlotScope screenSlots(nativeScope,player)",
    "NativeClientLegacyScope& mScope",
):
    if marker not in engine.replace(" ", "") and marker not in engine:
        raise AssertionError(
            f"non-relocating native scope lifetime contract missing {marker}"
        )

