# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.50 — Pre-composition local attachment poses

This version keeps arbitrary offhand storage and the existing item/block
rendering support, then moves the two remaining native attachment corrections
to the local animated pose that Minecraft actually consumes:

- Bow in TPP keeps the proven native slot-6 `rightitem` to owner `leftitem`
  binding, so there is still exactly one Bow in the offhand. Only the root's
  local animated `position.x` is mirrored before native matrix composition.
- Trident in FPP now moves sides by resolving its attachment owner from
  `rightitem` to `leftitem` only inside the exact first-person slot-6 pass. The
  `pole` root's local `position.x` is mirrored and its local Z rotation is
  advanced 180 degrees before composition.

v0.2.49 edited the already-composed output matrix. Device logs proved those
hooks ran, but the visual pose did not change. v0.2.50 instead temporarily
edits bone state `+0x70..+0x87`, calls the native composer, and restores the
original 24 bytes immediately. This prevents animation changes from leaking or
accumulating across camera, item, actor, and inventory-preview renders.

The Bow FPP mask and the correct Trident TPP path remain in place. The failed
TPP experiments that enabled a second generic Bow or suppressed the whole
player preview have been removed.

All new native RVAs are guarded by exact AArch64 entry fingerprints for this
Minecraft binary. Installation fails closed when the binary does not match.

Target Build ID:

`868e275cb295e9a275bb29d2258edc2f7dc48761`

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

The arm64 package is written to:

```text
dist/arm64-v8a/levi-offhand-v0.2.50.levipack
```

## Runtime validation

Use the exact Minecraft version above and check:

1. Bow in offhand, TPP: exactly one Bow appears in the left hand.
2. Bow removed from offhand: the inventory player preview remains visible.
3. Trident in offhand, FPP: it appears on the left with its head upward.
4. Trident in offhand, TPP: its already-correct pose is unchanged.
5. Mainhand items and other players' equipment continue to render normally.

Useful one-time log markers are `[BowTppBoneBinding]`,
`[BowTppLocalPose]`, `[TridentFppBoneBinding]`, and
`[TridentFppLocalPose]`.
