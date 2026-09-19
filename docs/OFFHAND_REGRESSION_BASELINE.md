# Offhand Regression Baseline

This document is the regression lock for Levi Offhand action/storage work on Minecraft Bedrock 1.26.51.1.

The purpose is to prevent a later bug fix or feature from silently replacing behavior that was already proven in-game.

## Baseline ownership

The offhand interaction path is the primary subsystem. The F-style swap is an extension and must not redefine block placement, right-click ownership, slot-removal behavior, or render ownership.

Install order in `LeviOffhandMod.cpp` must keep:

```text
AutoInsertRouting / NativeOffhandPolicy
RightUseRouter
OffhandSwapRuntime
renderer
```

`RightUseRouter` must therefore be installed before the swap runtime.

## Proven block-placement behavior

Source baseline: commit `f687b626f2c941eb4ff3a6b2ce534a3cf0a3132d` (`fix: isolate offhand placement snapshot and honor main hold-use`).

Required behavior:

1. Left-click remains vanilla MAINHAND.
2. Right-click tries MAINHAND once.
3. OFFHAND block placement is only eligible when the MAINHAND use-on result is PASS.
4. MAINHAND hold-use items keep priority. Bow, Trident/Spear, food, shield and equivalent long-use actions must not be displaced by OFFHAND block fallback.
5. OFFHAND placement uses Minecraft's native use-on path with `hand=1`.
6. The live OFFHAND `ItemStack*` must never be passed directly as the transaction before-state.
7. Create a detached `0x98`-byte ItemStack snapshot using the native copy constructor at RVA `0xFF9D748`, use that snapshot for the native OFFHAND action, and destroy it with the native destructor at RVA `0x85ADF98`.
8. Do not replay the broad upper input dispatcher and do not synthesize inventory packets.

The core accepted shape is:

```text
MAIN use-on once
  handled -> return MAIN result
  PASS ->
    if MAIN owns hold-use -> return MAIN result
    read OFFHAND
    copy OFFHAND to detached snapshot
    native use-on(snapshot, hand=1)
    destroy snapshot
```

This snapshot rule fixed the earlier condition where a placed OFFHAND stack could become locked/unremovable.

## Storage policy

The current storage architecture must stay separate from the old ContainerValidation approach.

Required:

- Arbitrary items are natively offhand-capable through `NativeOffhandPolicy`.
- Automatic insertion filters OFFHAND from generic destination planning.
- Manual UI storage/removal stays native.
- Do not reintroduce `ContainerScreenValidation::tryTransfer`, `ContainerScreenValidation::trySwap`, manual pre-validation, or synthesized transaction packets as a shortcut.

## F-style swap baseline

The accepted swap pump executes from `ClientInstance::preFrameTick`, not from the Android HUD callback and not from a selected-item worker-thread call.

Required:

- HUD button only queues `mSwapRequested`.
- The Minecraft client frame drains the request.
- MAIN selected storage is written only through `Player::setSelectedItem`.
- OFFHAND storage is written only through the native hand=1 setter.
- Never use `setItemInHandSlot(hand=0)` in the swap path.
- Source stacks are copied into detached snapshots before the first mutation.
- `ItemStack::EMPTY_ITEM()` must be validated as null before use.

For occupied MAIN=A / OFF=B, use clear-both then refill:

```text
MAIN=A,     OFF=B
MAIN=EMPTY, OFF=B
MAIN=EMPTY, OFF=EMPTY
MAIN=EMPTY, OFF=A
MAIN=B,     OFF=A
```

The explicit OFFHAND clear is part of the regression lock. It prevents the replacement stack from inheriting stale OFFHAND slot state after an occupied↔occupied swap.

## Must-pass runtime matrix

After every change that touches action routing, storage, swap, selected-item access, or offhand rendering, test from a fresh Minecraft process:

1. MAIN sword/tool + OFF block: right-click places OFF block when MAIN has no right-click action.
2. MAIN Bow/Trident/food/shield + OFF block: MAIN action retains priority.
3. OFF block placement reduces/changes the real OFF stack and the remaining stack can be removed normally from slot 34.
4. MAIN item + empty OFF: F moves MAIN to OFF; F again returns it.
5. MAIN=A + OFF=B: repeated F alternates A/B without loss, ghost, or duplicate.
6. Immediately after occupied↔occupied F swap, open inventory and remove the new OFF item. It must not be locked.
7. After swap, OFF block right-click still follows the same snapshot-based placement logic.
8. Crafting/pickup must not automatically route generic items into OFFHAND.
9. Leave/re-enter world and confirm arbitrary OFFHAND storage persists.
10. Bow/Trident visual behavior must remain independent from storage/action fixes.

## Documentation rule

Whenever a future change modifies any of the following, update this file and `README.md` in the same commit series:

- offhand storage policy
- block placement/right-use routing
- selected-item/offhand slot mutation
- F-style swap
- Bow/Trident/offhand render ownership
- supported Minecraft binary/RVAs/signatures

If a new implementation replaces a known-good mechanism, document the old baseline, the reason it was replaced, the exact regression tests that were run, and the commit that becomes the new baseline.
