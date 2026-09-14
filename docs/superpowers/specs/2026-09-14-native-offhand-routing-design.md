# Native Offhand Routing Design — Minecraft Bedrock 1.26.45.1

Date: 2026-09-14

## Scope

This design replaces the legacy Levi Offhand storage architecture that hooks `ContainerScreenValidation`, manual pre-validation, and `ItemStackBase::getAllowOffHand`.

Target binary:

- Minecraft Bedrock Android 1.26.45.1
- ARM64 / arm64-v8a
- `libminecraftpe.so` SHA-256 `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`
- GNU Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`

The first milestone covers storage policy and automatic inventory routing only. Swap-button input, item-use semantics, and Bow/Trident rendering remain separate later subsystems.

## Goals

1. Arbitrary items can be manually placed in and removed from offhand through Minecraft's native transaction path.
2. Crafting results, pickups, and generic automatic insertion do not choose offhand as an automatic destination.
3. No `ContainerScreenValidation::tryTransfer`, `trySwap`, manual pre-validation, or scoped `getAllowOffHand` hook remains.
4. Existing vanilla transaction generation, rollback, synchronization, item locks, and packet/request semantics remain intact.
5. The implementation fails closed on a mismatched Minecraft build or unexpected ABI/fingerprint.

## Non-goals

- No item-use dispatcher changes in this milestone.
- No custom offhand Swap action in this milestone.
- No Bow/Trident pose or attachment changes in this milestone.
- No direct ItemStack writes or synthesized inventory packets.

## Reverse-engineering findings

### Native offhand capability

`Item::getAllowOffHand` is at RVA `0xF667898` and reads bit 7 (`0x80`) of the 16-bit field at `Item + 0x112`.

`Item::Item` initializes the relevant low flags at RVA `0xF65A3BC`:

```asm
mov w9, #0x50
```

Changing this one instruction to `mov w9, #0xD0` preserves the existing bits and adds `mAllowOffHand` natively.

The v0.2.60 device test proved the resulting policy is broad enough for manual arbitrary-item offhand storage, but also exposed the routing regression: crafting can now select offhand automatically.

### Offhand validation is downstream, not the correct routing boundary

`OffhandContainerValidation` is identified by RTTI `26OffhandContainerValidation`.

Its validation method at RVA `0xF6F5D10` accepts empty stacks, otherwise tail-branches to `ItemStackBase::getAllowOffHand` at `0xF644930`.

This explains the v0.2.60 regression: making `mAllowOffHand` natively true causes any automatic destination search that reaches the offhand container to see it as a valid destination.

The fix therefore must not be another `ContainerValidation` override. The destination search itself must exclude offhand for generic automatic insertion.

### Central automatic-insertion planner

RVA `0xF024024` is the central planner previously observed at runtime during crafting.

Static structure:

- Direct callsites are exactly:
  - `0xF023868`
  - `0xF02E09C`
  - `0xF03B900`
- Argument 4 is treated as a vector-like destination list:
  - function loads begin/end from `[arg4]`
  - outer destination iteration advances by `0x20` bytes at `0xF0240EC`
- For each destination/container, the function scans slot indices from 0 to the container's slot count.
- The first pass attempts insertion into already compatible stacks.
- A later pass searches empty slots.
- Planned destination operations are appended to the output vector passed in argument 1.
- The return value is the number of items planned/inserted.

This is the correct semantic boundary for Java-like behavior: manual placement can remain allowed while generic automatic insertion ignores offhand.

`FullContainerName` uses `ContainerEnumName` as its container identity. On this target, `OffhandContainer` is enum value `34` (`0x22`). The planner's destination elements are `0x20` bytes each; the routing layer will treat them as an opaque ABI record and inspect only the container-name byte needed to identify offhand.

## Architecture

### 1. `NativeOffhandPolicy`

Responsibility: make arbitrary item types natively offhand-capable.

Implementation:

- resolve the verified `Item::Item` instruction signature
- verify original bytes for `mov w9,#0x50`
- patch to `mov w9,#0xD0`
- retain the existing exact-build/fingerprint checks

This component must not hook `getAllowOffHand` and must not depend on any transaction/UI scope.

### 2. `AutoInsertRouting`

Responsibility: prevent generic automatic insertion from considering offhand.

Target: RVA `0xF024024` using a unique signature for the exact target build.

Recommended implementation: a single detour at the planner boundary.

The detour receives the original destination vector, creates a temporary read-only ABI view containing every destination except `ContainerEnumName::Offhand (34)`, then calls the original planner with that filtered view.

Important constraints:

- Never mutate the caller's original destination list.
- Never construct or destroy Minecraft `FullContainerName` objects.
- Copy each `0x20`-byte destination record as opaque bytes for the duration of the original call.
- Preserve the original record contents and pointers; source records remain alive during the synchronous call.
- If the vector header is malformed, element stride is inconsistent, allocation fails, or the target fingerprint does not match, call the original planner unchanged rather than risking corruption.
- If no offhand destination is present, call the original planner directly with zero-copy overhead.
- Emit diagnostic logging only on state/caller changes or first observation; do not log per slot.

This hook is not a `ContainerValidation` hook. It changes only the candidate set for an operation whose semantics are already automatic insertion.

### 3. Manual inventory transaction path

No mod hook is required.

With native `mAllowOffHand=true`, Minecraft's ordinary request/transaction validation should accept a player's explicit move or swap involving offhand.

Legacy hooks to delete:

- manual pre-validation at `0xF01AD14`
- `ContainerScreenValidation::tryTransfer` at `0xF705380`
- `ContainerScreenValidation::trySwap` at `0xF704CA0`
- scoped `ItemStackBase::getAllowOffHand` at `0xF644930`
- the old auto-add hook whose only purpose was to force the getter back to vanilla policy

The new auto-insert detour at `0xF024024` remains, but its purpose and implementation are completely different: it filters destination candidates rather than changing validation policy.

## Why this boundary is preferred

### Rejected: keep scoped `getAllowOffHand`

This places a hot global getter under thread-local policy and couples unrelated item capability checks to inventory context. It also recreates the old architecture's scope complexity.

### Rejected: hook `OffhandContainerValidation`

This directly violates the architectural goal and mixes manual acceptance with automatic routing. It cannot express "manual yes, automatic no" without external context.

### Rejected: patch the planner's internal AArch64 loop inline

An inline skip for enum 34 would be fast but needs a branch island/code cave or a larger instruction rewrite. That is more brittle across exact instruction layout changes and harder to reason about than one typed planner detour.

### Selected: native capability + planner candidate filtering

This separates two independent policies:

```text
can item exist in offhand?       -> Item native capability
should auto-insert choose it?     -> automatic routing candidate list
```

That separation matches the desired Java-style behavior and keeps manual transactions native.

## Component boundaries

Proposed source layout:

```text
src/runtime/
  NativeOffhandPolicy.hpp/.cpp
  AutoInsertRoutingCore.hpp
  AutoInsertRouting.hpp/.cpp
```

`OffhandValidationHook.*` is removed rather than retained under a misleading name.

Later milestones may add:

```text
  OffhandSwap.*
  OffhandInteraction.*
src/render/
  ... attachment context handling ...
```

Those later components must not depend on `ContainerValidation`.


### Lifecycle safety

Install `AutoInsertRouting` before applying `NativeOffhandPolicy`. This ensures the automatic-routing guard already exists before any Item can acquire the broadened native offhand capability. A runtime disable reverts the constructor patch for future Item construction but keeps the routing guard active until process restart, because already-constructed Item singletons can retain `mAllowOffHand=true`. The hook is removed only during unload/process teardown.

## Diagnostics

On startup:

```text
[NativeOffhandPolicy] Item default flags 0x50 -> 0xD0
[AutoInsertRouting] planner active RVA=0xF024024
[AutoInsertRouting] ContainerValidation hooks = 0
```

On the first automatic route containing offhand:

```text
[AutoInsertRouting] callerRva=0x... candidates=... offhandFiltered=1
```

No per-slot spam.

## Test strategy

### Static/source contracts

Fail if production source contains any of:

- `ContainerScreenValidation::tryTransfer`
- `ContainerScreenValidation::trySwap`
- `kManualSetPathSignature`
- `kTryTransferSignature`
- `kTrySwapSignature`
- scoped `allowOffhandDetour`
- `gManualSetDepth`, `gTransferDepth`, or `gAutoAddDepth`

Require:

- native constructor policy signature/patch
- one auto-insert planner signature
- offhand enum constant 34
- opaque destination record size `0x20`
- fail-safe path that calls the original planner unmodified

### Binary contracts

Verify against the supplied 1.26.45.1 binary:

- SHA-256 and Build ID
- constructor instruction at `0xF65A3BC`
- `Item::getAllowOffHand` bit extraction
- `OffhandContainerValidation` RTTI and method at `0xF6F5D10`
- planner entry at `0xF024024`
- exactly three direct planner callsites
- destination stride `0x20` at the outer loop

### Device matrix

Fresh process for each major policy test:

1. Stone inventory -> offhand manually: succeeds.
2. Stone offhand -> inventory manually: succeeds.
3. Swap an arbitrary inventory/mainhand stack with offhand manually through ordinary UI behavior: no rollback.
4. Craft with offhand empty: result goes to normal inventory/hotbar, never offhand.
5. Pick up dropped items with offhand empty: normal inventory routing, never offhand.
6. Quick-move/shift-like supported UI actions: offhand is not selected as a generic destination unless the action explicitly targets offhand.
7. Shield/native offhand item: remains valid.
8. Leave/re-enter world: manually placed arbitrary offhand item persists.
9. Multiplayer/server transaction rejection is reported separately from local storage routing; the mod must not synthesize packets to bypass server policy.

## Success criteria

Milestone 1 is complete only when all three conditions hold on device:

```text
manual arbitrary offhand placement = works
automatic craft/pickup -> offhand   = never
ContainerValidation hooks           = zero
```

Only after this milestone is stable should Swap, item-use semantics, and Bow/Trident rendering resume.
