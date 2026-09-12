# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.52 — native 3D Trident binding and Bow midpoint

v0.2.52 corrects the cache boundary used for Trident FPP. The missing model was
not caused by the composed-matrix flag at bone state `+0xDE`: the native owner
resolver was being skipped earlier because binding mode `+0xDC` was already
cached. The mod now exposes that mode as unresolved only at the first native
read (`0x9B37780 -> 0xF147CB0`) for Trident, slot 6, exact FPP scope, and a
`rightitem`/`rightItem` target. Minecraft's existing resolver then binds the
native attachment to `leftitem`/`leftItem`.

A successful resolution is remembered for that bone state, so it is not
repeated every frame. The second mode read, Trident TPP, mainhand, inventory
preview, other actors, and other items retain the native result. Manual Trident
`pole` position/rotation changes have been removed; the model remains
**native 3D only**, and generic 2D item-form submission stays suppressed.

The resolved-state cache is bounded and generation-based. Install, uninstall,
and Mod Menu enable/disable changes advance an atomic generation; each render
thread clears its local entries on the next native prepare. The binding-mode
override remains disabled until every cooperating native hook is installed.
Prepare, mode, and resolver trampolines are published atomically behind a
separate lifetime gate. Teardown stops new readers, lets an already-admitted
prepare transaction finish its nested native calls, and only then releases the
trampolines. The two-instruction mode getter uses its fingerprint-proven direct
`+0xDC` load only while native forwarding is closed.

Bow TPP keeps its proven native owner-bone/local-pose path, with the extra
mirrored-X displacement set to `0.10`: the midpoint between v0.2.50 (`0.00`,
not far enough) and v0.2.51 (`0.20`, too far).

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
dist/arm64-v8a/levi-offhand-v0.2.52.levipack
```

## Runtime validation

Use the exact Minecraft version above and check:

1. Bow in offhand, TPP: exactly one Bow appears between the v0.2.50 and
   v0.2.51 positions.
2. Bow removed from offhand: the inventory player preview remains visible.
3. From a fresh game launch, place Trident in offhand and enter FPP once.
4. Trident in offhand, TPP: its already-correct pose remains unchanged.
5. Mainhand items and other players' equipment continue to render normally.

On first Trident FPP entry, the useful success sequence is
`[TridentFppPrepareProbe]`, `[TridentFppBindingCacheReset]`, then
`[TridentFppBoneBinding]`. Send those lines plus any
`[TridentFppBindingProbe]` lines if the model is still absent.
