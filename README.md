# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.57 — Bow TPP route latch + Trident native left binding

Device validation of v0.2.56 proved two separate boundaries. Bow reaches the
exact slot-34 Fishing-Rod-style route, but that RenderItem scope ends before
`renderOffhandItem` and the final held-item matrix. v0.2.57 carries a one-shot
TPP family latch into that later render scope, so the existing +25.2 degree
semantic-Y Bow correction can finally execute while preserving matrix
translation exactly. Fishing Rod keeps the small -0.06 semantic-Y adjustment.

Trident v0.2.56 proved the native slot-6 DataDriven attachment is the required
3D model. It appeared on the mainhand/right side because the cached native
`rightitem` binding prevented the owner-bone resolver from running. v0.2.57
reopens only the first proven binding-mode read for an unresolved FPP offhand
Trident, then remaps `rightitem` to `leftitem`. The native 3D attachment remains
active and the generic 2D item form remains suppressed. No pole rotation,
local-pose mirror, or composed-matrix rewrite is applied in this revision.

Expected diagnostic markers:

- `[TppReferenceLatch]` followed by `[BowTppGripPivot]` for Bow TPP.
- `[TridentFppBindingCacheReset]`, `[TridentFppBindingProbe]`, and
  `[TridentFppBoneBinding]` for Trident FPP.
- `[TridentFppNative3D]` confirms native 3D is retained.

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

The arm64 package is written to:

```text
dist/arm64-v8a/levi-offhand-v0.2.57.levipack
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
