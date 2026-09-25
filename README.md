### September 21 stability reset

The unsuccessful MAINHAND freeze experiments are removed from the active renderer. The accepted pre-animation visual baseline is restored before any further animation RE. No FPP swing-progress, equip-height, final-matrix, or LocalPlayer swing hook is active.

Shears routing is pinned independently of RTTI. For the current 26.50/26.51 item layout the router reads Item::mId at +0xAA and mMaxStackSize at +0xA8; Shears uses raw item ID 424 and stack size 1. This decision runs before the generic attack-only fallback, so Shears must retain MAINHAND ownership and must never invoke OFFHAND block placement.

# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1** and **1.26.51.1**.

## OFFHAND consumption and release candidate

This change starts from main `2638d88fedcffa0314aa4df51e4ee0273b5c4aa7` and changes only the use lifecycle, its tests and documentation. Renderer, sword/shears classification and block-count reconciliation are retained.

- Completion runs the original native consumption function and its callback/transaction envelope. Its final carried-item write is scoped to native OFFHAND, preserving the native remaining stack, empty slot or returned bottle/bowl.
- The existing session identifies local OFF use. A native active stack matching only OFF can also identify ownership for another Player instance; ambiguous equal stacks retain MAIN unless an explicit OFF session exists.
- If the OFF slot no longer matches active use, cancel use without consuming the replacement or MAIN. Writes outside the active completion/release scope remain native MAIN writes.
- Release now scopes writeback to OFF and passes hand=1 to the original native transaction wrapper only for the verified release callback. Callback/envelope ownership remains native.
- Empty OFF results remain OFF during subsequent callback reads instead of switching to MAIN.

Host tests cover these decisions, while the binary contract verifies the completion/callback chain, virtual setters and native OFF InventoryAction recording. **Device validation is still required** for hunger/status effects, stacked food, potion/milk/stew containers, projectile release, durability, reconnect and server acceptance. Keeping native packet construction does not establish that an unmodified remote server accepts arbitrary OFF consumption; no multiplayer parity is claimed.


## v0.2.68 — targeted 1.26.51.1 fixes on build #550 baseline

This candidate preserves the sword/offhand routing and renderer structure from the successful **build #550** (`2a3a9a7e1a11ab53899e46923aee62e6cc208acc`). The current changes are intentionally isolated to four reported regressions:

- **Crossbow FPP OFFHAND:** Crossbow now joins Bow at the exact generic `FIRSTPERSON_LEFT` offhand-dispatch admission point. Trident/Spear native-3D routing is unchanged.
- **Banner OFFHAND crash + FPP pose:** the old BannerItem `+0x1C0/+0x1C8` synthetic Block-pointer bridge remains disabled. Banner stays on the safe native item transaction, but its safe bridged `FIRSTPERSON_LEFT` transform is now explicitly allowed to use the existing custom pose: scale `1.56`, X `-0.78`, Y `-0.28`, yaw `180°`. No synthetic `Block*` is required.
- **RightUseRouter pre-hook diagnostic:** a pre-hooked `GameMode::useItemOnBlock` entry point is treated as an expected chainable condition after the stable 1.26.51.1 guard passes, so it is logged at INFO instead of WARN. The chaining behavior itself is unchanged.
- **OFFHAND block count reconciliation:** block placement still uses the detached ItemStack transaction snapshot from build #550. If native placement changes the snapshot count while the live OFFHAND slot remains stale, the changed snapshot is written back through native `Actor::setItemInHandSlot(hand=1)`. The verified ItemStack count byte is `+0x22`; no synthetic inventory packet or physical hand swap is used.

The build #550 sword classifier and MAIN/OFF placement order are not refactored by these fixes.

## v0.2.68 — Java-style right-use corrections (candidate)

This candidate fixes verified routing defects on **Bedrock Android 1.26.51.1**. It is **not yet full Java parity** and has not passed the device gameplay matrix. The pre-swap recovery commit `198787f5b0d750fd4c89ba00ac0aab5c72018331` is historical provenance, not evidence that the current implementation works in game.

### Corrected behavior

- Sword's native `WeaponItem::use` override only returns its input. It no longer falsely claims MAIN priority and blocks OFF placement/self-use.
- A MAIN block-use action runs once. A zero/PASS result can fall through to OFF for block-only owners; a nonzero native result is preserved. Bow/food/other MAIN self-use owners defer to the upper native dispatcher before OFF block placement.
- OFF placement **and air-use** use a detached native ItemStack snapshot. The air-use snapshot is created only when OFF is attempted, after any MAIN attempt, so it reflects the current slot.
- An explicit MAIN action scope wins over an older OFF active-use session.
- The renderer and its attachment helper now come from the matching accepted v0.2.67 build overlay. The 220 ms OFF block placement animation runs only after the first-person OFF guard. This repairs the source/header mismatch and undeclared-lambda build regression introduced by the animation commit.
- CI executes the actual production routing detours against fake ABI objects, in addition to source contracts. The renderer generation test checks working files and verifies that generation does not modify them.

### Remaining parity gaps

Full Java behavior requires one ordered interaction pipeline, including block/entity interaction, self-use, PASS/FAIL semantics, and active-use lifecycle. The current native hooks do not yet establish all of that.

- **Consumption/release is not verified:** binary inspection shows native release starts a MAIN-hand transaction and calls the MAIN setter. Redirecting `getSelectedItem` alone does not fix transaction/writeback ownership.
- **Active-use isolation is incomplete:** the existing session-based selected getter can affect unrelated native reads outside an explicit MAIN scope. In-game attack, slot-change, cancellation, world-exit and food/drink completion require validation.
- **Swap restored as an isolated candidate:** UI, frame-pump runtime, and native storage engine are now separate files (`src/ui/SwapButton.*`, `src/swap/SwapRuntime.*`, `src/swap/SwapEngine.*`). The engine no longer validates or calls `Player::getSelectedItem` at `0xF9F7824`, because RightUseRouter has already hooked that entry by the time swap installs. Instead it reproduces the verified 1.26.51.1 selected-stack read directly from Player `+0x570` and its selected-container fields. This removes the build-#596 validation conflict without changing RightUseRouter, renderer, Sword/Shears ownership, or block placement. Occupied swaps still use the user-tested 44a `MAIN=EMPTY -> OFF=old MAIN -> MAIN=old OFF` sequence.
- **Mixed block/self-use fallback remains incomplete:** for example MAIN bow self-use PASS followed by OFF block placement requires coordination at an upper dispatcher boundary.

Do not treat a green build or host test as proof that items cannot be lost, duplicated or locked. The exact evidence and outstanding gameplay matrix are in [the regression document](docs/OFFHAND_REGRESSION_BASELINE.md).

### Verification

```bash
bash tests/run_right_use_runtime_tests.sh
python3 tests/right_use_126511_binary_test.py /path/to/libminecraftpe.so
python3 tests/v0268_render_126511_compat_source_contract.py
python3 tests/v0268_swap_button_source_contract.py
```

The binary test requires the exact 1.26.51.1 file with SHA-256 `b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4`. It checks all eleven router fingerprints, the WeaponItem no-op, and relocated Item/ComponentItem use slots. Host tests execute production C++ detours with fake native objects; they do not emulate Bedrock transactions.

**Maintenance rule:** any change to offhand storage, routing, selected-item access, swap or rendering must update this README and the regression document together.

## v0.2.63 — native-only Bow/Trident renderer

This revision replaces the temporary dual-render/calibration architecture with the native attachment pipeline recovered from `libminecraftpe.so` 1.26.45.1.

- **Bow offhand** stays a native slot-6 attachment. Its local `rightitem` bone identity remains untouched so vanilla animation still works, while the native owner-bone resolver maps that attachment to `leftitem`.
- **Trident offhand FPP** keeps Mojang's native mode-3 `q.item_slot_to_bone_name(c.item_slot)` binding. The mod mirrors only the current animated `pole` local pose inside the exact native offhand first-person draw scope before Minecraft composes the bone matrix.
- The old forced generic hand-equipped route, Bow FPP weak-item mask, TPP pending latch, native-draw suppression, Trident binding mutation, post-compose 180° correction, and Bow/Trident temporary sliders are removed.
- Inventory/paperdoll rendering is no longer used as a proxy for world TPP state.
- Crossbow, Fishing Rod, blocks and the other existing offhand visual paths retain their independent behavior.

The renderer fails closed if the exact 1.26.45.1 native targets or fingerprints do not match.

## v0.2.62 — native offhand capability + automatic-routing separation

Arbitrary items become natively offhand-capable through the verified `Item::Item` default-flag patch (`0x50 -> 0xD0`), while the central automatic insertion planner filters `Offhand` container **34** from generic destination lists.

```text
manual arbitrary item -> offhand      allowed
crafting / pickup auto destination    offhand excluded
ContainerValidation storage hooks     zero
```

The automatic-routing guard remains active until process restart after a Mod Menu disable because Item singletons already constructed with `mAllowOffHand` can retain that flag for the rest of the process.

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

Output:

```text
dist/arm64-v8a/levi-offhand-v0.2.68.levipack
```

## Runtime validation

Use a supported build from `manifest.json` and start from a fresh game launch. For the current action/swap work, validate on **1.26.51.1**.

### Bow/Crossbow visual-pack duplicate fix candidate

Gameplay baseline remains build #711/#714 (`f9b75172b9fc7ecbb30801ec460a7dcb704f9f91`). Only first-person Bow/Crossbow render ownership changes.

The previous renderer forced the generic `FIRSTPERSON_LEFT` path for Bow and Crossbow even when a resource/animation pack also supplied a native attachable. That can produce two submissions: the pack/native model plus Levi's calibrated generic model.

The candidate changes FPP to **native-first, fallback-only**:

- the normal Bow/Crossbow offhand pass suppresses only Levi's forced generic admission;
- the existing native attachment pipeline is allowed to run;
- `prepareAttachment` is the ownership signal: if slot-6 Bow/Crossbow actually reaches native attachment preparation, that model owns the frame;
- right-item owner bindings from Bow/Crossbow attachables are mapped to the left-item owner using the existing slot-6 resolver;
- only when no native attachment was prepared does Levi submit one generic fallback;
- the explicit fallback suppresses native attachment re-entry so it cannot create a second model;
- the existing compiled Bow/Crossbow XYZ/rotation calibration remains on the final offhand matrix.

Swap, manual inventory, right-use, placement and all non-Bow/Crossbow item visuals are unchanged.


### OFFHAND eating candidate — exact inline use-tick bridge

Current main remains `12447d1ad3a6d9ad96ef817636aea58b295cab20`; `src/swap/*` is unchanged.

Device testing rejected build #748. It did not eat and made movement heavier. RE of the uploaded 1.26.51.1 ELF explains both results: the #748 implementation hooked `Inventory::getItem @ 0xF883B58` globally, but when selected MAIN is empty Player tick checks selected-state `+0xB0` at `0xF9E70BC` and jumps directly to `EMPTY_ITEM`. The getter is skipped completely, while its global detour still adds overhead to normal inventory/render/gameplay callers.

The replacement removes that hook entirely and patches only the 24-byte inline selected-stack block at `0xF9E70B8..0xF9E70CC`. The exact-build patch calls a local helper and resumes at `0xF9E71B8`:

- normal MAIN use -> helper returns the original selected MAIN stack;
- explicit local OFF session -> helper returns live OFFHAND while Minecraft's active-use stack matches it;
- integrated-server Player without the local TLS session may use OFF only when native active-use uniquely matches OFF and not MAIN;
- ambiguous MAIN/OFF identical stacks remain MAIN unless the explicit OFF session owns the Player.

All vanilla checks after `0xF9E71B8` remain untouched, including item validity, active-stack equality, selected slot/empty-flag continuity, duration progression, stop/cancel and `0xF9E7450 -> completeUsingItem`.

The patch is installed once and does not run during ordinary movement unless Minecraft has already entered its active-use tick. No global Inventory getter hook, custom eating timer, physical item swap or swap-history dependency remains.


### Banner FPP — build #470 logic ported to 1.26.51.1

Banner now uses the **rendering logic from successful build #470** rather than the later generic safe-item approximation. The old build temporarily exposed BannerItem's cached standing-banner `Block*` through the offhand ItemStack so Minecraft entered the same block-transform path that produced the accepted custom FPP position/orientation.

Only the layout-dependent part was updated for Minecraft 1.26.51.1. Static RE of the new BannerItem constructor shows:

```text
BannerItem ctor       0x1000B5A0
wall Block* store     0x1000B5D8 -> this+0x1D0
standing Block* store 0x1000B5F0 -> this+0x1D8
```

Build #470 used `+0x1C0/+0x1C8`; those stale offsets caused the 1.26.51.1 crash and are forbidden. The custom pose remains the build-#470 calibration: scale `1.56`, X `-0.78`, Y `-0.28`, yaw `180°`. No Minecraft signatures/RVAs from build #470 were copied.


### Renderer-only animation safety rule

Animation work under `src/render` must not hook or override `LocalPlayer::swing`, the upper-use dispatcher, or any other gameplay/action function. The rejected MAINHAND-swing experiment changed the boolean/control-flow semantics of the use path and caused specialized MAINHAND items such as Shears to yield incorrectly to OFFHAND block placement.

For Shears/Fishing Rod/Bow/Trident/food/shield and other items with a native right-click action, MAINHAND ownership remains authoritative. Future placement-animation work must operate only on render transforms/predicates/state that cannot change action dispatch, inventory, stack counts, or hand ownership.

### Block-placement animation phase 1 — freeze MAINHAND FPP swing

The build-#576 `0xB2F66D8` final-matrix experiment is retired. Deeper RE shows that callsite belongs to helper `0xB2F6374`, not to the active `ItemInHandRenderer::renderFirstPerson` MAINHAND path. That explains why the hook could be armed while the visible hand still moved.

The actual first-person swing source is:

```text
Minecraft 1.26.45.1
renderFirstPerson        0xADE96B0
swing-progress BL        0xADEA394 -> 0xEA8DEFC
return/caller            0xADEA398

Minecraft 1.26.51.1
renderFirstPerson        0xB2FB6C0
swing-progress BL        0xB2FC394 -> 0xF286ED8
return/caller            0xB2FC398
```

Immediately after this call Minecraft performs `sqrt`/`sin` math and applies the MAINHAND swing transform to the local FPP matrix. The renderer now actually installs this optional getter hook: it returns neutral progress `0.0f` only when the caller is exactly the verified `renderFirstPerson` return address and `OffhandPlacementAnimation` is active. Every other interpolation call forwards to vanilla. This runs alongside the existing MAIN arm-height freeze, so equip-height and swing-matrix motion are both neutralized without touching gameplay state.

This remains render-only: it does not hook `LocalPlayer::swing`, upper-use, `RightUseRouter`, inventory, hand ownership, Banner, Crossbow, Bow, Trident/Spear, or block-count handling. If the optional render getter hook is unavailable, the accepted visual baseline remains active.


### F-swap install regression — pre-hooked selected setter

The device log from the rejected candidate showed the failure before any swap mutation ran:

```text
[SwapEngine] targets off=1 null=1 copy=1 dtor=1 setOff=1 setSelected=0 emptyMapped=1
Swap engine: native storage target validation failed
Swap Item HUD button registered (runtime unavailable; button kept visible)
```

The supplied 1.26.51.1 binary still matches the `Player::setSelectedItem @ 0xF9F7850` fingerprint exactly on disk. The runtime mismatch occurs because `RightUseRouter` installs first and intentionally hooks that same setter before `SwapEngine::install()`.

Swap now keeps exact fingerprint validation for every other native ABI target. Only after those guards identify the supported build may the engine accept the executable live address `base + 0xF9F7850` for `setSelectedItem`. RightUseRouter's detour is pass-through outside its explicit OFFHAND completion/release writeback scope, which is the state used by the preFrame swap pump.

This commit restores the pre-regression swap mutation sequence unchanged. It does **not** attempt to solve the remaining one-empty synchronization bugs yet; those will be isolated after the F runtime is confirmed active again.


### Build #610 swap synchronization follow-up

Build #610 (`0a9a75ceadcc523832035100ebbcd9cbcfd02849`) remains the behavior baseline: the F button/runtime, hook-aware `Player::setSelectedItem` resolver, MAIN->OFF swap, occupied A/B swap ordering, Sword/Shears routing, renderer and block placement are preserved.

The two remaining defects both occur at a **one-empty cross-container transition**:

- an item moved into OFFHAND by F can become locked against normal inventory removal;
- an item inserted manually into OFFHAND can fail the F swap back to an empty MAINHAND.

Static 1.26.51.1 RE shows LocalPlayer OFFHAND write `0xAAD0360` calls `0xF9FC7C8`, which records container `119 (0x77)` before the low-level OFF slot write. `Player::setSelectedItem @ 0xF9F7850` owns the selected-container transition separately. Build #610 supplied the *opposite live slot's null-like ItemStack object* when clearing one side. That crosses container ownership even though both objects report `isNull()==true`.

Only those two clear values are changed: MAIN->OFF clears MAIN with canonical `ItemStack::EMPTY_ITEM @ 0x134C6780`; OFF->MAIN clears OFF with the same canonical empty. Detached source snapshots and the already-working occupied 44a sequence remain unchanged. No packet, ContainerValidation hook, ItemStackRequest action, right-use hook or renderer change is introduced.



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
