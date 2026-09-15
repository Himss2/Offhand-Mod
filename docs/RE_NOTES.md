# Reverse-engineering notes — v0.2.67 reference carriers

Target: Minecraft Bedrock Android **1.26.45.1** (`libminecraftpe.so`, AArch64).

## Bow TPP reference route

Recovered reference path from the earlier Fishing Rod diagnostic:

- `RenderItem`: `0xADDEA08`
- exact world offhand TPP caller: `0xA32F030`
- offhand inventory slot: `34`
- attachable-state function: `0xA32F0F4`
- attachable-state call inside RenderItem: `0xADDEADC`

v0.2.67 scopes a depth guard only around that exact Bow transaction and forces
that one attachable check false. The native Bow slot-6 attachment is suppressed
only while the exact transaction is active. No pending latch is used.

## Spear/Trident FPP carrier substitution

The owner-bone vector is recovered through `F14355C`; owner matrix copy is
`F147CC0`. In the exact native FPP scope, the player `rightitem` owner matrix is
located in the vector, copied into a temporary matrix, and transformed with a
bilateral X reflection `S * R * S`. That transformed owner frame is supplied to
`F147CC0`.

Crucially, Minecraft then applies the original local Spear/Trident animation on
top of the carrier. v0.2.67 does **not** mirror the fully composed root after
`F147ED0`, so `spear` / `pole` local animation remains authored and 3D.

## Locked behavior

Bow FPP remains on the accepted generic `FIRSTPERSON_LEFT` route and calibration.
Storage/automatic insertion remains the v0.2.62 architecture; no
`ContainerValidation` hooks are added.

## v0.2.68 Java-like action routing — locked gameplay boundaries

Exact binary identity remains SHA-256
`444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`,
GNU Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`.

The following semantic GameMode entries are now fingerprinted independently of
renderer/storage code:

- `GameMode::interact(Actor&, Vec3 const&)`: RVA `0xEF71B7C`, FDE
  `0xEF71B7C..0xEF72170`. Its `interact::$_0` function object uses vptr
  `0x1226FFF8`, constructed at `0xEF71CC4`.
- `GameMode::_attack(Actor&, bool, Vec3 const&)`: RVA `0xEF721E4`, FDE
  `0xEF721E4..0xEF72684`. The `_attack` transaction path constructs the
  verified lambda function object around `0xEF72368`.
- `GameMode::startDestroyBlock(BlockPos const&, unsigned char, bool&)`: RVA
  `0xEF72684`, FDE `0xEF72684..0xEF729FC`. ABI evidence includes the face in
  `w2`, output boolean in `x3` (cleared at `0xEF72704`), and the verified helper
  call at `0xEF72734 -> 0xEF729FC`.
- `GameMode::baseUseItem(ItemStack const&)`: RVA `0xEF75578`, FDE
  `0xEF75578..0xEF7590C`; lambda vptr references at `0xEF75684 -> 0x12270308`
  and `0xEF756B0 -> 0x12270388`.
- `GameMode::baseUseItemAsAttack(ItemStack const&, Vec3 const&)`: RVA
  `0xEF75B9C`, FDE `0xEF75B9C..0xEF75F40`; lambda vptr references at
  `0xEF75CB4 -> 0x12270408` and `0xEF75CE0 -> 0x12270488`.
- `GameMode::releaseUsingItem()`: RVA `0xEF76108`, FDE
  `0xEF76108..0xEF764EC`; lambda vptr references at
  `0xEF76298 -> 0x12270508` and `0xEF762A4 -> 0x12270588`.

The block-destroy lifecycle beyond `startDestroyBlock` is deliberately **not**
enabled yet. Functions around `0xEF72AF4` / `0xEF72F9C` have strong destroy
path evidence, but continue/finalize/cancel semantics are not yet uniquely
proven. Mining routing must remain fail-closed until those edges and the native
tool-suitability boundary are fingerprinted.
