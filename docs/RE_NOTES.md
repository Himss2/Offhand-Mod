# Reverse-engineering notes — Minecraft Bedrock Android 1.26.45.1

Target binary:

- file: `libminecraftpe.so`
- architecture: AArch64
- Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`
- SHA-256: `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`

All RVAs below are exact-build targets and are guarded by native fingerprints.

## Native display contexts

Minecraft maps Item Display Transform contexts as follows:

```text
0 gui
1 firstperson_righthand
2 firstperson_lefthand
3 thirdperson_righthand
4 thirdperson_lefthand
5 ground
6 fixed
7 head
8 embedded
9 shelf
```

The native offhand FPP path selects context 2 and the world offhand TPP path selects context 4. Paperdoll is a separate render state and must not be treated as TPP world state.

## Bow slot-6 attachment

Bow keeps its local animation bone `rightitem`. Unlike Crossbow, Bow does not provide an expression binding that maps that local bone to the owner hand.

Relevant native path:

- attachment prepare: `0x9B36A80`
- owner-name resolver: `0xAF3A1E4`
- resolver direct callers: `0x9B3779C`, `0x9B37814`

For Bow attachment slot 6, the mod clones the binding prefix, changes only the clone's owner lookup hash from `rightitem/rightItem` to `leftitem/leftItem`, runs the native resolver, then copies only the resolved owner bone/geometry indices back. The live local bone name/hash is never changed, preserving Bow animation.

`0x9B36A80` has one direct BL caller (`0xA2C837C`) and that caller passes `isFirstPerson=0`, so Bow owner remapping is deliberately scoped by **Bow + attachment slot 6**, not by perspective.

## Trident native FPP scope

Trident already uses `q.item_slot_to_bone_name(c.item_slot)`. On this build native `off_hand` resolves to `leftitem`; no binding-mode or owner-bone mutation is required.

The exact legacy attachment route helper is `0x9B368D4`. Its seven direct BL callers are:

```text
0x9B361AC 0x9B361D8 0x9B36204 0x9B36230
0x9B3625C 0x9B36314 0x9B36370
```

`0x9B36314` is mainhand slot 5 and `0x9B36370` is offhand slot 6. The helper evaluates `variable.is_first_person` natively through:

- actor type helper: `0xEC8A478`
- Molang variable lookup: `0xEE63508`
- Molang value view: `0xEEA721C`
- string RVA: `0x2652B1D`
- FNV/hash: `0x2739F381184DE4AE`
- accepted legacy first-person actor type: `0x13F`

The mod opens a synchronous RAII scope only for **Trident + slot 6 + native first-person=true** and lets the original route/draw execute normally.

## Trident local pose composition

Attachment bone composition is `0xF147ED0`; the relevant attachment caller is `0x9B254C8`.

Bone state layout used by the renderer:

```text
+0x30 composed/owner matrix seed
+0x70 local position xyz
+0x7C local rotation xyz
+0x88 local scale xyz
+0xDE composed-matrix-present flag
```

`+0xDE` does not mean "return immediately". When set, Minecraft seeds the output from `+0x30` and then continues applying the local pose. Therefore the mod never clears `+0xDE` and never overwrites `+0x30`.

Inside the exact Trident offhand FPP scope and only for the `pole` bone, the current animated local pose is mirrored before the original composition call:

```text
position.x = -position.x
rotation.y = -rotation.y
rotation.z = -rotation.z
```

Position Y/Z, rotation X and scale remain native. The original local pose is restored immediately after composition. Because the current animated pose is mirrored rather than a hard-coded rest pose, normal wield/raise/use animation can remain under Minecraft's animation system.

## Removed renderer architecture

v0.2.63 intentionally removes the old forced generic hand-equipped dispatch, Bow weak-item mask, pending TPP latch, Bow native-draw suppression, Trident binding/cache mutation, final-matrix horizontal/tilt corrections and Bow/Trident temporary sliders. Bow and Trident now remain on one native attachment path.

## Storage/routing remains independent

v0.2.62's storage architecture remains unchanged: Item construction enables the native offhand capability and the central automatic-insertion planner excludes container slot 34 from generic automatic destinations. No `ContainerValidation` hook is reintroduced.
