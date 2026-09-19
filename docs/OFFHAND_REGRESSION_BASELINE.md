# Offhand Regression Baseline

## Authoritative recovery point

For Minecraft Bedrock 1.26.51.1, the current action/storage baseline is the exact pre-swap commit:

`198787f5b0d750fd4c89ba00ac0aab5c72018331`
`fix: route sword like axe when no native right-click exists`

This commit is the recovery authority for `RightUseRouter`, its header, the action-routing source contract, `LeviOffhandMod.cpp`, `CMakeLists.txt`, and the build workflow.

Do not reconstruct these mechanisms from memory or from a later swap commit.

## Runtime fingerprint / hook-chain invariant

The supplied Minecraft 1.26.51.1 binary has been rechecked directly: the documented RVAs and fingerprints for RightUseRouter match the file on disk.

At runtime, another already-installed hook may change the first instructions of a function before RightUseRouter installs. Therefore:

- non-hook helper targets (offhand getter, stack-null check, active-use helpers, stack comparator, ItemStack copy constructor/destructor) must still pass exact fingerprint validation;
- those stable helpers form the exact 1.26.51.1 build guard;
- only after that guard passes may the four hookable entry points use their known RVA when their live prologue differs;
- hookable targets are `Player::getSelectedItem`, `GameMode::useItemOnBlock`, `GameMode::baseUseItem`, and `GameMode::releaseUsingItem`;
- a live-prologue mismatch must be logged explicitly;
- this compatibility layer must not change the action-routing or block-placement algorithms below.

## Pre-swap action fixes under verification

Before swap is reintroduced, the following two behaviors are mandatory:

1. **OFFHAND food/self-use:** when MAINHAND has no concrete native right-click owner (including ordinary Sword/Axe/Pickaxe paths), do not execute generic MAINHAND `baseUseItem` first. Attempt OFFHAND with native `hand=1`; if it enters active-use state, pin the OFFHAND long-use session through release. Only if OFFHAND passes may vanilla MAINHAND run once.
2. **Pre-hooked block-use compatibility:** if `GameMode::useItemOnBlock` was already patched before RightUseRouter installs, keep the detached OFFHAND snapshot and `hand=1`, but scope nested `Player::getSelectedItem` lookups to OFFHAND only for that chained call. If the entry point was clean, do not spoof selectedItem; use the proven snapshot-only path.

Do not add explicit eating or placement animation hooks until both mechanics above pass in-game. Animation must be layered on top of working native actions, not used to mask a failed transaction.

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
