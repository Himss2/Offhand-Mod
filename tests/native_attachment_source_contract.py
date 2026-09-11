from pathlib import Path


SOURCE = Path("src/render/OffhandBlockRenderPatch.cpp").read_text()


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
    "safe binding prefix copy": "BindingPrefix candidate=readValue<BindingPrefix>",
    "Trident FPP binding depth": "gTridentFppBindingDepth",
    "Trident FPP binding marker": "[TridentFppBoneBinding]",
    "Bow TPP draw depth": "gBowTppAttachmentDepth",
    "Bow pose scope": "shouldOffsetBowPose(",
    "Bow root offset": "offsetBowRight(",
    "Bow root offset marker": "[BowTppRightOffset]",
    "Trident exact scope": "shouldFixTridentPose(",
    "Trident pole guard": "kPoleBoneHash",
    "Trident matrix correction": "rotateTridentPoleHeadUp(",
    "Trident pole rotation marker": "[TridentFppPoleRotation]",
    "FPP Bow mask retained": "BowFppWeakItemMask bowMask",
    "native Trident retained": "suppress generic item-form submission",
}

for name, marker in required.items():
    assert marker in SOURCE, f"missing {name}: {marker}"


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
}

for name, marker in forbidden.items():
    assert marker not in SOURCE, f"stale or unsafe {name}: {marker}"


print(f"native attachment source contract passed: {len(required)} required, "
      f"{len(forbidden)} forbidden")
