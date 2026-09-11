# Bow TPP and Trident FPP Pose Specification

## Scope

This is a bounded visual correction for Minecraft Bedrock 1.26.45.1
(Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`). It must not change
inventory state, gameplay behavior, generic offhand routing, or other items.

## Required behavior

- Keep the proven Bow slot-6 `rightitem` to owner `leftitem` binding so TPP
  renders exactly one Bow in the offhand.
- Move that native TPP Bow `0.20F` toward visual right using semantic
  horizontal basis column 1 (`matrix[4..6]`), normalized before translation.
- Keep the Bow FPP native mask unchanged.
- Recognize both native draw routes: legacy offhand caller `0x9B36370` and
  renderer-v2 caller `0xA2C87BC`. The FPP depth still gates Trident, while
  Bow TPP can receive its offset through either renderer.
- Move Trident FPP from the right to the left by remapping only its slot-6 FPP
  owner binding from `rightitem` to `leftitem`.
- Apply a local 180-degree Z rotation to the Trident `pole` root while keeping
  `matrix[12..14]` unchanged, so its head faces upward without moving the
  native model outside the view frustum.
- Leave Trident TPP, all mainhand attachments, inventory player rendering,
  Crossbow, Fishing Rod, Spear, and blocks unchanged.

## Reverse-engineering evidence

The offhand draw is called with `BL 0x9B368D4` at `0x9B36370` after loading
slot 6. `0x9B368D4` then tail-branches with `B 0x9B3A228` at `0x9B36A18`.
Because a tail branch does not update LR, a detour at `0x9B3A228` observes the
return address from `0x9B36370`, not from `0x9B36A18`. The v0.2.47 guard could
therefore never open the Trident attachment scope, matching the missing
`[TridentFppBonePose]` runtime marker.

Renderer v2 calls the same draw function directly with `BL 0x9B3A228` at
`0xA2C87BC`. Supporting that exact caller is necessary for the Bow TPP offset
on devices where the v2 actor pipeline is enabled; no broad caller matching is
allowed.

## v0.2.48 runtime evidence

The device log emitted both `[BowTppGripOffset]` and
`[TridentFppBonePose]`. Therefore both compose hooks and their item/view scopes
were active; neither symptom came from a missed hook.

The Bow did not move horizontally because v0.2.48 translated along matrix
column 0. The existing device-calibrated held-item mapping establishes column
1 as semantic horizontal (`UI X`).

The Trident disappeared immediately after v0.2.48 negated `matrix[12]` and
reappeared only when that pose was absent. That value is an already-composed
absolute attachment translation, so reflecting it can move the model beyond
the FPP frustum. Side selection must instead use the native owner-bone binding.

## v0.2.49 tuning

Use a named visual-right Bow offset of `0.20F` in native attachment units.
Preserve it as one constant so any final screenshot-based adjustment remains a
single-value change.
