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

The following semantic GameMode entries are fingerprinted independently of
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

### Exact native hand/use accessors

The Android shipping binary does not expose the required C++ accessors through
`dlsym`, so v0.2.68 resolves module-base + exact RVA and fingerprints the target
instructions before enabling Java-like actions:

- `Player::getSelectedItem`: `0xF0B900C`
- `Actor::getOffhandSlot`: `0xEC9D62C`
- `ItemStackBase::isNull`: `0xF63E760`
- `Player::isUsingItem`: `0xF0B8094`
- active item-use stack accessor (`Player + 0x6D8`): `0xF0B80B4`
- native stack-difference comparator used for active use: `0xF6443F4`

The long-use offhand session is therefore validated against Minecraft's actual
active-use stack rather than an item ID or a stale `ItemStack*` identity.

### Real combat capability — Item virtual slot 38

`ItemStackBase` stores its `WeakPtr<Item>` at `+0x8`. The exact target's
`ItemStackBase::isNull` implementation independently confirms that pointer path.
Following the weak counter yields the live `Item*`; its vtable slot 38
(offset `0x130`) is `Item::getAttackDamage()`.

The relocation table proves the same slot across representative native classes:

- base `Item`: vtable relocation `0x122E2048 -> 0xF667504`; implementation
  returns `0`.
- `WeaponItem`: `0x122C62E8 -> 0xF4C7B00`; implementation loads weapon damage.
- `DiggerItem`: `0x122E4070 -> 0xF6B3880`; implementation loads tool damage.
- `TridentItem`: `0x122C5140 -> 0xF46CAEC`; implementation returns `8`.

v0.2.68 uses `getAttackDamage() > 0` as the native **real combat capability**
gate. This deliberately differs from generic punching: ordinary items such as
food or bows may still participate in Minecraft's universal punch fallback, but
they do not win hand arbitration merely because a punch is always possible.
The routing order is mainhand real combat -> offhand real combat -> unchanged
mainhand vanilla generic punch. The selected-item redirection exists only inside
the scoped offhand attack transaction.

### Mining lifecycle status

Additional RE now gives strong ABI evidence for:

- likely `GameMode::destroyBlock(BlockPos const&, unsigned char)`: `0xEF72C18`
- likely `GameMode::continueDestroyBlock(BlockPos const&, unsigned char,
  Vec3 const&, bool&)`: `0xEF72F9C`; `x3` carries player position and `x4` the
  output boolean, which is cleared near entry.
- likely `GameMode::stopDestroyBlock(BlockPos const&)`: `0xEF7398C`
- likely `GameMode::_creativeDestroyBlock`: `0xEF72AF4`
- likely `GameMode::getDestroyRate(Block const&)`: `0xEF73694`

Tool suitability also has a promising native boundary: Item vtable slot 89
(offset `0x2C8`) is `getDestroySpeed(ItemStackBase const&, Block const&)`.
Relocations include base Item `0x122E21E0 -> 0xF665908` (returns 1.0),
WeaponItem `0x122C6480 -> 0xF4C7970`, and DiggerItem
`0x122E4208 -> 0xF6B3FB0`.

Mining routing remains deliberately **fail-closed** for now. The continue,
finalize and cancel functions plus the target-Block resolver still need exact
binary-contract fingerprints before production hooks are installed. No nearby
RVA is enabled from inference alone.
