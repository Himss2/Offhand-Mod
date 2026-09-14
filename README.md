# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.62 — native offhand capability + automatic-routing separation

This storage milestone removes the legacy ContainerValidation/getAllowOffHand hook
architecture. Arbitrary items become natively offhand-capable through the verified
`Item::Item` default-flag patch (`0x50 -> 0xD0`), while the central automatic
insertion planner filters `Offhand` container **34** from generic destination lists.

- legacy ContainerValidation/getAllowOffHand storage hooks removed
- Item native mAllowOffHand capability is enabled at construction
- automatic insertion filters Offhand container 34 at the central planner
- manual transactions remain vanilla/native
- Bow/Trident rendering is intentionally unchanged in this storage milestone

Expected policy:

```text
manual arbitrary item -> offhand      allowed
crafting / pickup auto destination    offhand excluded
ContainerValidation storage hooks     zero
```

The automatic-routing guard is intentionally retained until process restart after a
Mod Menu disable, because Item singletons already constructed with `mAllowOffHand`
can keep that flag for the rest of the process.


## v0.2.59 — crash-safe Trident native binding + one-session visual calibration

v0.2.58 crashed with `SIGILL` at `0xEEAB3B4`. The tombstone proves the fault is
immediately after the tiny helper at `0xEEAB3AC`. That helper is only two AArch64
instructions (`ADD x0,x0,#8; RET`), so installing a normal inline hook there
necessarily overwrites into the next native routine. v0.2.59 removes that hook
completely.

Fresh static RE of the supplied Minecraft 1.26.45.1 binary also shows the hook was
unnecessary. Mojang's native `query.item_slot_to_bone_name` implementation already
maps the slot-name hash `off_hand` (`0x5D4C22812BA3AF8C`) to the owner-bone hash
`leftitem` (`0x1CF3FDCBB0AB92F7`). Therefore Trident keeps the vanilla mode-3 Molang
binding and native 3D attachment path. The generic 2D fallback remains suppressed.

The visible Trident still needs the first-person animation shifted across the
screen, so v0.2.59 exposes a temporary `Trident FPP Horizontal (TEMP)` slider
(-1.5..1.5, default +0.875). Only the composed Trident pole X translation is
changed; native Y/Z are preserved. The existing post-compose Z+180 head/tail
correction remains active.

Bow TPP keeps the v0.2.58 `Bow TPP Tilt (TEMP)` slider. Its grip translation is
preserved while semantic Rot-Z is adjusted, so the final Bow angle can also be
locked during the same game session.

Decisive markers:

- `[BowTppTiltSlider] semanticRotZ=...`
- `[BowTppGripPivot] semanticRotZDelta=... translationPreserved=1`
- `[TridentFppNativeBinding] off_hand->leftitem verified statically`
- `[TridentFppHorizontalSlider] delta=...`
- `[TridentFppHorizontal] nativeX=... delta=... finalX=...`
- `[TridentFppPoleRotation] postComposeZ180 ...`
- `[TridentFppNative3D]` confirms the native 3D route remains active

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

Output:

```text
dist/arm64-v8a/levi-offhand-v0.2.62.levipack
```

## Runtime validation

Use Minecraft **1.26.45.1**.

1. Start from a fresh game launch and verify there is no crash when Trident is
   placed in offhand.
2. Bow TPP: adjust `Bow TPP Tilt (TEMP)` until the upper limb is straight while
   the grip stays in the accepted v0.2.55 position. Report the final number.
3. Trident FPP: adjust only `Trident FPP Horizontal (TEMP)` until the native 3D
   model sits on the left/offhand side. Report the final number. If its head is
   still inverted, capture `[TridentFppPoleRotation]` as well.
4. Regression: Bow FPP, Trident TPP, mainhand tools, inventory preview and
   unrelated offhand items must remain unchanged.
