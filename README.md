# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1** and **1.26.51.1**.

## v0.2.68 — recovered pre-swap offhand baseline

The runtime action/storage path has been restored to the exact last known-good **pre-swap** commit:

`198787f5b0d750fd4c89ba00ac0aab5c72018331` — `fix: route sword like axe when no native right-click exists`

This recovery is intentional. The experimental F-style swap source may remain in the repository for later work, but it is **not compiled, installed, registered, or allowed to hook Minecraft in the recovery baseline**.

The restored block-placement rules are:

- classify whether MAINHAND truly owns right-click before touching the generic use-on path;
- attack-only Sword/Axe/Pickaxe-style items yield right-click to OFFHAND;
- Bow/Trident/Fishing Rod/Shears/food/shield-style native right-click actions keep MAINHAND priority;
- when MAINHAND has no right-click owner, OFFHAND gets the first native use-on attempt with `hand=1`;
- OFFHAND placement uses a detached native `ItemStack` snapshot, never the live slot pointer;
- if OFFHAND passes, vanilla MAINHAND runs once as fallback;
- left-click remains vanilla MAINHAND.

The exact regression contract is documented in `docs/OFFHAND_REGRESSION_BASELINE.md`.

### 1.26.51.1 runtime hook compatibility

The on-disk `libminecraftpe.so` supplied for 1.26.51.1 was rechecked against every RightUseRouter fingerprint and the bytes match the documented RVAs. If a hookable entry-point prologue is already modified **in memory**, RightUseRouter now keeps an exact fingerprint guard on the non-hook helper functions and then chains the live known-RVA target for `Player::getSelectedItem`, `GameMode::useItemOnBlock`, `GameMode::baseUseItem`, and `GameMode::releaseUsingItem`.

This changes only installation/compatibility. The pre-swap block-placement and right-use decision logic remains unchanged.

### Current action fixes before swap returns

Two interaction regressions are now explicitly protected:

- **OFFHAND food/self-use with Sword/Axe/Pickaxe/empty-like MAINHAND:** the router classifies MAINHAND capability before calling generic `baseUseItem`. If MAINHAND has no real right-click owner, OFFHAND gets the first semantic attempt, so generic ComponentItem success cannot swallow eating/long-use.
- **Sword MAINHAND + placeable OFFHAND through a pre-hooked use-on-block chain:** the normal path remains detached-snapshot + `hand=1`. Only when `GameMode::useItemOnBlock` was already patched before this mod installs do we add a narrow OFFHAND selected-item scope around that chained call, because third-party/earlier wrappers may re-query `Player::getSelectedItem`.

Swap remains quarantined. Eating/drinking animation is not being pursued because those OFFHAND actions are not currently usable. The current visual phase is limited to **first-person OFFHAND block-placement animation**: a 220 ms visual-only matrix impulse is triggered after an accepted native OFFHAND use-on result. It does not mutate inventory, selected-item ownership, hand routing, or transactions.

### Visual workstream scope

Current work in this branch/conversation is intentionally limited to **rendering, animation, offhand appearance, and UI/button presentation**. Java-like offhand action/storage logic is handled separately. Runtime action code should not be changed for visual calibration unless a very small integration fix is explicitly required.


**Renderer build invariant:** `OffhandBlockRenderPatch.cpp`, `NativeAttachmentFix.hpp`, and `OffhandBlockRenderPatch.hpp` are a matched source set. CI must restore all three from current `main` after the archived v0.2.67 overlay. Mixing the current renderer CPP with the archived helper header causes compile-time missing-symbol failures.
 The public header must also retain declarations for every out-of-line visual calibration method implemented by the renderer CPP.

### FPP OFFHAND placement animation

The placement animation is deliberately isolated from action logic:

- trigger source: accepted OFFHAND `GameMode::useItemOnBlock` result;
- duration: **220 ms**;
- renderer scope: block item in `FIRSTPERSON_LEFT` only;
- motion: short inward/down/**forward** impulse plus a small local X/Y/Z rotation;
- the transform returns exactly to Minecraft's normal offhand matrix at the end;
- no animation hook changes storage or placement success/failure;
- while this OFFHAND placement impulse is active, the renderer temporarily freezes the MAINHAND equip-height pair for that draw only, then restores Minecraft's real values immediately.

This phase is visual-only. If placement itself fails, the animation is not considered a functional fix.

**Maintenance rule:** any future feature or bug fix that changes offhand storage, right-use/block placement, selected-item access, swap behavior, or render ownership must update both this README and the regression document in the same change.

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
