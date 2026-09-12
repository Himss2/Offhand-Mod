# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.52 — Bow inward correction and Trident native-matrix fix

This revision is intentionally limited to the two remaining attachment bugs on
Minecraft Bedrock **1.26.45.1**.

For Bow TPP, v0.2.51 mirrored the attachment local X and then added another
`0.20`, which moved the Bow farther to the visual left. v0.2.52 removes that
production local-pose override. Minecraft first composes the native slot-6 Bow
attachment, then the mod moves the returned matrix `0.20F` along the normalized
semantic-horizontal column 1. This is the previously device-calibrated
**visual-right / toward-body** direction and keeps the existing owner-bone
`rightitem` -> `leftitem` remap.

For Trident FPP, static tracing of `F147ED0` shows that when bone-state flag
`+0xDE` is set, Minecraft first copies the cached composed matrix at `+0x30`
into the output matrix. v0.2.51 cleared that flag to force the temporary local
`pole` pose to be recomposed, which bypassed that native cached matrix seed; it
also mirrored the pole's local X. Both operations can detach the 3D model from
its native hand placement. v0.2.52 no longer clears `+0xDE`, rewrites `+0x70`,
or touches `+0x30` for the Trident. It lets Minecraft produce the native 3D
attachment matrix first, then applies only a local-Z 180-degree orientation
correction to the returned `pole` matrix while preserving
`matrix[12..14]` exactly. The generic item-form remains suppressed, so this is
still **native 3D only** with no 2D fallback.

The exact FPP/TPP scopes, slot-6 guards, owner-bone resolver probe, and binary
fingerprints remain restricted to the target binary. Bow FPP, Trident TPP,
mainhand rendering, inventory previews, and unrelated item families are not
changed.

Target Build ID:

`868e275cb295e9a275bb29d2258edc2f7dc48761`

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

The arm64 package is written to:

```text
dist/arm64-v8a/levi-offhand-v0.2.52.levipack
```

## Runtime validation

Use the exact Minecraft version above and check only these two regression targets:

1. Bow in offhand, TPP: exactly one native Bow appears and is shifted right, toward the player body, relative to v0.2.51.
2. Trident in offhand, FPP: the native 3D Trident is visible; its attachment translation remains native while the pole orientation is turned 180 degrees.
3. Trident in offhand, TPP and Bow in FPP remain unchanged.
4. Mainhand and unrelated item rendering remain unchanged.

For Trident diagnostics, capture `[TridentFppPrepareProbe]`,
`[TridentFppBindingProbe]`, `[TridentFppBoneBinding]`,
`[TridentFppPoleRotation]`, and `[TridentFppPoleMatrix]`.
