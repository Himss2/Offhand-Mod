# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.61 — attachment-context probe

This is a diagnostic branch based on the stable v0.2.59 storage baseline. It does **not** use the v0.2.60 global `mAllowOffHand` constructor policy, so the known crafting-result-to-offhand regression is not part of this test.

The purpose is to compare Minecraft's native attachment context for Shield, Bow, and Trident before applying another visual fix. For those target attachment passes, previous Bow/Trident owner-binding, local-pose, final-matrix calibration, pole rotation, and temporary slider adjustments are bypassed. The legacy DataDriven depth is still observed only as a comparison signal; native `prepareAttachment(..., isFirstPerson, ...)` is the authoritative perspective field being logged.

Expected diagnostic markers:

- `[AttachmentContextPrepare]` — family, slot 5/6, native `isFirstPerson`, old DataDriven scope, actor/stack identity.
- `[AttachmentContextBinding]` — native binding mode/name hash and name-resolver result where that path is used.
- `[AttachmentContextDraw]` — draw callsite plus the prepare context correlated to the same actor/stack/slot.
- `[AttachmentContextBone]` — unmodified Bow `rightitem` or Trident `pole` matrix after Minecraft composes it.
- Existing `[ShieldFppReference]` logs remain useful if Shield takes the generic offhand renderer instead of the attachment path.

### Device test

From a fresh launch, capture logs while viewing each case separately: Shield offhand FPP, Shield offhand TPP, Bow offhand FPP, Bow offhand TPP, Trident offhand FPP, Trident offhand TPP, then open inventory/paperdoll with Bow and Trident in offhand. Do not expect v0.2.61 to visually fix Bow/Trident; this build is intended to reveal the first native context where their path differs from Shield.

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

Output:

```text
dist/arm64-v8a/levi-offhand-v0.2.61.levipack
```
