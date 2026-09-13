# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.55 — Bow/Fishing Rod + Trident/Shield routing diagnostic

This build intentionally tests the renderer layer rather than adding another
offset or rotation. The v0.2.53 Bow/Trident owner-binding and local-pose
corrections are gated off for the two diagnostic targets so they cannot hide the
route difference we are trying to measure.

### Bow TPP → Fishing Rod reference

At the exact HumanoidAdditional offhand `RenderItem` call (`0xA32F030`, slot
`34`), Fishing Rod naturally continues through the generic LEFT item renderer.
Bow normally stops when `ADDEA08` observes its enabled attachable. v0.2.55
forces that attachable check false only for this exact Bow TPP transaction and
suppresses the separate native Bow slot-6 TPP attachment draw. The result is one
Bow candidate on the same generic LEFT renderer class used by Fishing Rod.

Useful logs:

- `[BowFishingRodTppRoute]` — compare Bow and Fishing Rod at slot 34.
- `[BowFishingRodTppNativeSuppress]` — confirms the native Bow slot-6 TPP
  attachment was removed, preventing a duplicate.

### Trident FPP → Shield reference

Shield is kept as the known-good native offhand reference. v0.2.55 suppresses
only the native Trident slot-6 attachment inside the exact first-person
DataDriven scope, then allows the generic `renderOffhandItem` / `renderObject`
submission that previous builds intentionally discarded. No `pole` local-X
mirror, Z+180 correction, owner-bone remap, or composed-matrix rewrite is
applied during this diagnostic.

Useful logs:

- `[TridentShieldFppNativeSuppress]` — native FPP Trident attachment suppressed.
- `[TridentShieldFppRoute]` — Trident entered / submitted through the generic
  offhand layer.
- `[ShieldFppReference]` — shows which of those same checkpoints Shield reaches.

This is a **diagnostic build**, not a claim that the final Bow/Trident transforms
are solved. Its purpose is to prove whether the stable reference items and the
broken target items differ at the renderer-selection layer.

Target Build ID:

`868e275cb295e9a275bb29d2258edc2f7dc48761`

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

The arm64 package is written to:

```text
dist/arm64-v8a/levi-offhand-v0.2.55.levipack
```

## Runtime validation

Use Minecraft **1.26.45.1** and test in this order:

1. Put **Fishing Rod** in offhand, switch to TPP, then capture the single
   `[BowFishingRodTppRoute] family=FishingRod ...` line.
2. Replace it with **Bow** while staying in TPP. Check whether exactly one Bow
   now sits on the same left-hand side / anchor class as Fishing Rod. Capture
   `[BowFishingRodTppRoute] family=Bow ...` and
   `[BowFishingRodTppNativeSuppress]`.
3. Switch to FPP and place **Shield** in offhand. Capture every
   `[ShieldFppReference]` line.
4. Replace Shield with **Trident**. Record whether a Trident model appears at
   all and whether it is 2D or 3D, then capture every `[TridentShieldFppRoute]`
   and `[TridentShieldFppNativeSuppress]` line.
5. Confirm Bow FPP, Trident TPP, mainhand items, inventory preview, and unrelated
   offhand items remain unchanged.

The most important comparison is not the numeric position yet. It is whether
Bow reaches the Fishing Rod route and whether Trident reaches the Shield route.
