# Java-like Offhand Action Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Java-like dual-hand gameplay action routing to Levi Offhand so Minecraft selects mainhand or offhand by the requested action capability while preserving mainhand priority, native item semantics, and long-action hand ownership.

**Architecture:** Keep storage/auto-insert/rendering independent. A pure `HandActionPolicy` makes deterministic hand-selection decisions; `ActionHandContext` owns thread-local scoped/session hand state; `NativeCapabilityProbe` binds only verified Minecraft 1.26.45.1 native capability boundaries; `HandActionRouter` installs semantic GameMode/action detours and calls the original native pipeline. Unsupported or unproven native boundaries fail closed to unchanged vanilla behavior.

**Tech Stack:** C++20, Android ARM64, Levi/preloader-android hook/signature APIs, Python binary/source contract tests, host-side g++ ASan/UBSan policy tests, exact Minecraft Bedrock 1.26.45.1 `libminecraftpe.so`.

**Spec:** `docs/superpowers/specs/2026-09-15-java-like-offhand-actions-design.md`

## Global Constraints

- Exact target: Minecraft Bedrock Android 1.26.45.1, ARM64 / arm64-v8a.
- Exact `libminecraftpe.so` SHA-256: `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`.
- Exact GNU Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`.
- Mainhand wins whenever both hands have the same relevant real capability.
- Right-click interaction priority: native block/entity interaction, then mainhand item-use, then offhand item-use, then vanilla fallback.
- Attack priority: mainhand real combat capability, then offhand real combat capability, then mainhand generic punch.
- Mining priority: mainhand target-sensitive native suitability, then offhand target-sensitive native suitability, then mainhand vanilla mining.
- No item-name capability tables, no physical main/offhand stack swap, no `ContainerValidation` action hook, no synthetic item-use packets, no renderer dependency, and no always-on selected-item redirection.
- Every native detour must pass through unchanged when its scoped context is inactive or when ABI/fingerprint validation is uncertain.
- Long-running actions never silently switch hands after start.
- Existing `NativeOffhandPolicy` and `AutoInsertRouting` behavior must remain intact.

---

### Task 1: Lock exact gameplay binary contracts before production hooks

**Files:**
- Create: `tests/v0268_action_routing_binary_contract.py`
- Create: `tests/v0268_action_routing_source_contract.py`
- Modify: `tests/run_native_attachment_fix_tests.sh`
- Modify: `docs/RE_NOTES.md`

**Interfaces:**
- Consumes: exact Minecraft 1.26.45.1 binary via `LEVI_MCPE_LIBRARY`.
- Produces: verified constants and fingerprints that later native code may use; unresolved semantic boundaries remain explicitly disabled rather than guessed.

- [ ] **Step 1: Write the failing binary-contract test for the already-proven GameMode boundaries**

Require exact SHA/Build ID and prove these functions by both entry fingerprint and their compiler-generated lambda RTTI/vtable references:

```text
GameMode::_attack(Actor&, bool, Vec3 const&)            RVA 0xEF721E4
GameMode::baseUseItem(ItemStack const&)                 RVA 0xEF75578
GameMode::baseUseItemAsAttack(ItemStack const&, Vec3 const&) RVA 0xEF75B9C
GameMode::releaseUsingItem()                            RVA 0xEF76108
```

The test must decode AArch64 `ADRP+ADD` references and assert the known lambda-vtable construction references inside each verified function range rather than accepting an RVA alone.

- [ ] **Step 2: Run the binary contract and verify the initial test fails until the exact fingerprints are encoded**

Run:

```bash
LEVI_MCPE_LIBRARY=/mnt/data/offhand_action_re/libminecraftpe.so \
python3 tests/v0268_action_routing_binary_contract.py
```

Expected before completion: FAIL on a missing fingerprint/reference assertion, never on a guessed address.

- [ ] **Step 3: Finish RE for `GameMode::interact` and destroy-block lifecycle before enabling those features**

Use the existing lambda RTTI strings and vtable construction references in the exact binary, then bind each candidate to its `.eh_frame` FDE range and inspect callers/callees. For `interact`, prove both `GameMode::interact(Actor&, Vec3 const&)` lambda types. For block destruction, prove the entry/cancel/continue/finalize boundaries through `_sendTryDestroyBlock(BlockPos const&, unsigned char)` and the surrounding GameMode call graph. Record only boundaries with unique caller/ABI/fingerprint evidence in `docs/RE_NOTES.md`.

If a semantic boundary cannot be uniquely proven, encode that feature as unavailable in the contract and keep its production hook disabled; do not substitute a nearby RVA.

- [ ] **Step 4: Add source-contract prohibitions before implementation exists**

`tests/v0268_action_routing_source_contract.py` must reject production code containing item-name capability tables, main/offhand swap/restore helpers, `ContainerValidation` action hooks, synthetic action packet construction, or unconditional selected-item replacement. It must later require scoped-action guards and original-call fallback paths.

- [ ] **Step 5: Add v0.2.68 contracts to the existing test runner and run the whole baseline suite**

Run:

```bash
LEVI_MCPE_LIBRARY=/mnt/data/offhand_action_re/libminecraftpe.so \
bash tests/run_native_attachment_fix_tests.sh
```

Expected: every existing v0.2.62-v0.2.67 contract remains PASS; the new binary contract reports exactly which action boundaries are verified/enabled.

- [ ] **Step 6: Commit**

```bash
git add tests/v0268_action_routing_binary_contract.py \
        tests/v0268_action_routing_source_contract.py \
        tests/run_native_attachment_fix_tests.sh docs/RE_NOTES.md
git commit -m "test: lock native action routing boundaries"
```

---

### Task 2: Implement pure Java-like hand-selection policy with TDD

**Files:**
- Create: `src/runtime/HandActionPolicy.hpp`
- Create: `tests/v0268_hand_action_policy_test.cpp`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Produces:
  - `enum class ActionHand { MainHand, OffHand };`
  - `enum class ActionKind { AttackEntity, MineBlock, UseAir, UseBlock, UseEntity };`
  - `struct CapabilitySet { bool mainReal; bool offReal; };`
  - `selectAttackHand(mainCombat, offCombat) -> ActionHand`
  - `selectMiningHand(mainSuitable, offSuitable) -> ActionHand`
  - `selectUseHand(mainHandled, offHandled) -> ActionHand`
- The interaction layer decides whether item routing is entered at all; `selectUseHand` therefore receives only item-use handled/capability outcomes.

- [ ] **Step 1: Write failing policy tests**

Cover at least:

```cpp
assert(selectAttackHand(true,  true)  == ActionHand::MainHand);
assert(selectAttackHand(true,  false) == ActionHand::MainHand);
assert(selectAttackHand(false, true)  == ActionHand::OffHand);
assert(selectAttackHand(false, false) == ActionHand::MainHand); // generic punch fallback

assert(selectMiningHand(true,  true)  == ActionHand::MainHand);
assert(selectMiningHand(false, true)  == ActionHand::OffHand);
assert(selectMiningHand(false, false) == ActionHand::MainHand); // vanilla fallback

assert(selectUseHand(true,  true)  == ActionHand::MainHand);
assert(selectUseHand(false, true)  == ActionHand::OffHand);
assert(selectUseHand(false, false) == ActionHand::MainHand); // vanilla fallback
```

- [ ] **Step 2: Run the policy test and verify it fails because the policy header does not exist**

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined \
    -I./src tests/v0268_hand_action_policy_test.cpp -o /tmp/v0268-policy-test
```

- [ ] **Step 3: Implement the minimal constexpr policy**

Keep `HandActionPolicy.hpp` free of Minecraft ABI types and item names. Mainhand priority must be explicit in the branch order.

- [ ] **Step 4: Run the host policy test and full test runner**

Expected: PASS under ASan/UBSan and no regression in existing tests.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/HandActionPolicy.hpp tests/v0268_hand_action_policy_test.cpp \
        tests/run_native_attachment_fix_tests.sh
git commit -m "feat: add Java-like hand action policy"
```

---

### Task 3: Add scoped and long-running action-hand state with TDD

**Files:**
- Create: `src/runtime/ActionHandContext.hpp`
- Create: `src/runtime/ActionHandContext.cpp`
- Create: `tests/v0268_action_hand_context_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Consumes: `ActionHand`, `ActionKind` from `HandActionPolicy.hpp`.
- Produces:
  - `ScopedActionHand(ActionHand hand, ActionKind kind)` with nested-scope restoration.
  - `currentScopedAction() -> optional<ActionContextView>`.
  - `ActionSessionState` with `None`, `Mining`, `UsingItem`, `Charging`, `Blocking`.
  - begin/end/cancel session operations carrying hand, stack identity token, slot/container identity, target token, and start tick.

- [ ] **Step 1: Write failing tests for nested scopes and session invariants**

Tests must prove: inactive by default; offhand scope visible only inside scope; nested scope restores previous state; session keeps its original hand; replacing stack/target makes `matches(...)` false; `cancel()` clears state; no implicit hand switch occurs.

- [ ] **Step 2: Run the test and verify RED**

Compile with `-pthread -fsanitize=address,undefined`; expected failure is missing `ActionHandContext` symbols/header.

- [ ] **Step 3: Implement thread-local synchronous scope plus explicit session state**

Do not store raw `ItemStack*` as long-lived session identity. Store opaque identity values supplied by the native adapter so sessions cannot dereference stale stack pointers.

- [ ] **Step 4: Run policy/context tests plus the full suite**

Expected: PASS; source contract must confirm no global unconditional selected-item redirect was introduced.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ActionHandContext.hpp src/runtime/ActionHandContext.cpp \
        tests/v0268_action_hand_context_test.cpp CMakeLists.txt \
        tests/run_native_attachment_fix_tests.sh
git commit -m "feat: add scoped offhand action context"
```

---

### Task 4: Implement guarded native capability adapter and instant item-use routing

**Files:**
- Create: `src/runtime/NativeCapabilityProbe.hpp`
- Create: `src/runtime/NativeCapabilityProbe.cpp`
- Create: `src/runtime/HandActionRouter.hpp`
- Create: `src/runtime/HandActionRouter.cpp`
- Modify: `src/LeviOffhandMod.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/v0268_action_routing_source_contract.py`
- Create: `tests/v0268_hand_action_router_core_test.cpp`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Consumes: binary-contract verified boundaries, `HandActionPolicy`, `ActionHandContext`.
- Produces:
  - `HandActionRouter::install/uninstall/setFeatureEnabled` following existing runtime-component style.
  - native ItemStack acquisition for current mainhand and actual offhand without moving slots.
  - native use result classification (`handled` versus `pass/fail`) through the verified GameMode item-use path.

- [ ] **Step 1: Write a router-core test using fake callbacks**

Inject fake `tryUse(MainHand)` / `tryUse(OffHand)` callbacks. Assert call order is MAIN then OFF; OFF is never called when MAIN handles; OFF handles when MAIN passes; neither-hand handling falls back exactly once to the supplied vanilla path. The test must also assert an offhand `ScopedActionHand` exists only while the offhand native callback runs.

- [ ] **Step 2: Run and verify RED**

Expected: missing router-core interface.

- [ ] **Step 3: Implement the callback-level router core, then make the host test GREEN**

Keep this core free of RVAs and Minecraft structs so ordering is independently testable.

- [ ] **Step 4: Bind only exact verified 1.26.45.1 `baseUseItem` / `baseUseItemAsAttack` / `releaseUsingItem` boundaries**

Install hooks only after signature/fingerprint validation. When no routed context applies, immediately call the original function with original arguments. For right-click item-use, preserve the original mainhand attempt; invoke an offhand native attempt only when the verified mainhand path reports non-handled and the actual offhand stack can be acquired through a proven native accessor. Never mutate inventory slots to make the offhand look selected.

- [ ] **Step 5: Integrate lifecycle without making storage depend on router success**

`LeviOffhandMod::enable` keeps `AutoInsertRouting` and `NativeOffhandPolicy` as the required storage path. Install `HandActionRouter` afterward. If action-router installation fails, log that Java-like actions are unavailable but keep stable storage enabled. On disable, cancel active action state before disabling the router. On unload, remove action hooks before storage teardown.

- [ ] **Step 6: Run source/binary/core/full tests**

Expected: transparent passthrough is source-contract visible; existing storage tests stay green.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/NativeCapabilityProbe.* src/runtime/HandActionRouter.* \
        src/LeviOffhandMod.cpp CMakeLists.txt tests/v0268_* \
        tests/run_native_attachment_fix_tests.sh
git commit -m "feat: route native offhand item use"
```

---

### Task 5: Add real-combat attack routing without treating generic punch as capability

**Files:**
- Modify: `src/runtime/NativeCapabilityProbe.hpp`
- Modify: `src/runtime/NativeCapabilityProbe.cpp`
- Modify: `src/runtime/HandActionRouter.cpp`
- Modify: `tests/v0268_hand_action_router_core_test.cpp`
- Modify: `tests/v0268_action_routing_binary_contract.py`
- Modify: `docs/RE_NOTES.md`

**Interfaces:**
- Consumes: verified `GameMode::_attack` entry at RVA `0xEF721E4` and a separately proven native combat-capability/attribute boundary.
- Produces: attack routing that distinguishes actual combat capability from the universal vanilla punch fallback.

- [ ] **Step 1: Add RED tests for attack ordering**

Fake native combat probes must produce the agreed cases: both real => MAIN; main only => MAIN; off only => OFF; neither => MAIN generic fallback. Verify offhand attack executes inside an offhand scope and main generic fallback remains untouched when neither stack has real combat capability.

- [ ] **Step 2: Prove the native real-combat capability boundary before binding it**

Trace `_attack` and the held-item attribute/component reads used to compute attack semantics. Accept a capability probe only if it can distinguish Bow/Apple generic punch from Sword/Axe real weapon behavior without item-name tests. Add its entry/callsite fingerprint to the binary contract. If the proof is absent, leave offhand attack routing disabled while right-click routing remains available.

- [ ] **Step 3: Implement the native probe and `_attack` hand scope**

Mainhand is checked first. Only an offhand real-combat result may redirect the attack action to offhand. Neither result must preserve original mainhand generic punch. Pass-through remains exact when feature/context is inactive.

- [ ] **Step 4: Run all tests and commit**

```bash
git add src/runtime/NativeCapabilityProbe.* src/runtime/HandActionRouter.cpp \
        tests/v0268_hand_action_router_core_test.cpp \
        tests/v0268_action_routing_binary_contract.py docs/RE_NOTES.md
git commit -m "feat: route native offhand attacks"
```

---

### Task 6: Add target-sensitive mining hand session

**Files:**
- Modify: `src/runtime/NativeCapabilityProbe.hpp`
- Modify: `src/runtime/NativeCapabilityProbe.cpp`
- Modify: `src/runtime/HandActionRouter.cpp`
- Modify: `tests/v0268_hand_action_router_core_test.cpp`
- Modify: `tests/v0268_action_routing_binary_contract.py`
- Modify: `docs/RE_NOTES.md`

**Interfaces:**
- Consumes: Task 1's uniquely proven GameMode destroy-block lifecycle and native target-block tool suitability/destroy-speed boundary.
- Produces: a `Mining` `ActionSession` bound to one hand from start through progress/finalize/cancel.

- [ ] **Step 1: Add RED mining-session tests**

Test main suitable + off suitable => MAIN; main unsuitable + off suitable => OFF; neither => MAIN vanilla; target change cancels; active stack identity change cancels; repeated progress for the same target keeps the starting hand.

- [ ] **Step 2: Extend binary contract with the exact destroy lifecycle and tool-suitability fingerprints**

The production feature stays disabled until all lifecycle edges needed for clean cancel/finalize are proven. Do not implement a partial offhand mining session that can start but cannot reliably finish/cancel.

- [ ] **Step 3: Bind mining start/progress/finalize/cancel to `ActionSession`**

Use the actual target block for native suitability/effectiveness. Keep the offhand scope active only around native operations that consume held-tool semantics. Never copy the offhand stack into selected mainhand storage.

- [ ] **Step 4: Run all tests and commit**

```bash
git add src/runtime/NativeCapabilityProbe.* src/runtime/HandActionRouter.cpp \
        tests/v0268_hand_action_router_core_test.cpp \
        tests/v0268_action_routing_binary_contract.py docs/RE_NOTES.md
git commit -m "feat: route native offhand mining"
```

---

### Task 7: Add long-use hold/release/cancel ownership

**Files:**
- Modify: `src/runtime/HandActionRouter.cpp`
- Modify: `src/runtime/NativeCapabilityProbe.cpp`
- Modify: `tests/v0268_hand_action_router_core_test.cpp`
- Modify: `tests/v0268_action_routing_binary_contract.py`
- Modify: `docs/RE_NOTES.md`

**Interfaces:**
- Consumes: verified `GameMode::releaseUsingItem()` at RVA `0xEF76108`, instant-use routing, `ActionSession`.
- Produces: hand-locked long-use lifecycle for native Bow/Food/Shield/Trident/Spear-like actions without item-name branching.

- [ ] **Step 1: Add RED tests for hold/release/cancel**

Start offhand use, run multiple update ticks, release, and assert every callback observes OFFHAND. Replace the initiating stack and assert native cancel fires once and release does not execute on the replacement. Disable feature during a session and assert safe cancel + state clear.

- [ ] **Step 2: Prove active-use update and cancel/finish boundaries**

`releaseUsingItem` alone is insufficient. Extend the binary contract with the exact active-use update and cancel/finish path before enabling long-use offhand sessions. If either boundary is unresolved, keep long-use rerouting disabled rather than faking completion.

- [ ] **Step 3: Implement session ownership using native result/state, not item names**

Determine long-use start from native handled/use state. Preserve selected hand through native updates and release. Let native cooldown/durability/use duration operate on the actual routed stack.

- [ ] **Step 4: Run all tests and commit**

```bash
git add src/runtime/HandActionRouter.cpp src/runtime/NativeCapabilityProbe.cpp \
        tests/v0268_hand_action_router_core_test.cpp \
        tests/v0268_action_routing_binary_contract.py docs/RE_NOTES.md
git commit -m "feat: preserve offhand long-use sessions"
```

---

### Task 8: Final lifecycle, CI, packaging, and device-validation handoff

**Files:**
- Modify: `README.md`
- Modify: `manifest.json`
- Modify: `scripts/build.sh`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/run_native_attachment_fix_tests.sh`
- Modify: source files only if verification reveals a concrete issue.

**Interfaces:**
- Produces: a buildable v0.2.68 test package plus diagnostics suitable for device verification.

- [ ] **Step 1: Run the exact full local verification**

```bash
LEVI_MCPE_LIBRARY=/mnt/data/offhand_action_re/libminecraftpe.so \
bash tests/run_native_attachment_fix_tests.sh
```

Also compile production with the project Android NDK/Levi build path and inspect the resulting ARM64 ELF.

- [ ] **Step 2: Require useful low-volume diagnostics**

Startup logs show action-router installation or exact fingerprint failure. Transition logs show action kind + chosen hand and long-session start/finish/cancel. No per-tick spam.

- [ ] **Step 3: Update version/docs without claiming device behavior**

Document v0.2.68 as a gameplay-routing test build. State which subfeatures are enabled by proven binary boundaries and which remain fail-closed if any boundary is still unresolved.

- [ ] **Step 4: Run GitHub Actions and inspect the actual run and artifact**

Do not report CI success until the run and artifact are fetched and verified. Verify `.levipack` contains exactly `manifest.json` and `liblevi_offhand.so` and that the ELF is ARM64 Android.

- [ ] **Step 5: Prepare device matrix**

Test at minimum: Sword+Axe attack; Bow+Sword attack; Apple+Sword attack; Apple+Apple punch; Pickaxe+Pickaxe mining; Bow+Pickaxe mining; Bow+Food right-use; non-use+Food right-use; chest/door/lever/villager priority; offhand Bow/Food/Shield/Trident/Spear hold/release; active-stack move cancellation; feature-disable cancellation.

- [ ] **Step 6: Commit packaging/docs changes**

```bash
git add README.md manifest.json scripts/build.sh .github/workflows/build.yml \
        tests/run_native_attachment_fix_tests.sh
git commit -m "build: package v0.2.68 action routing test"
```

Do not call the feature fully fixed until device results prove the selected hand, actual stack mutation, native action result, and swing/hand semantics agree.
