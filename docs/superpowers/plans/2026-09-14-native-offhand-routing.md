# Native Offhand Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all legacy `ContainerValidation`-based offhand storage hooks with native `Item::mAllowOffHand` capability plus one automatic-insertion destination filter so arbitrary items can be manually stored in offhand without crafting/pickup auto-routing there.

**Architecture:** `NativeOffhandPolicy` applies the exact-build constructor instruction patch `mov w9,#0x50 -> mov w9,#0xD0`, making item definitions natively offhand-capable without a getter hook. `AutoInsertRouting` detours only the central planner at RVA `0xF024024`; when its destination list contains `ContainerEnumName::Offhand (34)`, it builds a temporary opaque 0x20-byte-record list with offhand removed and invokes the original planner synchronously. Manual inventory transactions are left fully native and no `ContainerScreenValidation`, `OffhandContainerValidation`, or `ItemStackBase::getAllowOffHand` hook remains.

**Tech Stack:** C++20, Android ARM64, Levi Launcher preloader-android / `pl::memory::HookHandle`, `pl::memory::Signature`, `pl::memory::Patch`, Python 3 source/binary contract tests, Bash test runner, CMake/Ninja, Android NDK `28.2.13676358`.

**Spec:** `docs/superpowers/specs/2026-09-14-native-offhand-routing-design.md`

## Global Constraints

- Target Minecraft Bedrock Android version is exactly `1.26.45.1`.
- Target `libminecraftpe.so` SHA-256 is exactly `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`.
- Target GNU Build ID is exactly `868e275cb295e9a275bb29d2258edc2f7dc48761`.
- No hook may target `ContainerScreenValidation::tryTransfer`, `ContainerScreenValidation::trySwap`, `OffhandContainerValidation`, or `ItemStackBase::getAllowOffHand`.
- No direct `ItemStack` writes and no synthesized inventory packets.
- Automatic insertion must exclude container enum `34` only; all other opaque destination records must be copied byte-for-byte and in original order.
- Destination record size is exactly `0x20` bytes on this exact target.
- Any malformed planner vector header, impossible record count, allocation failure, signature failure, or patch failure must fail safe by either refusing installation or delegating to the original planner unchanged.
- `main` must not be updated until device testing succeeds and the user explicitly approves the merge.

---

### Task 1: Lock source and binary contracts before production changes

**Files:**
- Create: `tests/v0262_native_routing_source_contract.py`
- Create: `tests/v0262_native_routing_binary_contract.py`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Consumes: production tree rooted at repository root; optional `LEVI_MCPE_LIBRARY` path to raw `.so` or ZIP containing `libminecraftpe.so`.
- Produces: executable source contract and exact-build binary contract that fail on legacy ContainerValidation architecture and verify the new planner/constructor invariants.

- [ ] **Step 1: Write the failing source contract**

Create `tests/v0262_native_routing_source_contract.py` with these exact rules:

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = [
    ROOT / "src" / "LeviOffhandMod.cpp",
    ROOT / "src" / "runtime" / "NativeOffhandPolicy.cpp",
    ROOT / "src" / "runtime" / "NativeOffhandPolicy.hpp",
    ROOT / "src" / "runtime" / "AutoInsertRouting.cpp",
    ROOT / "src" / "runtime" / "AutoInsertRouting.hpp",
    ROOT / "CMakeLists.txt",
]

forbidden = (
    "ContainerScreenValidation::tryTransfer",
    "ContainerScreenValidation::trySwap",
    "kManualSetPathSignature",
    "kTryTransferSignature",
    "kTrySwapSignature",
    "allowOffhandDetour",
    "gManualSetDepth",
    "gTransferDepth",
    "gAutoAddDepth",
    "OffhandValidationHook",
)

required = {
    "src/runtime/NativeOffhandPolicy.cpp": (
        "0xF65A3BC",
        "09 0A 80 52",
        "09 1A 80 52",
        "levi_offhand.item_allow_offhand",
    ),
    "src/runtime/AutoInsertRouting.cpp": (
        "0xF024024",
        "kOffhandContainer = 34",
        "kDestinationRecordSize = 0x20",
        "original(",
        "std::nothrow",
    ),
}

texts = {}
for path in PRODUCTION:
    assert path.exists(), f"missing production file: {path.relative_to(ROOT)}"
    texts[str(path.relative_to(ROOT))] = path.read_text(encoding="utf-8")

joined = "\n".join(texts.values())
for token in forbidden:
    assert token not in joined, f"legacy storage token remains: {token}"

for rel, tokens in required.items():
    text = texts[rel]
    for token in tokens:
        assert token in text, f"missing {token!r} in {rel}"

assert not (ROOT / "src/runtime/OffhandValidationHook.cpp").exists()
assert not (ROOT / "src/runtime/OffhandValidationHook.hpp").exists()
print("v0.2.62 native routing source contract passed")
```

- [ ] **Step 2: Run the source contract and verify RED**

Run:

```bash
python3 tests/v0262_native_routing_source_contract.py
```

Expected: FAIL because `NativeOffhandPolicy.*` and `AutoInsertRouting.*` do not exist and `OffhandValidationHook.*` still exists.

- [ ] **Step 3: Write the binary contract**

Create `tests/v0262_native_routing_binary_contract.py` that:

```python
EXPECTED_SHA256 = "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"
ITEM_CTOR_FLAG_RVA = 0xF65A3BC
ITEM_CTOR_FLAG_BYTES = bytes.fromhex("09 0A 80 52")
GET_ALLOW_RVA = 0xF644930
GET_ALLOW_BYTES = bytes.fromhex(
    "08 04 40 F9 88 00 00 B4 00 01 40 F9 40 00 00 B4 "
    "D6 8B 00 14 E0 03 1F 2A C0 03 5F D6"
)
OFFHAND_VALIDATION_RVA = 0xF6F5D10
OFFHAND_VALIDATION_PREFIX = bytes.fromhex(
    "FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 68 8C 40 39"
)
PLANNER_RVA = 0xF024024
PLANNER_PREFIX = bytes.fromhex(
    "FF 83 05 D1 FD 7B 10 A9 FC 6F 11 A9 FA 67 12 A9 "
    "F8 5F 13 A9 F6 57 14 A9 F4 4F 15 A9 FD 03 04 91"
)
PLANNER_CALLS = (0xF023868, 0xF02E09C, 0xF03B900)
STRIDE_RVA = 0xF0240EC
STRIDE_BYTES = bytes.fromhex("18 83 00 91")
```

The script must parse ELF64 PT_LOAD headers, map RVA to file offsets, verify the full SHA-256, verify each byte sequence above, scan executable PT_LOAD segments for AArch64 `BL` instructions whose decoded target is `0xF024024`, and assert that the hit tuple equals exactly `PLANNER_CALLS`. It must accept either a raw `.so` or a ZIP containing `libminecraftpe.so`.

- [ ] **Step 4: Run the binary contract on the supplied binary and verify GREEN**

Run:

```bash
python3 tests/v0262_native_routing_binary_contract.py /mnt/data/re_storage_arch/bin/libminecraftpe.so
```

Expected:

```text
v0.2.62 native routing binary contract passed
```

- [ ] **Step 5: Wire both contracts into the test runner**

Append to `tests/run_native_attachment_fix_tests.sh`:

```bash
python3 "$repo_root/tests/v0262_native_routing_source_contract.py"

if [[ -n "${LEVI_MCPE_LIBRARY:-}" ]]; then
    python3 \
        "$repo_root/tests/v0262_native_routing_binary_contract.py" \
        "$LEVI_MCPE_LIBRARY"
else
    printf '%s\n' \
        "v0.2.62 native routing binary contract skipped: LEVI_MCPE_LIBRARY unset"
fi
```

Do not remove the existing v0.2.59 attachment/crash contracts in this task.

- [ ] **Step 6: Commit the tests**

```bash
git add tests/v0262_native_routing_source_contract.py \
        tests/v0262_native_routing_binary_contract.py \
        tests/run_native_attachment_fix_tests.sh
git commit -m "test: lock native offhand routing contracts"
```

---

### Task 2: Replace getter/container validation hooks with native Item capability

**Files:**
- Create: `src/runtime/NativeOffhandPolicy.hpp`
- Create: `src/runtime/NativeOffhandPolicy.cpp`
- Modify: `src/LeviOffhandMod.cpp`
- Modify: `CMakeLists.txt`
- Delete: `src/runtime/OffhandValidationHook.hpp`
- Delete: `src/runtime/OffhandValidationHook.cpp`

**Interfaces:**
- Consumes: `pl::mod::ModContext`, `pl::memory::resolveSignature`, `pl::memory::writeBytes`, `pl::memory::revertPatch`.
- Produces: singleton `levioffhand::runtime::NativeOffhandPolicy` with `install`, `uninstall`, `setFeatureEnabled`, `featureEnabled`, and `installed` methods matching the current mod lifecycle needs.

- [ ] **Step 1: Add the policy header**

Create `src/runtime/NativeOffhandPolicy.hpp`:

```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::runtime {

class NativeOffhandPolicy final {
public:
    static NativeOffhandPolicy& instance() noexcept;
    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;
    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    NativeOffhandPolicy() = default;
    bool applyPatch() noexcept;
    void revertPatch() noexcept;

    std::uintptr_t mInstruction{0};
    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mInstalled{false};
};

} // namespace levioffhand::runtime
```

- [ ] **Step 2: Implement exact native constructor policy**

Create `src/runtime/NativeOffhandPolicy.cpp` using the verified signature window that starts at RVA `0xF65A3B0`:

```cpp
constexpr std::uintptr_t kItemDefaultFlagsRva = 0xF65A3BC;
constexpr std::uintptr_t kPatchOffsetFromSignature = 0x0C;
constexpr char kItemConstructorFlagSignature[] =
    "08 DA 94 94 00 E4 00 6F F5 03 13 AA 09 0A 80 52 "
    "A0 8E 8E 3C A8 56 40 79 A0 C2 00 91 A0 A2 81 3C "
    "08 19 17 12 A0 06 80 3D 08 01 09 2A BF 2E 00 B9";
constexpr char kVanillaItemFlagsInstruction[] = "09 0A 80 52";
constexpr char kAllOffhandItemFlagsInstruction[] = "09 1A 80 52";
constexpr char kAllOffhandPatchName[] = "levi_offhand.item_allow_offhand";
```

`install()` must resolve the signature, add `0x0C`, verify that `dladdr` maps the target to `libminecraftpe.so`, apply the patch with `pl::memory::writeBytes`, and log:

```text
[NativeOffhandPolicy] Item default flags 0x50 -> 0xD0
```

If resolution or write fails, return `false`; do not install any other storage component.

`setFeatureEnabled(false)` may revert the constructor patch for future constructions, but it must log that already-constructed Item singletons retain their current flags until process restart. `uninstall()` must revert the named patch if active.

- [ ] **Step 3: Remove the legacy validation source completely**

Delete:

```text
src/runtime/OffhandValidationHook.cpp
src/runtime/OffhandValidationHook.hpp
```

No alias or compatibility wrapper is allowed because the source contract intentionally forbids the old name.

- [ ] **Step 4: Change build inputs**

In `CMakeLists.txt`, replace:

```cmake
src/runtime/OffhandValidationHook.cpp
```

with:

```cmake
src/runtime/NativeOffhandPolicy.cpp
src/runtime/AutoInsertRouting.cpp
```

`AutoInsertRouting.cpp` will be added by Task 3; at the end of Task 2 the Android target may not yet build, which is acceptable because Task 2 is not merged independently.

- [ ] **Step 5: Change the mod lifecycle to use the new policy name**

In `src/LeviOffhandMod.cpp`, replace the legacy include with:

```cpp
#include "runtime/NativeOffhandPolicy.hpp"
#include "runtime/AutoInsertRouting.hpp"
```

During `enable()`, `AutoInsertRouting` must be installed before `NativeOffhandPolicy` so the automatic-routing guard exists before any new native offhand capability is enabled. No ContainerValidation hook is installed.

For the Mod Menu toggle, do not disable automatic routing independently after native capability has been applied in this process; Task 3 will provide `AutoInsertRouting::setFeatureEnabled` semantics that remain fail-safe while existing Item singletons are still offhand-capable.

- [ ] **Step 6: Re-run the source contract**

Run:

```bash
python3 tests/v0262_native_routing_source_contract.py
```

Expected: still FAIL only because `AutoInsertRouting.*` is not implemented yet. It must no longer fail on legacy `OffhandValidationHook` files/tokens.

- [ ] **Step 7: Commit the native policy conversion**

```bash
git add CMakeLists.txt src/LeviOffhandMod.cpp \
        src/runtime/NativeOffhandPolicy.hpp \
        src/runtime/NativeOffhandPolicy.cpp
git rm src/runtime/OffhandValidationHook.hpp src/runtime/OffhandValidationHook.cpp
git commit -m "refactor: replace offhand validation hooks with native policy"
```

---

### Task 3: Filter offhand from the central automatic-insertion planner

**Files:**
- Create: `src/runtime/AutoInsertRouting.hpp`
- Create: `src/runtime/AutoInsertRouting.cpp`
- Test: `tests/v0262_native_routing_source_contract.py`
- Test: `tests/v0262_native_routing_binary_contract.py`

**Interfaces:**
- Consumes: original planner ABI `int (*)(void*, const void*, const void*, int, const void*)` and an opaque destination vector header whose elements are exactly `0x20` bytes.
- Produces: singleton `levioffhand::runtime::AutoInsertRouting`; its detour preserves every non-offhand destination record byte-for-byte and calls the original planner synchronously with a temporary vector ABI header.

- [ ] **Step 1: Add the routing header**

Create `src/runtime/AutoInsertRouting.hpp`:

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory { class HookHandle; }

namespace levioffhand::runtime {

class AutoInsertRouting final {
public:
    static AutoInsertRouting& instance() noexcept;
    ~AutoInsertRouting();
    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;
    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    AutoInsertRouting() = default;
    static int plannerDetour(
        void* arg0,
        const void* arg1,
        const void* arg2,
        int amount,
        const void* destinationVector
    ) noexcept;

    static AutoInsertRouting* sInstance;
    std::unique_ptr<pl::memory::HookHandle> mHook;
    void* mOriginal{nullptr};
    std::uintptr_t mTarget{0};
    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mLoggedFilter{false};
};

} // namespace levioffhand::runtime
```

- [ ] **Step 2: Define only the ABI needed for filtering**

In `src/runtime/AutoInsertRouting.cpp`, define:

```cpp
constexpr std::uintptr_t kPlannerRva = 0xF024024;
constexpr std::uint8_t kOffhandContainer = 34;
constexpr std::size_t kDestinationRecordSize = 0x20;
constexpr std::size_t kMaxDestinationRecords = 256;

struct DestinationRecord {
    std::array<std::byte, kDestinationRecordSize> bytes{};
};
static_assert(sizeof(DestinationRecord) == 0x20);

struct DestinationVectorAbi {
    const DestinationRecord* begin;
    const DestinationRecord* end;
    const DestinationRecord* capacityEnd;
};
static_assert(sizeof(DestinationVectorAbi) == 0x18);
```

Use the existing exact unique planner signature from the old storage source:

```cpp
constexpr char kAutoInsertPlannerSignature[] =
    "FF 83 05 D1 FD 7B 10 A9 FC 6F 11 A9 FA 67 12 A9 "
    "F8 5F 13 A9 F6 57 14 A9 F4 4F 15 A9 FD 03 04 91 "
    "E2 2F 00 F9 49 D0 3B D5 28 15 40 F9 7F 04 00 71 "
    "A8 03 1F F8 0B 55 00 54 F6 03 00 AA";
```

- [ ] **Step 3: Implement strict header validation and zero-copy fast path**

Before filtering, reinterpret only the 24-byte vector header. Convert its three pointers to `std::uintptr_t` and reject to the original planner unchanged when any of these are true:

```text
begin == 0 while end/capacity are nonzero
end < begin
capacityEnd < end
(end - begin) % 0x20 != 0
recordCount > 256
```

If `recordCount == 0`, delegate unchanged.

Scan the records using `std::memcpy`/byte access only. Container identity is byte `0` of each record. If no record has value `34`, call the original planner with the original `destinationVector` pointer; this is the required zero-copy path.

- [ ] **Step 4: Implement filtered temporary view without Minecraft constructors**

If at least one offhand record exists:

```cpp
auto filtered = std::unique_ptr<DestinationRecord[]>(
    new (std::nothrow) DestinationRecord[recordCount]
);
```

If allocation returns `nullptr`, call the original planner unchanged.

Copy only records whose first byte is not `34` with `std::memcpy`. Preserve order. Build:

```cpp
DestinationVectorAbi filteredVector{
    filtered.get(),
    filtered.get() + keptCount,
    filtered.get() + keptCount,
};
```

Call the original planner synchronously with `&filteredVector`. Do not retain any pointer after the call returns.

On the first filtered call, log candidate count, kept count, and caller RVA recovered through `__builtin_return_address(0)` plus module-base subtraction when `dladdr` succeeds:

```text
[AutoInsertRouting] callerRva=0x... candidates=N offhandFiltered=1
```

- [ ] **Step 5: Install only this one storage detour**

`install()` must resolve `kAutoInsertPlannerSignature`, verify the target belongs to `libminecraftpe.so`, create one `pl::memory::HookHandle`, require a non-null original trampoline, and log:

```text
[AutoInsertRouting] planner active RVA=0xF024024
[AutoInsertRouting] ContainerValidation hooks = 0
```

No other storage `HookHandle` may exist.

- [ ] **Step 6: Make feature-disable fail safe**

Because existing Item singletons can retain `mAllowOffHand=true` until process restart, `setFeatureEnabled(false)` must **not** disable filtering in the current process once native capability has been installed. It must keep `mFeatureEnabled=true` for routing and log once:

```text
[AutoInsertRouting] routing guard retained until process restart
```

Actual hook removal happens only from `uninstall()` during mod unload/process teardown.

- [ ] **Step 7: Integrate installation order and rollback**

In `src/LeviOffhandMod.cpp`, storage installation order must be:

```cpp
if (!runtime::AutoInsertRouting::instance().install(context)) {
    context.logger().error("Levi Offhand: automatic routing installation failed");
    return false;
}

if (!runtime::NativeOffhandPolicy::instance().install(context)) {
    runtime::AutoInsertRouting::instance().uninstall(context);
    context.logger().error("Levi Offhand: native offhand policy installation failed");
    return false;
}
```

`disable()` and Mod Menu toggle-off revert the constructor patch but retain the routing guard until process restart. Actual routing-hook removal happens only from `unload()`/process teardown, after which native policy state is also released.

- [ ] **Step 8: Run source and binary contracts**

Run:

```bash
python3 tests/v0262_native_routing_source_contract.py
python3 tests/v0262_native_routing_binary_contract.py /mnt/data/re_storage_arch/bin/libminecraftpe.so
```

Expected: both PASS.

- [ ] **Step 9: Commit the routing implementation**

```bash
git add src/runtime/AutoInsertRouting.hpp \
        src/runtime/AutoInsertRouting.cpp \
        src/LeviOffhandMod.cpp CMakeLists.txt
git commit -m "feat: filter offhand from automatic insertion"
```

---

### Task 4: Version, packaging, and full verification

**Files:**
- Modify: `manifest.json`
- Modify: `scripts/build.sh`
- Modify: `README.md`
- Modify: `.github/workflows/build.yml` if test branches are not already included
- Test: complete `tests/` suite

**Interfaces:**
- Consumes: Tasks 1-3 implementation.
- Produces: test-branch CI artifact `levi-offhand-v0.2.62.levipack`, full-repo ZIP, changed-files ZIP, and device-test instructions.

- [ ] **Step 1: Set experimental version and documentation**

Set `manifest.json` version to:

```json
"version": "0.2.62"
```

Set `scripts/build.sh` package name to:

```bash
LEVIPACK="$DIST_DIR/levi-offhand-v0.2.62.levipack"
```

Update `README.md` so the v0.2.62 section states exactly:

```text
- legacy ContainerValidation/getAllowOffHand storage hooks removed
- Item native mAllowOffHand capability is enabled at construction
- automatic insertion filters Offhand container 34 at the central planner
- manual transactions remain vanilla/native
- Bow/Trident rendering is intentionally unchanged in this storage milestone
```

- [ ] **Step 2: Ensure GitHub Actions builds test branches**

The workflow trigger must include:

```yaml
on:
  push:
    branches:
      - main
      - "test/**"
```

Do not merge or push to `main`.

- [ ] **Step 3: Run the complete host/source/binary verification with the exact binary**

Run:

```bash
LEVI_MCPE_LIBRARY=/mnt/data/re_storage_arch/bin/libminecraftpe.so \
    bash tests/run_native_attachment_fix_tests.sh
```

Expected: all existing attachment/crash contracts plus both v0.2.62 routing contracts PASS with exit code 0.

- [ ] **Step 4: Run a host syntax compile for the pure source surface that does not require Android headers**

Run Python source contracts and any existing host C++ tests with `-Wall -Wextra -Wpedantic -Werror`; no warning may be ignored. Android-only translation units are validated by CI/NDK in Step 6.

- [ ] **Step 5: Create and push only a non-main test branch**

Create branch from current clean `main`/v0.2.59 baseline:

```text
test/v0.2.62-native-routing
```

Push the verified source commits there only. Do not fast-forward or force-update `main`.

- [ ] **Step 6: Verify GitHub Actions Android build**

CI must use Android NDK `28.2.13676358`, ABI `arm64-v8a`, and produce:

```text
dist/arm64-v8a/levi-offhand-v0.2.62.levipack
```

Inspect job logs and require successful link of `liblevi_offhand.so`; do not infer success from artifact presence alone.

- [ ] **Step 7: Verify the packaged artifact**

Open the generated `.levipack` as ZIP and require exactly the expected core package entries:

```text
manifest.json
liblevi_offhand.so
```

Verify the manifest says `0.2.62` and `1.26.45.1`. Run `file` on the `.so` and require ARM aarch64 Android shared object.

- [ ] **Step 8: Produce user test packages**

Create:

```text
Offhand-Mod-v0.2.62-native-routing-full-repo.zip
Offhand-Mod-v0.2.62-native-routing-changed-files.zip
Levi-Offhand-v0.2.62-native-routing.levipack
```

The changed-files ZIP must include only files changed relative to baseline, including deleted-file information in a `DELETED_FILES.txt` entry naming:

```text
src/runtime/OffhandValidationHook.cpp
src/runtime/OffhandValidationHook.hpp
```

- [ ] **Step 9: Device validation checkpoint before any merge**

Ask the user to test from a fresh Minecraft process in this order:

```text
1. Stone inventory -> offhand manually: succeeds.
2. Stone offhand -> inventory manually: succeeds.
3. Craft with offhand empty: result never goes to offhand.
4. Pick up dropped items with offhand empty: pickup never auto-routes to offhand.
5. Manual UI swap involving offhand: no rollback.
6. Shield remains valid in offhand.
7. Leave/re-enter world: arbitrary offhand item persists.
8. Capture [NativeOffhandPolicy] and [AutoInsertRouting] logs if any step fails.
```

Do not merge to `main` until all three milestone success criteria are device-confirmed and the user explicitly approves the merge.

