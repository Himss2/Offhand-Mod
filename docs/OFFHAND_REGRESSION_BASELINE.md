# Offhand Regression Baseline

## Build #550 preservation rule for targeted fixes

The preservation baseline is successful GitHub Actions **build #550**, commit `2a3a9a7e1a11ab53899e46923aee62e6cc208acc`. Its Sword -> OFFHAND placement ordering must not be rewritten while repairing the isolated regressions below.

- Crossbow may join Bow's exact FPP generic-dispatch admission, but Trident/Spear native-3D paths stay unchanged.
- Banner rendering must not write `ItemStack::mBlock` from historical BannerItem offsets `+0x1C0/+0x1C8`. The supplied 1.26.51.1 tombstone is consistent with an invalid small Block pointer reaching native block/id lookup.
- Banner's custom FPP pose is renderer-only and may run through the safe `BridgeScope`: scale `1.56`, X `-0.78`, Y `-0.28`, yaw `180°`. The bridge-depth recursion guard must exempt only Banner; all other bridged special families retain the existing guard.
- After the exact stable 1.26.51.1 guards pass, a pre-hooked `GameMode::useItemOnBlock` entry is chainable compatibility state, not a warning condition.
- Detached OFFHAND placement snapshots remain mandatory. On accepted placement, if the snapshot count differs from the live slot count, reconcile through native `Actor::setItemInHandSlot` RVA `0xF579C50` with `hand=1`. ItemStackBase `mCount` is byte `+0x22`.
- Do not introduce packets, physical hand swapping, ContainerValidation hooks, or a new action-routing order to solve these four defects.

Required regression checks include: Sword + OFF block routing unchanged, Crossbow visible in FPP OFFHAND, Banner insert/render without crash, no warning-level pre-hook diagnostic, and an OFFHAND placement count transition such as 16 -> 15.

## Candidate status and provenance

Historical pre-swap recovery: `198787f5b0d750fd4c89ba00ac0aab5c72018331`.
Current candidate starts from `7d0846d93bd04821e011ab8e3a62a62caacbc957` and corrects routing and build defects found in that source. No device-validated baseline is claimed here. Swap remains quarantined.

## Verified native evidence (1.26.51.1)

Binary SHA-256: `b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4`.
GNU Build ID: `712509dc14ccc233e91f267937dfb46ecdcc4b68`.

| Evidence | Address / slot | Implication |
|---|---|---|
| WeaponItem primary vtable | `0x1306D7D8`, use `+0x290` | Target `0xFD66F30` is `mov x0,x1; ret`; this override does not own a click |
| Item primary vtable | `0x1307B730` | use `+0x290` -> `0xFF8429C`, useOn `+0x418` -> `0xFF84B84` |
| ComponentItem primary vtable | `0x1306EE98` | use `+0x290` -> `0xFDA8274`, useOn `+0x418` -> `0xFDA8A20` |
| GameMode release transaction | `0xF8A3350` | Hardcodes hand zero before wrapper `0xF9E9DFC` |
| Release callback carried setter | `0xF8A679C` | Calls virtual `+0x268` (MAIN setter); getter redirection is insufficient |

`tests/right_use_126511_binary_test.py` verifies the exact binary, all eleven production fingerprints, and use/no-op vtable identities. The release findings are investigation evidence, not a completed lifecycle fix.

## Routing invariants in this candidate

1. Attack-only MAIN items with no concrete right-use implementation yield to OFF. WeaponItem's no-op override is explicitly excluded.
2. MAIN owners get one native attempt. A nonzero native block result is returned without replay or OFF attempt. Native result values are preserved; this is not a complete translation of Java InteractionResult.
3. MAIN block-only PASS can try OFF. MAIN self-use owners keep their opportunity in the upper native dispatcher before OFF block-use. Coordination after that self-use also passes remains unfinished.
4. Both OFF block-use and air-use pass native `hand=1` with a detached `0x98` ItemStack copied/destroyed through native functions. Air-use acquires a fresh snapshot only when its OFF attempt begins.
5. If OFF passes, MAIN fallback runs once, or reuses the previously obtained MAIN result. Never replay a completed MAIN call.
6. An explicit MAIN scope overrides an existing OFF long-use session.
7. A pre-hooked block-use chain alone gets a narrow OFF selected-item scope; the clean native block path uses the snapshot and native hand argument.
8. No inventory swap, synthetic packets, or upper input dispatcher replay is introduced.

Exact stable helper fingerprints still gate installation before chaining pre-hooked entry points. Helper mismatches disable routing. Hook prologue mismatches are logged.

## Renderer build repair

The animation commit restored a stale tracked renderer but left the newer overlay attachment header in place. CI run #549 failed with missing attachment symbols. This candidate uses the paired v0.2.67 materialized sources:

- renderer before animation: Git blob `3a3ea8cde3a0ae3df2376fff3c1bd0a9aa3c43be`;
- attachment helper: Git blob `deb12b1f33aa36e91d143d996ea92c415604f128`.

The placement lambda is declared only after the vanilla early-return guard. CI restores both renderer and matching headers together. Compatibility generation still translates RVAs into a build copy. Its test now reads working source, not unchanged `git show HEAD`, and checks generation leaves all three source files untouched.

## Unresolved lifecycle requirements

The existing OFF long-use session is not a complete Java hand-ownership implementation. Native release still chooses a MAIN transaction and setter. Session-based getter redirection also extends beyond explicit action scopes. Do not claim food completion, bow/trident release, container replacement, durability, attack isolation or world-change cancellation are fixed by the initial-use snapshot.

A follow-up must establish the native transaction envelope and hand-aware writeback for use tick, completion and release, then verify these paths on device. Re-enabling clear-both/refill swap setters is not a substitute.

## Automated checks versus device checks

`tests/run_right_use_runtime_tests.sh` compiles the production router into a host executable with fake ABI objects and UndefinedBehaviorSanitizer. It covers sword no-op classification, MAIN block PASS/success/nonzero result, both hands passing, detached OFF air-use, MAIN success without touching OFF, bow self-use priority, explicit MAIN scope and disabled behavior. These checks protect call order and snapshot lifetime; they cannot prove native inventory transactions.

The source contracts and existing host policy/context/router/auto-insert tests remain required. The Android CI build must also succeed. None replaces the gameplay matrix below.

## Banner build-#470 rendering baseline

Banner FPP must follow the accepted build #470 **logic**, not its old binary addresses.

- Preserve the scoped `StackBlockOverride` strategy so Minecraft selects the block-transform path used by the accepted Banner pose.
- Minecraft 1.26.51.1 BannerItem constructor stores the cached wall/standing Block pointers at `+0x1D0/+0x1D8` (stores at `0x1000B5D8` and `0x1000B5F0`).
- Never restore build #470's old `+0x1C0/+0x1C8` offsets.
- Keep the override scoped to `renderOffhandDetour` and restore `ItemStack::mBlock` immediately after the original call.
- Retain the accepted Banner matrix calibration: scale `1.56`, X `-0.78`, Y `-0.28`, yaw `180°`.
- Do not port any 1.26.45.1 signatures/RVAs while applying this logic.

## Renderer-only animation safety invariant

Do not hook `LocalPlayer::swing`, upper-use, block-use, selected-item access, or any gameplay/action function from `src/render`. A rejected experiment at the LocalPlayer swing boundary altered return/control-flow semantics and regressed Shears: MAINHAND Shears must retain its specialized native right-click action and must not fall through to OFFHAND block placement.

This is now a source-contract rule. Placement-animation work may inspect Freecam's first-person render predicates as RE references, but implementation must stay on a render-only boundary that cannot affect hand ownership or action routing.

## Placement animation phase 1: MAINHAND visual freeze

This phase is renderer-only. Do not modify RightUseRouter, storage, block count reconciliation, Banner routing, or tool-family rendering.

Verified 1.26.51.1 FPP helper:

```text
per-hand FPP renderer 0xB2F7F18
OFFHAND callsite       0xB2F6B48 -> hand=0
MAINHAND callsite      0xB2FC2E8 -> hand=1
equip height           ItemInHandRenderer +0x180
old equip height       ItemInHandRenderer +0x184
swing interpolator     0xF286ED8
swing current          Player +0x3EC
swing previous         Player +0x430
```

During `OffhandPlacementAnimation`, the optional renderer hook may affect only the `hand=1` MAINHAND draw. It pins the MAINHAND equip-height pair and temporarily writes zero to the two swing-progress fields so native `0xF286ED8` returns neutral swing progress. Swing and equip fields must all be restored immediately after the original draw. The OFFHAND `hand=0` invocation must not clear the freeze latch while the placement window is active. The optional hook must never become part of mandatory renderer readiness; if unavailable, all pre-existing visual paths continue unchanged.

Do not tune the OFFHAND placement matrix until this freeze behavior is verified on-device.

## FPP placement-animation invariant

The current animation layer is intentionally narrow:

- state lives in `runtime/OffhandPlacementAnimation.hpp`;
- `RightUseRouter` may only call `trigger()` after `(offResult & 1u) != 0`;
- duration is 220 ms and timing uses `steady_clock`;
- renderer applies motion only while an OFFHAND block is rendered in `FIRSTPERSON_LEFT`;
- animation changes the render matrix only; it must not write ItemStack/storage state;
- repeated placement restarts the short visual impulse;
- swap remains completely quarantined.

Current initial calibration at the animation midpoint:

```text
translation X +0.08
translation Y -0.18
translation Z +0.12
rotation X    -20 deg
rotation Y     +9 deg
rotation Z     +7 deg
```

These numbers are visual calibration values and may be tuned after device testing without changing action/storage semantics.

## Storage / slot-removal invariant

The detached OFFHAND snapshot is mandatory to avoid aliasing the transaction before-state with the live slot. Actual slot removal and count reconciliation still require device verification.

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

## Must-pass runtime matrix (not yet run on device)

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

11. OFF food/drink: hold, finish, cancel, change slot, and receive the correct container item without modifying MAIN.
12. OFF bow/trident: charge and release with the correct projectile, durability and slot transaction.
13. While OFF is in use, attack/mining still reads MAIN; changing world or disabling the mod cannot retain a stale player/session.
14. MAIN self-use PASS followed by OFF block/entity interaction follows the full ordered pipeline.

Only after this matrix passes may swap be reintroduced. When that happens, add swap-specific tests **without replacing or weakening this baseline**.

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


Renderer review also corrected six 1.26.51.1 callsite translations for spear admission and native owner-vector/matrix lookup. Their BL targets are verified by the binary test.
