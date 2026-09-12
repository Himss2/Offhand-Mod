# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.51 — Bow spacing and native 3D Trident probe

Device validation of v0.2.50 proved that the Bow TPP owner-bone remap and local
pose hook both execute. v0.2.51 keeps that path and continues the mirrored
local-X displacement by another `0.20`, moving the single native Bow farther
in the same direction regardless of the native X sign.

The same validation showed that the Trident `pole` local-pose hook executes in
FPP, but the expected left-owner binding marker does not appear and the model
remains invisible. This build intentionally stays **native 3D only**: the
generic item-form submission remains suppressed, so no 2D fallback or duplicate
Trident is introduced.

Further static tracing found the missing cache boundary: `F147ED0` returns the
matrix cached at bone state `+0x30` immediately when flag `+0xDE` is set, before
reading the temporary local pose at `+0x70`. v0.2.51 snapshots the original
cached matrix and flag, clears `+0xDE` for the target composition, then restores
the pose, cache, and flag. The corrected output remains available to child
bones for the current draw without leaking into another actor or perspective.

To verify that correction and identify any remaining owner-binding issue,
v0.2.51 records a bounded diagnostic snapshot of:

- the exact FPP attachment preparation scope;
- each owner-bone resolver call, source hash, left-remap attempt and result;
- the `pole` local position, rotation and scale before composition;
- the composed `pole` matrix translation and relevant native state flags.

The probe is limited to four binding messages and one pose/matrix snapshot per
enable cycle. Toggle the Offhand module off/on to reset it. Bow FPP, Trident
TPP, mainhand rendering, inventory previews and unrelated items retain their
existing paths.

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
dist/arm64-v8a/levi-offhand-v0.2.51.levipack
```

## Runtime validation

Use the exact Minecraft version above and check:

1. Bow in offhand, TPP: exactly one Bow appears farther from the body.
2. Bow removed from offhand: the inventory player preview remains visible.
3. From a fresh game launch, place Trident in offhand and enter FPP once.
4. Trident in offhand, TPP: its already-correct pose remains unchanged.
5. Mainhand items and other players' equipment continue to render normally.

Send all lines containing `[TridentFppPrepareProbe]`,
`[TridentFppBindingProbe]`, `[TridentFppPolePose]`,
`[TridentFppPoleMatrix]`, and `[TridentFppBoneBinding]`. The absence of one of
these markers is also evidence and should be reported.
