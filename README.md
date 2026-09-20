# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1** and **1.26.51.1**.

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
- **Swap remains disabled:** the prior clear/refill setter experiment does not provide a verified atomic native transaction. It is not compiled or installed.
- **Mixed block/self-use fallback remains incomplete:** for example MAIN bow self-use PASS followed by OFF block placement requires coordination at an upper dispatcher boundary.

Do not treat a green build or host test as proof that items cannot be lost, duplicated or locked. The exact evidence and outstanding gameplay matrix are in [the regression document](docs/OFFHAND_REGRESSION_BASELINE.md).

### Verification

```bash
bash tests/run_right_use_runtime_tests.sh
python3 tests/right_use_126511_binary_test.py /path/to/libminecraftpe.so
python3 tests/v0268_render_126511_compat_source_contract.py
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

1. Bow offhand FPP: exactly one Bow is visible on the left/offhand side.
2. Bow offhand TPP: exactly one Bow is attached to the left hand; inventory/player preview must remain unaffected.
3. Trident offhand FPP: one native 3D Trident is visible on the left and follows normal/raise/use animation without a generic 2D duplicate.
4. Trident TPP and mainhand Bow/Trident remain vanilla.
5. Crafting and pickup must not auto-route ordinary items into slot 34.
6. Manual arbitrary-item placement into offhand remains available.
7. MAINHAND item with no right-click action + placeable block in OFFHAND: right-click places from OFFHAND; left-click remains MAINHAND.
8. After OFFHAND placement, the remaining item can still be removed/moved normally from slot 34.
9. F swap with one empty hand works repeatedly without ghost/duplicate stacks.
10. F swap with both MAINHAND and OFFHAND occupied alternates correctly, and the new OFFHAND item can immediately be removed through normal inventory UI.


Renderer review also corrected six 1.26.51.1 callsite translations for spear admission and native owner-vector/matrix lookup. Their BL targets are verified by the binary test.

### Banner FPP — build #470 logic ported to 1.26.51.1

Banner now uses the **rendering logic from successful build #470** rather than the later generic safe-item approximation. The old build temporarily exposed BannerItem's cached standing-banner `Block*` through the offhand ItemStack so Minecraft entered the same block-transform path that produced the accepted custom FPP position/orientation.

Only the layout-dependent part was updated for Minecraft 1.26.51.1. Static RE of the new BannerItem constructor shows:

```text
BannerItem ctor       0x1000B5A0
wall Block* store     0x1000B5D8 -> this+0x1D0
standing Block* store 0x1000B5F0 -> this+0x1D8
```

Build #470 used `+0x1C0/+0x1C8`; those stale offsets caused the 1.26.51.1 crash and are forbidden. The custom pose remains the build-#470 calibration: scale `1.56`, X `-0.78`, Y `-0.28`, yaw `180°`. No Minecraft signatures/RVAs from build #470 were copied.


### Block-placement animation phase 1 — freeze MAINHAND

Before changing the OFFHAND placement motion, the renderer now installs an **optional visual-only MAINHAND freeze layer**. Static RE of Minecraft 1.26.51.1 identifies the shared FPP per-hand renderer at `0xB2F7F18`; callsite `0xB2F6B48` passes hand `0` for MAINHAND and `0xB2FC2E8` passes hand `1` for OFFHAND.

While the existing `OffhandPlacementAnimation` window is active, only the MAINHAND draw temporarily pins ItemInHandRenderer's equip-height pair (`+0x180/+0x184`) to the value seen at the start of the placement. The real fields are restored immediately after the draw. No runtime/action/storage state is changed.

The hook is intentionally optional: failure to resolve/install it must leave every existing Banner/Bow/Crossbow/Trident/block visual active. OFFHAND placement motion values are **not tuned in this phase**; first verify that MAINHAND no longer performs the unwanted placement/equip motion.

