from pathlib import Path


SOURCE = Path("src/render/OffhandBlockRenderPatch.cpp").read_text()
NATIVE_HELPER = Path("src/render/NativeAttachmentFix.hpp").read_text()


required = {
    "native helper include": '#include "render/NativeAttachmentFix.hpp"',
    "attachment prepare RVA": "kPrepareAttachmentRva=0x9B36A80",
    "name binding RVA": "kResolveOwnerBoneByNameRva=0xAF3A1E4",
    "name resolver first exact caller":
        "kResolveOwnerBoneFirstCallsiteRva=0x9B3779C",
    "name resolver second exact caller":
        "kResolveOwnerBoneSecondCallsiteRva=0x9B37814",
    "attachment draw RVA": "kDrawAttachmentRva=0x9B3A228",
    "exact FPP DataDriven caller":
        "kFirstPersonDataDrivenCallsiteRva=0xADE9E9C",
    "effective offhand draw caller":
        "isEffectiveOffhandDrawCallsite(",
    "bone matrix RVA": "kComposeAttachmentBoneMatrixRva=0xF147ED0",
    "bone matrix exact caller":
        "kComposeAttachmentBoneMatrixCallsiteRva=0x9B254C8",
    "prepare fingerprint": "kPrepareAttachmentFingerprint",
    "name resolver fingerprint": "kResolveOwnerBoneByNameFingerprint",
    "draw fingerprint": "kDrawAttachmentFingerprint",
    "bone matrix fingerprint": "kComposeAttachmentBoneMatrixFingerprint",
    "fingerprint gate": "matchesFingerprint(",
    "exact caller gate": "isExactMinecraftCallsite(",
    "Bow exact scope": "shouldRemapBowOwnerBone(",
    "Trident binding scope": "shouldRemapTridentOwnerBone(",
    "Bow owner-bone remap": "mapRightOwnerBoneToLeft(",
    "safe binding prefix snapshot": "const BindingPrefix sourceBinding=",
    "Trident FPP binding depth": "gTridentFppBindingDepth",
    "Trident FPP binding marker": "[TridentFppBoneBinding]",
    "Bow TPP draw depth": "gBowTppAttachmentDepth",
    "Bow pose scope": "shouldFixBowLocalPose(",
    "scoped local pose override": "ScopedLocalPoseOverride localPoseOverride(",
    "Bow calibrated local pose correction": "mirrorAndOffsetBowLocalPose",
    "Bow local pose marker": "[BowTppLocalPose]",
    "Trident exact scope": "shouldFixTridentLocalPose(",
    "Trident pole guard": "kPoleBoneHash",
    "Trident local pose correction": "mirrorAndRotateTridentLocalPose",
    "Trident local pose marker": "[TridentFppLocalPose]",
    "Trident native prepare probe": "[TridentFppPrepareProbe]",
    "Trident owner binding probe": "[TridentFppBindingProbe]",
    "Trident pole pose probe": "[TridentFppPolePose]",
    "Trident composed matrix probe": "[TridentFppPoleMatrix]",
    "bounded native probe budget": "consumeProbeBudget(",
    "FPP Bow mask retained": "BowFppWeakItemMask bowMask",
    "native Trident retained": "suppress generic item-form submission",
}

for name, marker in required.items():
    assert marker in SOURCE, f"missing {name}: {marker}"

assert "kBoneLocalPoseOffset=0x70" in NATIVE_HELPER, (
    "missing binary-proven local pose state offset"
)
assert "kBoneComposedMatrixOffset=0x30" in NATIVE_HELPER, (
    "missing binary-proven composed-matrix cache offset"
)
assert "kBoneMatrixCachedOffset=0xDE" in NATIVE_HELPER, (
    "missing binary-proven matrix-cache flag offset"
)


forbidden = {
    "renderItem diagnostic": "[ToolTppAttachableProbe]",
    "attachable cache diagnostic": "[BowTppAttachableCache]",
    "humanoid mislabel diagnostic": "[HumanoidHandSlotProbe]",
    "actor-wide TPP identity diagnostic": "[BowTppAttachableIdentity]",
    "actor-wide TPP suppression": "[BowTppNativeSuppress]",
    "generic Bow bypass": "[BowTppAttachableBypass]",
    "impossible tail-branch caller guard":
        "kDrawAttachmentCallsiteRva=0x9B36A18",
    "Trident absolute-position reflection": "matrix[12]=-matrix[12]",
    "obsolete combined Trident transform": "mirrorToOffhandAndFlipPole(",
    "v0.2.49 Bow composed-matrix offset": "offsetBowRight(",
    "v0.2.49 Trident composed-matrix rotation":
        "rotateTridentPoleHeadUp(",
    "manual composed-matrix cache overwrite":
        "writeValue<OffhandBlockRenderPatch::Matrix64>(",
    "v0.2.49 Bow marker": "[BowTppRightOffset]",
    "v0.2.49 Trident marker": "[TridentFppPoleRotation]",
    "v0.2.50 mirror-only Bow helper": "mirrorBowLocalPose",
}

for name, marker in forbidden.items():
    assert marker not in SOURCE, f"stale or unsafe {name}: {marker}"


compose_start = SOURCE.index("void composeAttachmentBoneMatrixDetour(")
compose_end = SOURCE.index("using FirstPersonDataDrivenFn=", compose_start)
compose_body = SOURCE[compose_start:compose_end]
override_index = compose_body.index("ScopedLocalPoseOverride localPoseOverride(")
original_index = compose_body.rindex("original(boneState,pivot,matrix);")
assert override_index < original_index, (
    "local attachment pose must be overridden before F147ED0 composition"
)


print(f"native attachment source contract passed: {len(required)} required, "
      f"{len(forbidden)} forbidden")
