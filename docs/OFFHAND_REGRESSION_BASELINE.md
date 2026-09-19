# Offhand Regression Baseline

## Authoritative recovery point

For Minecraft Bedrock 1.26.51.1, the current action/storage baseline is the exact pre-swap commit:

`198787f5b0d750fd4c89ba00ac0aab5c72018331`
`fix: route sword like axe when no native right-click exists`

This commit is the recovery authority for `RightUseRouter`, its header, the action-routing source contract, `LeviOffhandMod.cpp`, `CMakeLists.txt`, and the build workflow.

Do not reconstruct these mechanisms from memory or from a later swap commit.

## Block placement / right-use invariants

The known-good logic first decides whether MAINHAND genuinely owns right-click **before** invoking the generic MAINHAND use-on wrapper.

Required behavior:

1. Specialized native right-click owners keep MAINHAND priority. This includes Bow/Trident/Fishing Rod/Shears and hold-use items such as food/shield where applicable.
2. Attack-only Sword/Axe/Pickaxe-style items with no specialized native right-click action yield to OFFHAND.
3. If MAINHAND does not own right-click and OFFHAND is non-empty, OFFHAND gets the first and only initial `GameMode::useItemOn` attempt.
4. That OFFHAND attempt uses native `hand=1`.
5. Never pass the live OFFHAND `ItemStack*` as the transaction before-state.
6. Copy OFFHAND into a detached `0x98` ItemStack snapshot using the native copy constructor, pass the snapshot to the native use-on call, then destroy it with the native destructor.
7. If OFFHAND handles the action, return that result.
8. If OFFHAND passes, call the untouched vanilla MAINHAND wrapper once as fallback.
9. Left-click remains vanilla MAINHAND.
10. Do not replay the broad upper input dispatcher and do not synthesize inventory packets.

The critical order is:

```text
classify MAINHAND ownership
  MAIN owns right-click -> vanilla MAINHAND use-on once
  MAIN does not own right-click ->
    read OFFHAND
    snapshot OFFHAND
    native OFFHAND use-on(snapshot, hand=1)
      handled -> return OFFHAND result
      PASS    -> vanilla MAINHAND fallback once
```

## Storage / slot-removal invariant

The detached OFFHAND snapshot is not optional. It is part of the known-good solution for preventing the real OFFHAND slot from becoming transaction-locked/unremovable after block placement.

Manual offhand storage/removal must remain native. Do not reintroduce ContainerValidation transfer/swap hooks or synthetic inventory packets as a shortcut.

## Swap quarantine

The F-style swap experiment was introduced **after** the authoritative baseline and caused regressions in action/storage behavior.

Until a new swap implementation passes the full runtime matrix below, it must remain isolated:

- not listed in `CMakeLists.txt`;
- not included or installed from `LeviOffhandMod.cpp`;
- no swap button registered;
- no swap hook attached to `Player::getSelectedItem`, `ClientInstance::preFrameTick`, right-use, block placement, or storage validation;
- no mutation of OFFHAND/selected storage from swap code during normal gameplay.

Experimental swap source may remain in the repository for reference, but it is non-runtime code in the recovery baseline.

## Must-pass runtime matrix

Start from a fresh Minecraft process for every candidate baseline:

1. MAIN Sword + OFF block: right-click places the OFF block.
2. MAIN Axe/Pickaxe + OFF block: right-click places the OFF block when MAIN has no native right-click action.
3. MAIN Bow/Trident/Fishing Rod/Shears/food/shield + OFF block: MAIN action keeps priority where vanilla defines one.
4. After OFFHAND block placement, the remaining OFF stack can be moved or removed normally from slot 34.
5. Repeated OFFHAND placement updates the real stack count and never creates a ghost/locked stack.
6. Left-click always remains MAINHAND.
7. Crafting/pickup does not auto-route generic items into OFFHAND.
8. Manual arbitrary-item placement/removal in OFFHAND still works.
9. Leave and re-enter the world: OFFHAND storage remains valid.
10. Bow/Trident rendering remains independent from action/storage behavior.

Only after all ten pass may swap be reintroduced. When that happens, add swap-specific tests **without replacing or weakening this baseline**.

## Documentation rule

Any change that touches:

- right-use classification,
- block placement,
- OFFHAND storage/removal,
- selected-item access,
- swap,
- supported Minecraft RVAs/signatures,
- Bow/Trident/offhand render ownership,

must update both `README.md` and this file in the same change. Record the previous known-good commit and the new validated baseline commit after in-game verification.
