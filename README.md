# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.49 — Runtime-corrected native attachment poses

This version keeps arbitrary offhand storage and the existing item/block
rendering support, then corrects the two remaining native attachment cases:

- Bow in TPP keeps the proven native slot-6 `rightitem` to owner `leftitem`
  binding, so there is still exactly one Bow in the offhand. Its `0.20`
  adjustment now follows the renderer's semantic horizontal basis
  (`matrix[4..6]`). v0.2.48 used column 0, which primarily changed depth and
  therefore produced no visible rightward movement.
- Trident in FPP now moves sides by resolving its attachment owner from
  `rightitem` to `leftitem` only inside the exact first-person slot-6 pass.
  Its `pole` root is then rotated 180 degrees while preserving translation.
  This removes the v0.2.48 absolute-X reflection that moved the model outside
  the FPP frustum and made it disappear.

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
dist/arm64-v8a/levi-offhand-v0.2.49.levipack
```

## Runtime validation

Use the exact Minecraft version above and check:

1. Bow in offhand, TPP: exactly one Bow appears in the left hand.
2. Bow removed from offhand: the inventory player preview remains visible.
3. Trident in offhand, FPP: it appears on the left with its head upward.
4. Trident in offhand, TPP: its already-correct pose is unchanged.
5. Mainhand items and other players' equipment continue to render normally.

Useful one-time log markers are `[BowTppBoneBinding]`,
`[BowTppRightOffset]`, `[TridentFppBoneBinding]`, and
`[TridentFppPoleRotation]`.
