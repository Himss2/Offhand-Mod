# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.56 — Bow TPP tilt + native Trident 3D

This revision follows the v0.2.55 device diagnostic instead of adding another
renderer-route experiment.

### Bow TPP

Bow keeps the v0.2.55 generic LEFT route that placed the lower/grip area on the
correct offhand side. The TPP-only final matrix receives one additional
`+25.20°` semantic-Y tilt correction while its matrix translation is preserved.
Bow FPP continues to use the existing accepted path.

Useful logs:

- `[BowFishingRodTppRoute]` — confirms the Bow/Fishing Rod slot-34 generic LEFT route.
- `[BowFishingRodTppNativeSuppress]` — confirms the separate native Bow TPP draw is suppressed.
- `[BowTppGripPivot]` — confirms the v0.2.56 TPP-only tilt correction ran.

### Fishing Rod TPP

Fishing Rod remains the Bow route reference and is lowered by `0.06` along the
proven semantic Y basis in TPP only.

Useful log:

- `[FishingRodTppLower]` — confirms the small TPP vertical correction ran.

### Trident FPP

The v0.2.55 Shield-style generic route proved that `renderOffhandItem` /
`renderObject` only produces the 2D Trident item form. v0.2.56 therefore returns
Trident to Minecraft's native slot-6 DataDriven attachment path and suppresses
the generic 2D submission.

The only Trident attachment intervention retained is owner-bone remapping from
`rightitem` to `leftitem` when Minecraft's native resolver naturally runs inside
the exact FPP slot-6 scope. This revision does **not** force a binding-cache
re-resolve and does **not** mirror/rotate the `pole`, rewrite its local pose, or
overwrite the composed attachment matrix.

Useful logs:

- `[TridentFppPrepareProbe]` — exact native FPP slot-6 preparation scope reached.
- `[TridentFppBindingProbe]` / `[TridentFppBoneBinding]` — owner-bone resolver activity.
- `[TridentFppNative3D] native slot6 attachment retained` — native 3D draw retained.
- `[TridentFppNative3D] suppress generic 2D item form` — 2D fallback suppressed.

Target Build ID:

`868e275cb295e9a275bb29d2258edc2f7dc48761`

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

The arm64 package is written to:

```text
dist/arm64-v8a/levi-offhand-v0.2.56.levipack
```

## Runtime validation

Use Minecraft **1.26.45.1**. The two decisive checks are:

1. Bow in offhand TPP: lower/grip position should remain where v0.2.55 placed it,
   while the upper part should be straighter instead of leaning outward. Fishing
   Rod should sit slightly lower than v0.2.55.
2. Trident in offhand FPP after a fresh game launch: report whether the native
   model is now **3D**, still missing, or appears in the wrong hand/orientation.

Also verify Bow FPP, Trident TPP, mainhand items, inventory preview, and unrelated
offhand items remain unchanged.
