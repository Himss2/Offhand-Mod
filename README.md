# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.58 — Bow screen-plane calibration + native Trident expression binding

v0.2.57 proved the Bow TPP route latch and final held-item matrix both execute:
`[TppReferenceLatch]` and `[BowTppGripPivot]` were present on device, but the
silhouette did not change. The previous correction used semantic Rot Y, which
maps to native Rz and mainly turns the 2D item around its own depth axis. v0.2.58
moves the TPP-only correction to **semantic Rot Z -> native Rx**, the screen-plane
axis that changes the visible upper/lower Bow lean. Translation is restored
exactly after the rotation, so the accepted grip position is preserved. A
temporary `Bow TPP Tilt (TEMP)` slider (-45..45 degrees, default -25.2) is exposed
so the final angle/sign can be locked in one device session and removed next.

Static RE of the supplied 1.26.45.1 `libminecraftpe.so` also identified why the
Trident right-hand repair never reached the old name resolver. Mojang's Trident
`pole` is **mode-3 expression bound** by `q.item_slot_to_bone_name(c.item_slot)`.
At `0x9B37BD4`, Minecraft calls `0xEEAB3AC` to expose the evaluated HashedString
before scanning owner bones. v0.2.58 hooks that exact callsite only while the
slot-6 Trident FPP prepare scope is active and returns a thread-local native-hash
proxy for the owner bone `leftItem`. The Molang value itself is never modified.
The native 3D attachment stays enabled and the generic 2D fallback stays
suppressed.

The already-observed upside-down Trident pole is corrected independently after
native matrix composition with a local Z 180-degree turn. The selected owner
bone translation is restored byte-for-byte after the turn.

Decisive markers:

- `[BowTppGripPivot] semanticRotZDelta=... translationPreserved=1`
- `[BowTppTiltSlider] semanticRotZ=...` when the temporary slider changes
- `[TridentFppExpressionBinding] ... forcedLeftItem=...`
- `[TridentFppPoleRotation] postComposeZ180 translationPreserved=1`
- `[TridentFppNative3D]` confirms the native 3D route remains active

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

Output:

```text
dist/arm64-v8a/levi-offhand-v0.2.58.levipack
```

## Runtime validation

Use Minecraft **1.26.45.1**.

1. Bow TPP: open Mod Menu and adjust only `Bow TPP Tilt (TEMP)` until the Bow is
   visually straight. Do not compensate with position; the grip/translation is
   intentionally frozen. Report the final slider value.
2. Trident FPP: from a fresh launch, put Trident in offhand. It should remain 3D,
   move to the left/offhand owner bone, and have the pole head corrected upward.
   Capture the three Trident markers above if any part is still wrong.
3. Regression: Bow FPP, Trident TPP, mainhand tools, inventory preview and
   unrelated offhand items must remain unchanged.
