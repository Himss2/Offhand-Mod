# Java-Like Offhand Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Java-like dual-hand action routing so native Minecraft gameplay selects mainhand or offhand based on action capability, with mainhand priority when both hands are valid, while preserving native packets, durability, cooldowns, interaction handling, and long-running use/mining sessions.

**Architecture:** Keep storage policy and rendering independent. Add a runtime action subsystem with a pure selection core, native capability probes, a scoped effective-hand context, and an action-session state machine. Hook only verified Minecraft 1.26.45.1 gameplay boundaries; do not swap inventory stacks, synthesize packets, hook ContainerValidation, or use renderer state as gameplay truth.

**Tech Stack:** C++20, Levi/Preloader Android hook APIs, Android ARM64, Python source/binary contract tests, exact Minecraft Bedrock 1.26.45.1 `libminecraftpe.so`.

**Spec:** `docs/superpowers/specs/2026-09-15-java-like-offhand-actions-design.md`

## Global Constraints

- Target Minecraft Bedrock Android 1.26.45.1 ARM64 only.
- Mainhand wins whenever mainhand and offhand both have the requested real capability.
- Right-click block/entity interaction is attempted before item-use routing.
- Right-click item-use order after interaction pass: mainhand, then offhand, then vanilla fallback.
- Entity attack prefers real combat capability mainhand, then offhand, then mainhand generic punch.
- Mining prefers a suitable/effective mainhand tool, then a suitable/effective offhand tool, then vanilla mainhand mining.
- Capability detection must use native Minecraft behavior/metadata, not hardcoded item-name lists.
- Long-running actions lock the chosen hand until completion/cancel.
- Never physically swap/copy mainhand and offhand stacks to route an action.
- Never synthesize gameplay packets.
- Never add ContainerValidation hooks.
- Existing `NativeOffhandPolicy`, `AutoInsertRouting`, and renderer behavior remain independent unless integration wiring is required.
- Fail closed on target/signature/fingerprint mismatch.

---

### Task 1: Pure Hand Selection Policy

**Files:**
- Create: `src/runtime/HandActionRoutingCore.hpp`
- Create: `tests/hand_action_routing_core_test.cpp`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Produces: `enum class ActionHand { Vanilla, Main, Off };`
- Produces: `selectAttackHand(bool mainCombat, bool offCombat) -> ActionHand`
- Produces: `selectMiningHand(bool mainSuitable, bool offSuitable) -> ActionHand`
- Produces: `selectUseHand(bool mainHandled, bool offHandled) -> ActionHand`

- [ ] **Step 1: Write failing policy tests**

```cpp
static_assert(selectAttackHand(true, true) == ActionHand::Main);
static_assert(selectAttackHand(false, true) == ActionHand::Off);
static_assert(selectAttackHand(false, false) == ActionHand::Main); // generic punch fallback

static_assert(selectMiningHand(true, true) == ActionHand::Main);
static_assert(selectMiningHand(false, true) == ActionHand::Off);
static_assert(selectMiningHand(false, false) == ActionHand::Main); // vanilla mining fallback

static_assert(selectUseHand(true, true) == ActionHand::Main);
static_assert(selectUseHand(false, true) == ActionHand::Off);
static_assert(selectUseHand(false, false) == ActionHand::Vanilla);
```

- [ ] **Step 2: Run the test and verify failure**

Run:
```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc tests/hand_action_routing_core_test.cpp -o /tmp/hand_action_routing_core_test
```
Expected: fail because `HandActionRoutingCore.hpp` and policy functions do not exist.

- [ ] **Step 3: Implement the minimal constexpr policy**

```cpp
#pragma once

namespace levioffhand::runtime {

enum class ActionHand { Vanilla, Main, Off };

[[nodiscard]] constexpr ActionHand selectAttackHand(bool mainCombat, bool offCombat) noexcept {
    if (mainCombat) return ActionHand::Main;
    if (offCombat) return ActionHand::Off;
    return ActionHand::Main;
}

[[nodiscard]] constexpr ActionHand selectMiningHand(bool mainSuitable, bool offSuitable) noexcept {
    if (mainSuitable) return ActionHand::Main;
    if (offSuitable) return ActionHand::Off;
    return ActionHand::Main;
}

[[nodiscard]] constexpr ActionHand selectUseHand(bool mainHandled, bool offHandled) noexcept {
    if (mainHandled) return ActionHand::Main;
    if (offHandled) return ActionHand::Off;
    return ActionHand::Vanilla;
}

} // namespace levioffhand::runtime
```

- [ ] **Step 4: Run the policy test and add it to the project test runner**

Expected: PASS with `-Werror`.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/HandActionRoutingCore.hpp tests/hand_action_routing_core_test.cpp tests/run_native_attachment_fix_tests.sh
git commit -m "test: lock dual-hand action selection policy"
```

### Task 2: Reverse-Engineer Exact Gameplay Boundaries and Add Binary Contracts

**Files:**
- Create: `tests/hand_action_binary_contract.py`
- Modify: `docs/RE_NOTES.md`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Produces verified RVAs/signatures for attack entity, start/continue/destroy block, use item/use-on, release/cancel use, held-stack lookup or hand-bearing boundary, and swing/hand packet path.
- Consumes exact target SHA-256 `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557` and Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`.

- [ ] **Step 1: Add a failing binary-contract skeleton that verifies exact binary identity and requires named gameplay boundaries**

```python
REQUIRED = {
    "attack_entity": None,
    "start_destroy_block": None,
    "continue_destroy_block": None,
    "destroy_block": None,
    "use_item": None,
    "use_item_on": None,
    "release_using_item": None,
}
assert all(v is not None for v in REQUIRED.values())
```

- [ ] **Step 2: Run against `LEVI_MCPE_LIBRARY` and verify it fails because addresses are not yet locked**

- [ ] **Step 3: RE the exact 1.26.45.1 binary and document each boundary with caller/callee evidence and instruction fingerprints**

For every locked address, record:
```text
symbol/semantic name
RVA
AArch64 fingerprint bytes
verified direct callers/callees
relevant argument registers / return semantics
why this boundary is narrow enough for routing
```

Reject any candidate whose semantics cannot distinguish the intended action safely.

- [ ] **Step 4: Encode the verified RVAs/fingerprints in `hand_action_binary_contract.py`**

The test must fail on wrong SHA/Build ID or any instruction mismatch.

- [ ] **Step 5: Run binary contract and existing suite**

Expected: all PASS on the exact supplied binary.

- [ ] **Step 6: Commit**

```bash
git add tests/hand_action_binary_contract.py docs/RE_NOTES.md tests/run_native_attachment_fix_tests.sh
git commit -m "re: lock 1.26.45.1 hand action boundaries"
```

### Task 3: Native Capability Probe Layer

**Files:**
- Create: `src/runtime/NativeActionCapability.hpp`
- Create: `src/runtime/NativeActionCapability.cpp`
- Create: `tests/native_action_capability_source_contract.py`
- Modify: `CMakeLists.txt`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Produces: `struct NativeActionCapability` singleton lifecycle `install/uninstall`.
- Produces: `hasCombatCapability(ItemStack const&) -> bool`.
- Produces: `isSuitableForBlock(ItemStack const&, Block const&) -> bool`.
- Produces only probes that call verified native Minecraft semantics; no item-name/category tables.

- [ ] **Step 1: Write source-contract tests that reject hardcoded item-name classification and require verified signature constants**

Require production source to contain the exact binary fingerprints from Task 2 and reject strings such as `minecraft:sword`, `minecraft:pickaxe`, `minecraft:bow`, and lookup tables keyed by item names.

- [ ] **Step 2: Run source contract and verify it fails**

- [ ] **Step 3: Implement narrow native probe wrappers using the verified functions from Task 2**

Each wrapper must:
```text
1. Validate resolved address belongs to libminecraftpe.so.
2. Validate instruction fingerprint before enabling.
3. Return false if unavailable/mismatched.
4. Never mutate ItemStack/Block during a probe.
```

- [ ] **Step 4: Build with Android NDK and run source/binary contracts**

Expected: PASS without introducing new global getter hooks.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/NativeActionCapability.* tests/native_action_capability_source_contract.py CMakeLists.txt tests/run_native_attachment_fix_tests.sh
git commit -m "feat: add native action capability probes"
```

### Task 4: Scoped Effective-Hand Context and Session State

**Files:**
- Create: `src/runtime/ActionHandContext.hpp`
- Create: `src/runtime/ActionHandContext.cpp`
- Create: `tests/action_hand_context_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Produces: `enum class ActionKind { None, AttackEntity, MineBlock, UseAir, UseBlock, UseEntity };`
- Produces: `ScopedActionHand(ActionKind, ActionHand)` RAII context.
- Produces: session API `beginSession`, `matchesSession`, `cancelSession`, `completeSession` for long-running use/mining actions.
- Produces thread-local/narrow state only; inactive context must be pass-through.

- [ ] **Step 1: Write failing tests for nested RAII restoration and session locking**

Cover:
```text
inactive -> Main scoped -> Off nested -> Main restored -> inactive restored
begin Offhand use session -> repeated tick stays Off
active stack identity mismatch -> session cancels
mining target changes -> mining session cancels
```

- [ ] **Step 2: Run and verify failure**

- [ ] **Step 3: Implement minimal RAII + session state with no Minecraft dependencies in the core state machine**

- [ ] **Step 4: Run unit tests under `-Wall -Wextra -Wpedantic -Werror`**

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ActionHandContext.* tests/action_hand_context_test.cpp CMakeLists.txt tests/run_native_attachment_fix_tests.sh
git commit -m "feat: add scoped action hand sessions"
```

### Task 5: Attack and Mining Routing Hooks

**Files:**
- Create: `src/runtime/HandActionRouter.hpp`
- Create: `src/runtime/HandActionRouter.cpp`
- Create: `tests/hand_action_router_source_contract.py`
- Modify: `CMakeLists.txt`
- Modify: `src/LeviOffhandMod.cpp`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Consumes `NativeActionCapability`, `ActionHandContext`, and pure selection policy.
- Produces `HandActionRouter::install/uninstall/setFeatureEnabled`.
- Attack selection: main real combat -> off real combat -> main generic punch.
- Mining selection: main suitable -> off suitable -> main vanilla.

- [ ] **Step 1: Write failing source contract requiring exact verified attack/mining hooks and forbidding physical inventory swaps**

Reject production strings/APIs that indicate direct slot swapping/copying for action routing.

- [ ] **Step 2: Run contract and verify failure**

- [ ] **Step 3: Implement attack hook with a scoped selected hand around the native attack call**

The hook must not replace damage calculation; it only selects which real held stack/hand native gameplay sees.

- [ ] **Step 4: Implement mining start/continue/destroy routing with a locked Mining session**

`startDestroyBlock` chooses a hand once. `continueDestroyBlock` and final destroy reuse it while stack identity and target still match. On mismatch, call the native cancel/reset path and clear the session.

- [ ] **Step 5: Run tests, static contracts, and NDK build**

- [ ] **Step 6: Commit**

```bash
git add src/runtime/HandActionRouter.* tests/hand_action_router_source_contract.py CMakeLists.txt src/LeviOffhandMod.cpp tests/run_native_attachment_fix_tests.sh
git commit -m "feat: route attack and mining by effective hand"
```

### Task 6: Right-Click Interaction and Item-Use Routing

**Files:**
- Modify: `src/runtime/HandActionRouter.cpp`
- Modify: `tests/hand_action_router_source_contract.py`
- Create: `tests/hand_action_use_policy_test.cpp`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Interaction first: native block/entity interaction receives its normal chance before item fallback.
- Item-use order after interaction pass: mainhand native attempt; if native return is pass/not-handled, offhand native attempt; otherwise mainhand wins.
- Long-running item use begins a locked session on the hand whose native call handled the action.

- [ ] **Step 1: Add failing tests for main-priority use semantics**

Cover:
```text
main handled + off handled -> Main
main pass + off handled -> Off
main pass + off pass -> Vanilla
interaction handled -> no second-hand item attempt
```

- [ ] **Step 2: Run and verify failure against router integration contract**

- [ ] **Step 3: Implement use/use-on routing using native return semantics rather than pre-classifying item names**

Mainhand is attempted first. Only an explicit native pass/not-handled result permits an offhand attempt.

- [ ] **Step 4: Start a use session on the successful hand and keep native update/release paths on that hand**

- [ ] **Step 5: Run all tests and NDK build**

- [ ] **Step 6: Commit**

```bash
git add src/runtime/HandActionRouter.cpp tests/hand_action_router_source_contract.py tests/hand_action_use_policy_test.cpp tests/run_native_attachment_fix_tests.sh
git commit -m "feat: add main-priority dual-hand item use"
```

### Task 7: Native Held-Stack, Packet-Hand, Swing, and Release Propagation

**Files:**
- Modify: `src/runtime/HandActionRouter.cpp`
- Modify: `src/runtime/ActionHandContext.*`
- Modify: `tests/hand_action_router_source_contract.py`
- Modify: `tests/hand_action_binary_contract.py`

**Interfaces:**
- During active scoped/session context, only verified action-path stack/hand lookups may resolve to offhand.
- Native packet/use/swing/release path must carry the selected hand without synthetic packets.
- Context inactive -> 100% original behavior.

- [ ] **Step 1: Extend contracts to require narrow caller-scoped hand propagation and reject unconditional global selected-item spoofing**

- [ ] **Step 2: Run and verify failure**

- [ ] **Step 3: Hook the smallest verified hand-bearing/held-stack boundaries from Task 2 and gate every override on `ActionHandContext::active()` plus verified call scope**

- [ ] **Step 4: Route native swing/release/cancel to the selected hand and clear session only after the native lifecycle finishes**

- [ ] **Step 5: Run full suite + NDK build**

- [ ] **Step 6: Commit**

```bash
git add src/runtime/HandActionRouter.cpp src/runtime/ActionHandContext.* tests/hand_action_router_source_contract.py tests/hand_action_binary_contract.py
git commit -m "feat: propagate effective hand through native action lifecycle"
```

### Task 8: Lifecycle Integration, Diagnostics, and Device Validation Build

**Files:**
- Modify: `src/LeviOffhandMod.cpp`
- Modify: `README.md`
- Modify: `manifest.json`
- Modify: `scripts/build.sh`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Install order: existing storage guards/policy -> `NativeActionCapability` -> `HandActionRouter` -> renderer.
- Runtime toggle disables new action routing safely and cancels active sessions before returning to vanilla behavior.

- [ ] **Step 1: Add lifecycle/source assertions for install, disable, and uninstall ordering**

- [ ] **Step 2: Wire the subsystem into `LeviOffhandMod.cpp` and Mod Menu toggle**

Expected startup diagnostics:
```text
[NativeActionCapability] active
[HandActionRouter] attack/mining/use routing active
[HandActionRouter] no ContainerValidation hooks
```

- [ ] **Step 3: Bump version and update README with an explicit experimental device-test matrix**

Device cases:
```text
Sword + Axe, left-click entity -> main Sword
Bow + Sword, left-click entity -> off Sword
Apple + Apple, left-click entity -> main generic punch
Pickaxe + Pickaxe, mine stone -> main Pickaxe
Bow + Pickaxe, mine stone -> off Pickaxe
Bow + Food, right-click air -> main Bow
Stone + Food, right-click air -> off Food
interactive chest + usable offhand -> chest interaction first
start offhand Bow, hold, release -> same offhand session through release
move active offhand item during hold -> native cancel, no hand failover
```

- [ ] **Step 4: Run the full clean verification suite against the exact binary**

Run:
```bash
LEVI_MCPE_LIBRARY=/path/to/libminecraftpe.so bash tests/run_native_attachment_fix_tests.sh
```
Expected: all old storage/render contracts and all new hand-action contracts PASS.

- [ ] **Step 5: Build `.levipack`, inspect archive and ELF, and verify manifest version**

- [ ] **Step 6: Commit**

```bash
git add src/LeviOffhandMod.cpp README.md manifest.json scripts/build.sh .github/workflows/build.yml tests/run_native_attachment_fix_tests.sh
git commit -m "build: package java-like offhand action routing"
```

## Self-Review

- Spec coverage: selection priorities, native interaction-first behavior, native capability probes, mining, attack, use, long-running sessions, cancellation, native packets/swing/release, lifecycle isolation, and no stack swapping are all assigned to explicit tasks.
- Placeholder scan: no implementation step depends on an unspecified item-name table or a future renderer fix.
- Type consistency: `ActionHand`, `ActionKind`, `NativeActionCapability`, `ActionHandContext`, and `HandActionRouter` are defined before consuming tasks and use consistent names throughout.
