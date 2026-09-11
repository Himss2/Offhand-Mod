# Reverse-engineering notes — Minecraft Bedrock Android 1.26.45.1

Target binary:

- file: `libminecraftpe.so`
- architecture: AArch64
- Android min ABI reported by ELF: Android 26
- Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`

## Offhand policy gate

RTTI type-name:

`26OffhandContainerValidation`

The primary vtable was recovered through the RTTI/typeinfo chain. Its address
point contains the validation function at virtual slot 3.

Validated target on this build:

- `OffhandContainerValidation` vtable slot 3
- RVA: `0x0F6F5D10`

The function checks the incoming ItemStack (x3). For a real non-empty item it
ultimately reaches the item's offhand permission getter.

Validated getter:

- RVA: `0x0F667898`

AArch64 body:

```
ldrh w8, [x0, #0x112]
ubfx w0, w8, #7, #1
ret
```

Therefore `allow_off_hand` is represented by bit 7 in the 16-bit flag field at
`Item + 0x112` on this exact build.

The binary also contains `minecraft:allow_off_hand` and
`AllowOffHandItemComponent`, consistent with this policy.

## Milestone-1 patch strategy

Do not mutate every Item object and do not synthesize inventory packets.
Resolve `OffhandContainerValidation` slot 3 through Levi preloader RTTI/vtable
APIs and detour only that dedicated policy gate to return true.

This intentionally leaves vanilla ItemStacks, inventory transactions, item lock
checks, synchronization, and removal behavior untouched.

The runtime additionally verifies the first eight AArch64 instructions of the
1.26.45.1 target before installing the hook. A mismatched build fails closed.
