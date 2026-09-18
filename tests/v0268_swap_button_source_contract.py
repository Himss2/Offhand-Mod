from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

button = (ROOT / "src/ui/SwapButton.cpp").read_text(errors="replace")
runtime = (ROOT / "src/runtime/OffhandSwapRuntime.cpp").read_text(errors="replace")
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
    "kSelectedItemRva = 0xF9F7824",
    "kOffhandSlotRva = 0xF579C2C",
    "kStackIsNullRva = 0xFFA0F70",
    "kItemStackCopyCtorRva = 0xFF9D748",
    "kItemStackDtorRva = 0x85ADF98",
    "kSetItemInHandSlotRva = 0xF579C50",
    "kItemStackStorageSize = 0x98",
    "ItemStackSnapshot mainSnapshot",
    "ItemStackSnapshot offSnapshot",
    "mSetItemInHandSlot(player, kMainHand, offSnapshot.get())",
    "mSetItemInHandSlot(player, kOffHand, mainSnapshot.get())",
    "[SwapRuntime] swapped MAINHAND <-> OFFHAND",
):
    if token not in runtime:
        raise AssertionError(f"OffhandSwapRuntime missing {token}")

main_snapshot = runtime.index("ItemStackSnapshot mainSnapshot")
off_snapshot = runtime.index("ItemStackSnapshot offSnapshot")
first_write = runtime.index("mSetItemInHandSlot(player, kMainHand")
if not (main_snapshot < first_write and off_snapshot < first_write):
    raise AssertionError("both ItemStack snapshots must exist before first hand write")

for forbidden in (
    "InventoryTransactionPacket",
    "ContainerValidation",
    "ItemStackRequestAction",
):
    if forbidden in runtime:
        raise AssertionError(f"swap runtime must not synthesize {forbidden}")

if "OffhandSwapRuntime::instance().observePlayer(player)" not in router:
    raise AssertionError("RightUseRouter must publish its verified LocalPlayer")

if "OffhandSwapRuntime::instance().swapNow()" not in mod:
    raise AssertionError("SwapButton callback must call native swap runtime")

if "src/runtime/OffhandSwapRuntime.cpp" not in cmake:
    raise AssertionError("CMakeLists.txt missing OffhandSwapRuntime.cpp")

print("v0.2.68 swap button/runtime source contract passed")
