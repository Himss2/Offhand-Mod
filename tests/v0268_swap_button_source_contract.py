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
    "kItemStackStorageSize = 0x98",
    'kMinecraftMainThreadName[] = "MINECRAFT MAIN"',
    "prctl(PR_GET_NAME",
    "requestSwap()",
    "hasPendingSwap()",
    "processPendingSwap(",
    "mSwapRequested.store(true",
    "[SwapRuntime] F swap queued for MINECRAFT MAIN",
    "[SwapRuntime] swapped MAINHAND <-> OFFHAND on MINECRAFT MAIN",
):
    if token not in runtime:
        raise AssertionError(f"OffhandSwapRuntime missing {token}")

for token in (
    "void requestSwap() noexcept",
    "bool hasPendingSwap() const noexcept",
    "bool processPendingSwap(",
    "std::atomic_bool mSwapRequested",
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
    raise AssertionError("processPendingSwap must enforce MINECRAFT MAIN gate")

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
):
    if forbidden in runtime:
        raise AssertionError(f"swap runtime must not synthesize {forbidden}")

for token in (
    "OffhandSwapRuntime::instance()",
    "swapRuntime.hasPendingSwap()",
    "swapRuntime.processPendingSwap(",
    "const void* selectedForSwap = original(player)",
):
    if token not in router:
        raise AssertionError(
            f"RightUseRouter must drain queued swap from native selected-item path: {token}"
        )

if "observePlayer(player)" in router:
    raise AssertionError("swap must not cache a LocalPlayer pointer across threads")

if "OffhandSwapRuntime::instance().requestSwap()" not in mod:
    raise AssertionError("SwapButton callback must only queue the swap request")
if "OffhandSwapRuntime::instance().swapNow()" in mod:
    raise AssertionError("SwapButton callback must not execute Minecraft swap synchronously")

if "src/runtime/OffhandSwapRuntime.cpp" not in cmake:
    raise AssertionError("CMakeLists.txt missing OffhandSwapRuntime.cpp")

print("v0.2.68 swap button game-thread dispatch contract passed")
