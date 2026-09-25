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


## F-swap install regression: setSelected target already hooked

Systematic-debugging evidence:

1. Device runtime log: all SwapEngine targets validate except `setSelected=0`; the engine disables itself before an F request can execute.
2. Static 1.26.51.1 binary check: the 48-byte fingerprint at `0xF9F7850` matches exactly on disk.
3. Install order: `RightUseRouter::install()` runs before `SwapRuntime::install()`.
4. RightUseRouter installs a HookHandle on `Player::setSelectedItem @ 0xF9F7850`, so the in-memory prologue is expected to differ by the time SwapEngine validates it.
5. RightUseRouter's `setSelectedItemDetour` immediately forwards to the original setter unless an explicit OFFHAND completion/release writeback scope is active.

Root cause: SwapEngine incorrectly treated an expected in-process hook as a game-version fingerprint failure.

Fix boundary: keep exact fingerprints for offhand getter, null test, ItemStack copy/dtor, and OFF setter. Only `setSelectedItem` receives a known-build live-RVA fallback after those stable guards pass. Do not change swap mutation ordering in the same candidate.

The previous canonical-empty experiment is rolled back for this diagnostic candidate so device testing changes only one variable: whether the isolated swap runtime becomes available again.

## Build #610 one-empty swap synchronization

Known-good base: GitHub Actions build #610, commit `0a9a75ceadcc523832035100ebbcd9cbcfd02849`.

Systematic-debugging trace:

1. #610 validates and activates the isolated F runtime; the previous `setSelectedItem` install failure is resolved.
2. MAIN->OFF and occupied A/B swaps execute, so button/preFrame/resolver/action ownership are not the failing boundary.
3. The remaining failures are symmetric around one-empty OFFHAND transitions: F-created OFFHAND state is not normally removable, while manually-created OFFHAND state does not swap cleanly back to empty MAIN.
4. In #610 the MAIN->OFF branch clears selected storage with the *OFFHAND slot's live empty stack object*; the OFF->MAIN branch clears OFFHAND with the *selected slot's live empty stack object*.
5. Native RE confirms these setters own different inventory transitions. LocalPlayer OFFHAND setter `0xAAD0360` records container 119 through `0xF9FC7C8`; selected storage is handled by `Player::setSelectedItem @ 0xF9F7850`.
6. Hypothesis under test: crossing those null-like live ItemStack objects between containers desynchronizes predictive/native transaction identity even though `ItemStack::isNull` is true.

Candidate change is intentionally one variable: canonical `ItemStack::EMPTY_ITEM @ 0x134C6780` is used only for the two one-empty clear operations. The occupied 44a path remains `MAIN=EMPTY -> OFF=old MAIN -> MAIN=old OFF` with no OFFHAND clear-both intermediate.

Required device checks after CI: (a) MAIN item -> empty OFF, then manually drag the resulting OFF item out; (b) manually insert an item into OFF with MAIN empty, press F repeatedly and confirm OFF->MAIN; (c) repeat occupied A/B swaps to prove the accepted path is unchanged.

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

## Bow/Crossbow FPP native-first ownership candidate

Known-good gameplay base remains build #711/#714 (`f9b75172b9fc7ecbb30801ec460a7dcb704f9f91`). Scope is renderer-only and restricted to Bow/Crossbow first-person offhand ownership.

Observed device failure with visual/model packs: Bow and Crossbow show two models. The old FPP path unconditionally forced generic-left admission when vanilla's hand-equip predicate returned false, while third-party packs could independently supply an attachable.

Candidate ownership rule:

1. During the normal FPP pass, exact offhand Bow/Crossbow dispatch is native-only; Levi does not force generic admission.
2. `prepareAttachmentDetour` marks ownership only when slot 6, first-person and enabled actually enter the native attachment pipeline.
3. Bow/Crossbow right-item attachment bindings may use the existing right->left owner remap; already-left bindings are left unchanged.
4. If native attachment preparation occurred, no fallback is drawn.
5. If no native attachment preparation occurred, `renderOffhandDetour` invokes exactly one generic fallback.
6. During that fallback, native Bow/Crossbow attachment routing is suppressed to prevent recursive/double ownership.
7. Existing final local XYZ/rotation calibration remains active; no gameplay/storage/swap code is changed.

Required device checks: Bow and Crossbow with no visual pack, then with Actions & Stuff/XNova/HMI-style packs. Each must render exactly one offhand model. Also verify Bow/Crossbow use animations and the build-#711/#714 F-swap/manual inventory behavior remain unchanged.

## OFFHAND food consumption — exact inline use-tick bridge

Parent gameplay/render baseline remains `12447d1ad3a6d9ad96ef817636aea58b295cab20`. Swap code is unchanged.

Device result for rejected build #748: OFF food still did not progress and gameplay became noticeably heavier while moving. The expected getter-bridge log never appeared.

Exact RE correction:

1. Player virtual tick `0xF9E6358..0xF9E7E14` reaches active-use validation at `0xF9E7090`.
2. At `0xF9E70B8` it loads selected state. If `selectedState+0xB0 != 0` (empty MAIN), `0xF9E70C4..0xF9E70C8` selects global `EMPTY_ITEM` and **never calls Inventory::getItem**.
3. Only non-empty selected MAIN reaches `0xF9E71A0..0xF9E71B0` and virtual Inventory getter `+0x40`. Therefore the #748 global `0xF883B58` hook could not solve the empty-MAIN food case and merely added hot-path overhead.
4. From `0xF9E71B8` onward x20 is the validation stack. It is checked for validity, compared with active-use at `Player+0x6D8`, checked against saved selected metadata `Player+0x770/+0x774`, then duration logic eventually calls `completeUsingItem @ 0xF9E8094`.
5. Start-use `0xF9E8D94` stores selected empty-flag/index into `Player+0x770/+0x774` but does not store the native hand enum, so only the x20 stack source must be extended; the saved metadata can remain vanilla MAIN continuity state.

Build #751 exposed an ABI mistake in the first inline stub. At `0xF9E70A4` vanilla sets `x0 = Player+0x6D8` and calls `ItemStack::isNull @ 0xFFA0F70`; therefore at `0xF9E70B8` x0 is the boolean return value, while x19 is still the Player pointer. The old stub called the C++ helper without restoring x0.

The corrected candidate fingerprints original bytes `0xF9E70B8..0xF9E70D0` (28 bytes) and emits: `mov x0,x19`; literal-load helper; `blr x16`; `mov x20,x0`; branch to untouched `0xF9E71B8`; 8-byte helper literal. The following vanilla path has no second selected-stack fetch: it validates x20, compares it with active-use `Player+0x6D8`, verifies saved selected metadata `+0x770/+0x774`, progresses use duration, and eventually calls `completeUsingItem`. Helper fallback uses the original `Player::getSelectedItem` trampoline, preserving vanilla MAIN behavior. OFF is selected only for explicit OFF session ownership or unique native OFF active-use ownership.

The rejected global Inventory getter HookHandle is removed. Patch removal is paired with RightUseRouter uninstall. Feature-disable leaves the patch installed but helper returns MAIN, avoiding executable-code churn while toggling.

Required device checks: FPS/movement first (must return to baseline), then manual/F-swapped food with MAIN empty, hold-to-finish, release early, stack decrement, last item, container replacement and MAIN preservation.


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



## F-swap paired legacy InventoryAction candidate — 1.26.51.1

This candidate starts cleanly from `debug/swap-sync-610`
(`2d3dfdfa98fb25b08081777a8cce155a6bf7ef20`) after device testing rejected
both the raw-container bridge and the Place-vs-Swap ItemStackRequest theory.

Static RE of the exact 1.26.51.1 binary now identifies the asymmetric
transaction boundary that explains the one-empty ghost/locked state:

- LocalPlayer constructs its selected inventory container at `0xF9E0AA0`.
  Constructor `0xF881CB0` stores the owning Player at container `+0x158`.
- That selected container's installed vtable dispatches `setItem +0x68` to
  `0xF9DA128 -> +0x70 -> 0xF9DA138`, which reaches transaction-aware
  `0xF8834D4`.
- `0xF8834D4` calls `0xF9FD618` before the local write. That helper builds
  a legacy `InventoryAction` with source type Container, container ID `0`,
  and the selected slot, then submits it through
  `InventoryTransactionManager::addAction @ 0x1001EC24`.
- LocalPlayer OFFHAND wrapper `0xF9FC7C8` normally builds the matching
  container `119 (0x77)` InventoryAction, but when modern
  ItemStackNetManager mode is active it branches directly to the low-level OFF
  writer `0xF579C24` and skips that action.
- `InventoryTransactionManager::addAction` accepts legacy actions while
  modern networking is enabled as long as no modern request is active. It keeps
  an unbalanced first action and sends through Player vcall `+0x718` only
  after subsequent actions make the transaction balance.

The F engine therefore no longer synthesizes ItemStackRequest actions and no
longer uses raw container vtable writes. It keeps `Player::setSelectedItem`
for the MAIN/HOTBAR side, constructs exactly the native OFFHAND container-119
InventoryAction layout used at `0xF9FC818..0xF9FC874`, submits that action to
the embedded transaction manager at Player `+0x9B8`, and writes OFF through
the same low-level `0xF579C24` used by vanilla after bookkeeping.

For occupied A/B the accepted 44a local order is preserved:
hotbar A->EMPTY, OFF B->A, hotbar EMPTY->B. The three actions are allowed to
remain pending until their item balance reaches zero; the engine logs
`[SwapEngine][legacy-txn] ... settled=1` only when the transaction manager has
sent/cleared the completed transaction.

The bridge fails closed before any mutation when an unrelated legacy
InventoryTransaction is already pending or a modern ItemStackRequest owns the
ItemStackNetManager. RightUseRouter, NativeOffhandPolicy, SwapRuntime, HUD UI,
renderer, Sword/Shears routing, placement, consumption and release are
unchanged.


### 2026-09-24 screen-aware F-swap bookkeeping candidate

The production baseline remains build #667/#662. This candidate changes only the F-swap transaction bookkeeping in `SwapEngine`; `RightUseRouter`, block placement, renderer, `NativeOffhandPolicy`, auto-insert routing, HUD input and the proven #662 local storage order are unchanged.

Root-cause evidence now points to missing ItemStackNetManager touched-slot ownership rather than an ItemStack copy problem. The earlier #645 experiment called `ItemStackNetManagerClient::_addLegacyTransactionRequestSetItemSlot` with the wrong second argument (a `Player*`). Current generated LeviLamina headers confirm the native signature is `(ItemStackNetManagerScreen&, ContainerType, int)`.

The candidate opens the exact Player-aware legacy request wrapper at `0xF88A434`, resolves the active native top screen, and registers both affected player-container slots before the unchanged #662 mutation: `ContainerType::Inventory (-1)` with selected hotbar slot 0-8, and `ContainerType::Hand (19)` with offhand local slot 0. It then closes that native scope before the existing #662 post-normalization. No public OFF setter is introduced into the swap body, and the raw OFF writer/container-119 action path stays unchanged.

Native scope cleanup follows the recovered libc++ `std::function` storage mode: inline callable destruction uses vtable `+0x20`, heap callable destruction uses `+0x28`. Callable vtables are mapping-validated before any method dereference.

This remains a device-test candidate until MAIN→OFF, OFF→MAIN and occupied A/B swaps prove: no loss/duplication/rollback, both resulting slots detach normally without right-click healing, and existing OFF block placement/right-use behavior remains unchanged.


#### Exact 1.26.51.1 RE correction: current screen resolver

The uploaded `libminecraftpe.so` was verified as ARM64 NDK r28c with Build ID `712509dc14ccc233e91f267937dfb46ecdcc4b68`. Its dynamic symbol table is stripped, so the previous attempt to resolve `_ZN23ItemStackNetManagerBase13_getTopScreenEv` by name cannot work on this binary.

Static binary analysis establishes `0xF88AA24` as the current-screen resolver used by the legacy predictive request lifecycle: `ItemStackNetManagerClient::_tryBeginClientLegacyTransactionRequest @ 0xF88D960` directly calls `0xF88AA24` at `0xF88D97C` before writing the generated negative-even request id to `manager+0x50`. The mod therefore resolves `0xF88AA24` only through its exact 32-byte fingerprint.

The same binary also confirms `0xF88CE8C` maps legacy `ContainerType::Inventory (-1)` to player container enum 29 and `ContainerType::Hand (19)` to Offhand enum 34 before recording the requested slot. No gameplay storage writer or right-use route is changed by this correction.
