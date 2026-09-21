## September 21 animation/shears reset

Renderer experiments that touched gameplay state remain retired. The only reintroduced freeze paths are pure-render boundaries proven by RE: renderFirstPerson MAIN arm-height publication and the exact read-only swing-progress interpolation call at `0xB2FC394 -> 0xF286ED8`. LocalPlayer swing suppression, upper-use hooks and final-matrix experiments remain forbidden.

Shears is a routing invariant: Item::mId is read at +0xAA and Item::mMaxStackSize at +0xA8; current Shears raw ID is 424 with stack size 1. This check runs before the attack-only fallback and must keep MAINHAND ownership. CI includes an executable Shears case that must produce only a MAINHAND call and no OFFHAND block placement.

# Offhand Regression Baseline

## Consumption/release implementation candidate

Parent: `2638d88fedcffa0314aa4df51e4ee0273b5c4aa7`. This candidate preserves current renderer, classification and placement-count code.

The old statement that only the getter is redirected is superseded for completion/release by scoped native setter routing. The session getter outside these scopes remains a separate isolation concern.

| Boundary | Verified 1.26.51.1 RVA | Use |
|---|---|---|
| Player completion | `0xF9E8094` | Original effect, callback envelope and clear-use retained |
| Completion callback | `0xFA05E30` | Calls ItemStack useTimeDepleted, then virtual MAIN setter |
| ItemStack useTimeDepleted | `0xFF9F520`, Item virtual `+0x2B8` | Native consumption/container conversion |
| MAIN setter | `0xF9F7850`, Player virtual `+0x268` | Redirect only while matching player is completing/releasing OFF |
| Native hand setter | `0xF579C50` | Hand 1 invokes virtual OFF setter `+0x278` |
| LocalPlayer OFF setter | `0xAAD0360` | Calls `0xF9FC7C8`, which records container-119 InventoryAction |
| Stop using | `0xF9E86C0` | Cancel stale item use without consuming/releasing |
| Hand transaction wrapper | `0xF9E9DFC` | Preserve original envelope/callback/context with hand 1 for OFF release |
| Release callback | `0xF8A65FC` | Only this callback qualifies for release-hand correction |

Completion previously called `Player::getSelectedItem`, consumed a detached stack, then wrote through MAIN virtual `+0x268`. The new completion scope keeps native behavior but directs that final write through the native OFF setter. Empty stacks and native replacement items are forwarded intact. The mod does not subtract counts or construct replacement items itself.

The scope keeps a native snapshot of the original OFF stack; writeback is rejected if a nested callback changed slot identity/count. It never falls back to writing the consumed stack into MAIN. A session by itself cannot redirect setters or hand transactions. Stale active use cancels. Other-player sessions are not cleared by an inferred native OFF completion.

GameMode release hardcodes `w1=0` at `0xF8A3350`. The new transaction hook changes this only inside the matching OFF release scope and only when callback equals `0xF8A65FC`. All callback objects are moved/destructed by the original native wrapper.

`tests/use_lifecycle_126511_binary_test.py` checks the exact supplied ELF, four new function prologues, native BL boundaries, LocalPlayer/ServerPlayer setter vtables, the hardcoded release hand and OFF container recording. It requires no third-party Python packages.

The host suite executes production detours with fake native callbacks. It does **not** verify hunger, status effects, actual bowl/bottle semantics, projectile behavior, packet acceptance or persistence. Native completion is conditional on game-side state, so both client and integrated-server behavior require device testing. Remote-server support remains unproven; no new packet schema or artificial transaction is introduced.

Must verify on device before treating this as a gameplay baseline: survival food count/hunger; final food; potion/milk/stew replacement; full inventory; creative consumption; MAIN priority; changing OFF mid-use; bow/trident charge/release and durability; reconnect/slot removal. Existing sword, shears and placement-count tests remain mandatory.


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

## Placement animation phase 1: MAINHAND FPP swing-progress freeze

The `0xB2F66D8` matrix-cache experiment is retired. Static xrefs show that helper is not the active `ItemInHandRenderer::renderFirstPerson` MAINHAND swing path.

Verified render-only path:

```text
1.26.45.1 renderFirstPerson   0xADE96B0
1.26.45.1 swing BL            0xADEA394 -> 0xEA8DEFC
1.26.45.1 return              0xADEA398

1.26.51.1 renderFirstPerson   0xB2FB6C0
1.26.51.1 swing BL            0xB2FC394 -> 0xF286ED8
1.26.51.1 return              0xB2FC398
```

The target is a read-only interpolation helper. At this exact FPP callsite its return is immediately consumed by the MAINHAND `sqrt`/`sin` matrix composition. The optional detour returns `0.0f` only for this exact caller while `OffhandPlacementAnimation` is active; otherwise it calls the original helper.

Safety invariants: no Player/ItemStack writes; no `LocalPlayer::swing`, upper-use, block-use, or `RightUseRouter` hook; exact FPP render-callsite guard; optional installation only; Banner build-#470 routing, Crossbow FPP, Sword placement, Shears ownership, and block-count reconciliation stay untouched.


## FPP placement-animation invariant

The current animation layer is intentionally narrow:

- state lives in `runtime/OffhandPlacementAnimation.hpp`;
- `RightUseRouter` may only call `trigger()` after `(offResult & 1u) != 0`;
- duration is 220 ms and timing uses `steady_clock`;
- renderer applies motion only while an OFFHAND block is rendered in `FIRSTPERSON_LEFT`;
- animation changes the render matrix only; it must not write ItemStack/storage state;
- repeated placement restarts the short visual impulse;
- F-swap is isolated in its own preFrame runtime and must not alter this animation/render path.

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

## Isolated F-swap candidate

Swap is split into three independent layers:

- `src/ui/SwapButton.*`: ButtonBuilder UI only; currently code-styled and ready for image-backed UI later.
- `src/swap/SwapRuntime.*`: queue + `ClientInstance::preFrameTick` pump only.
- `src/swap/SwapEngine.*`: native selected/OFFHAND storage exchange only.

The build-#596 failure was caused by validating `Player::getSelectedItem` at `0xF9F7824` after `RightUseRouter` had already hooked that entry. The new engine never validates/calls that function. Static RE of 1.26.51.1 shows the getter reads Player `+0x570`, then selected-state `+0xB0/+0xB8/+0x10`, and dispatches the container getter at vtable `+0x40`. Swap reproduces that read locally, so it no longer joins the RightUseRouter hook chain.

Storage mutation remains the user-tested 44a design: selected hotbar only through `Player::setSelectedItem`, OFFHAND only through `setItemInHandSlot(hand=1)`, detached snapshots before mutation, and occupied MAIN=A/OFF=B uses `MAIN=EMPTY -> OFF=A -> MAIN=B`. No clear-both step, ContainerValidation, packet synthesis, right-use hook, or renderer hook is added.


## F-swap one-empty synchronization invariant

New device evidence supersedes the earlier assumption that any `ItemStack::isNull()==true` object is interchangeable across hand containers.

Minecraft 1.26.51.1 native setters already perform inventory bookkeeping. RE shows LocalPlayer OFFHAND write `0xAAD0360` calls `0xF9FC7C8`, which builds an InventoryAction for container ID `119 (0x77)` before the low-level OFF slot write. `Player::setSelectedItem @ 0xF9F7850` separately owns the selected-container mutation.

Therefore the swap engine must preserve container ownership of the empty transition:

- MAIN -> empty uses canonical `ItemStack::EMPTY_ITEM @ 0x134C6780`, never the OFFHAND slot's null-like stack object.
- OFFHAND -> empty uses the same canonical EMPTY_ITEM, never the selected hotbar slot's null-like stack object.
- Detached snapshots remain mandatory before the first mutation.
- Occupied MAIN=A / OFF=B retains the accepted 44a sequence and must not gain the rejected clear-both intermediate step.
- No synthetic packet, ContainerValidation hook, ItemStackRequest action, RightUseRouter coupling, or renderer change is permitted for this fix.

Device checks for this candidate: repeat empty MAIN/OFF -> MAIN at least 20 times without loss; repeat MAIN -> empty OFF and immediately drag the resulting OFF item to inventory/hotbar; then repeat occupied A/B swaps to verify the 44a path remains unchanged.

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

15. Repeated F swap with one hand empty returns the same item back and forth without ghost/duplicate/loss.
16. Occupied MAIN=A / OFF=B repeated F swaps alternate A/B correctly.
17. Immediately after occupied swap, the new OFFHAND item can be moved/removed normally and right-click behavior still follows the existing MAINHAND ownership rules.

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

