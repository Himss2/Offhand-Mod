# Levi Offhand

Native Levi Launcher Android mod for Minecraft Bedrock **1.26.45.1**.

## v0.2.61.1 — native Item offhand policy architecture

This diagnostic release removes the five storage-policy detours used by the
previous architecture (`manual-set`, `auto-add`, `tryTransfer`, `trySwap`, and
`ItemStackBase::getAllowOffHand`). Instead, it follows Minecraft's own Item
state: `Item::Item` initializes a 16-bit flag field at `Item+0x112`, and bit
`0x80` is the native `mAllowOffHand` bit read by `getAllowOffHand`.

For the supplied 1.26.45.1 binary, the constructor initializes that word from
`0x50`. v0.2.61.1 changes only the constructor immediate to `0xD0`, preserving
all existing default bits and adding `0x80`. The patch is one four-byte native
instruction change; no ContainerValidation/getAllowOffHand trampoline is used.

Visual Bow/Trident code is intentionally left at the v0.2.61.1 state so this
build isolates the effect of making every Item genuinely offhand-capable.

### Runtime validation

Test these before changing visual code again:

1. Move Stone (or another normally unsupported item) inventory -> offhand and back.
2. Move an unsupported item chest -> offhand and offhand -> chest.
3. Swap mainhand/inventory items with offhand.
4. Craft with an empty offhand and verify crafted output does **not** auto-route into offhand unexpectedly.
5. Pick up dropped items with empty offhand and verify normal inventory routing.
6. Leave/re-enter the world and confirm the offhand item persists.
7. Recheck Bow TPP and Trident FPP visuals without changing calibration values.

Important: disabling the module reverts the constructor instruction for future
Item constructions, but Item singletons already constructed in the current
process keep their flag values. Restart Minecraft for a complete vanilla-policy
rollback.

## Build

Install Android NDK `28.2.13676358`, Ninja, CMake and zip, then run:

```bash
bash ./scripts/build.sh
```

Output:

```text
dist/arm64-v8a/levi-offhand-v0.2.61.1.levipack
```
