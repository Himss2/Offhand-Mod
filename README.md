# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1** and **1.26.51.1**.

## v0.2.68 — offhand stability baseline + F-style swap

The current 1.26.51.1 baseline intentionally treats the already-working offhand interaction path as the primary subsystem and the F-style swap as an extension.

- **Block placement / right-use baseline:** restored from commit `f687b626f2c941eb4ff3a6b2ce534a3cf0a3132d`. Minecraft receives exactly one MAINHAND use-on attempt; only a PASS can fall back to OFFHAND.
- **Transaction-safe OFFHAND placement:** never pass the live offhand slot as the transaction snapshot. Copy the offhand `ItemStack` with the native copy constructor, call the native use-on path with `hand=1`, then destroy the detached snapshot.
- **MAINHAND hold-use priority:** Bow/Trident/food/shield-style hold-use remains MAINHAND-owned and must not be displaced by OFFHAND placement fallback.
- **Swap:** the successful `ClientInstance::preFrameTick` pump remains. For occupied↔occupied exchange, both storages are cleared through the native empty stack before snapshots are refilled, so the new OFFHAND stack does not inherit stale slot state.
- **Ordering:** `RightUseRouter` is installed before `OffhandSwapRuntime`. Swap changes must not alter the proven block-placement/right-use code path.
- **No ContainerValidation hooks:** the current storage architecture keeps manual transactions native and does not restore the older `ContainerScreenValidation` hook family.

The invariants and regression checklist are documented in `docs/OFFHAND_REGRESSION_BASELINE.md`. Any future bug fix or feature that changes offhand storage, right-click routing, swap behavior, or render ownership must update both this README and the regression document in the same change.

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
