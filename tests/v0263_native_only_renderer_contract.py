#!/usr/bin/env python3
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/render/OffhandBlockRenderPatch.cpp').read_text()
HELPER=(ROOT/'src/render/NativeAttachmentFix.hpp').read_text()
HEADER=(ROOT/'src/render/OffhandBlockRenderPatch.hpp').read_text()
MOD=(ROOT/'src/LeviOffhandMod.cpp').read_text()

MANIFEST=(ROOT/'manifest.json').read_text()
BUILD=(ROOT/'scripts/build.sh').read_text()
WORKFLOW=(ROOT/'.github/workflows/build.yml').read_text()
README=(ROOT/'README.md').read_text()

assert '"version": "0.2.63"' in MANIFEST
assert 'levi-offhand-v0.2.63.levipack' in BUILD
assert 'levi-offhand-v0.2.63.levipack' in WORKFLOW
assert 'levi-offhand-arm64-v0.2.63' in WORKFLOW
assert '## v0.2.63 — native-only Bow/Trident renderer' in README
assert not (ROOT/'src/runtime/OffhandBlockRenderPatch.cpp').exists(), (
    'stale runtime renderer duplicate must be removed'
)

required_source=(
    'kPrepareAttachmentRva=0x9B36A80',
    'kResolveOwnerBoneByNameRva=0xAF3A1E4',
    'kResolveOwnerBoneFirstCallsiteRva=0x9B3779C',
    'kResolveOwnerBoneSecondCallsiteRva=0x9B37814',
    'kLegacyAttachmentRouteRva=0x9B368D4',
    'kComposeAttachmentBoneMatrixRva=0xF147ED0',
    'kComposeAttachmentBoneMatrixCallsiteRva=0x9B254C8',
    'kVariableIsFirstPersonHash=0x2739F381184DE4AEULL',
    'prepareAttachmentDetour(',
    'resolveOwnerBoneByNameDetour(',
    'legacyAttachmentRouteDetour(',
    'queryNativeFirstPerson(',
    'gBowOffhandBindingDepth',
    'gTridentOffhandFppDepth',
    'mirrorTridentOffhandLocalPose',
    '[BowNativeOwnerBinding]',
    '[TridentNativeFppPose]',
)
for marker in required_source:
    assert marker in SOURCE, f'missing native-only renderer marker: {marker}'

required_helper=(
    'mirrorTridentOffhandLocalPose(',
    'class ScopedLocalPoseOverride',
    'shouldRemapBowOwnerBone(',
    'mapRightOwnerBoneToLeft(',
)
for marker in required_helper:
    assert marker in HELPER, f'missing helper marker: {marker}'

for forbidden in (
    'kHandEquipPredicateSignature',
    'handEquipPredicateDetour(',
    'shouldForceOffhandDispatch(',
    'gHandEquipPredicateHook',
    'firstPersonDataDrivenDetour(',
    'gFirstPersonDataDrivenDepth',
    'BowFppWeakItemMask',
    'renderItemRouteDetour(',
    'attachableStateRouteDetour(',
    'gPendingTppReferenceArmed',
    'gPendingTppReferenceFamily',
    'attachmentBindingModeDetour(',
    'gTridentFppBindingGeneration',
    'gResolvedTridentFppBindingBones',
    'drawAttachmentDetour(',
    'gBowTppAttachmentDepth',
    'gTridentFppAttachmentDepth',
    'rotateTridentPoleHeadUp(',
    'gTridentFppHorizontalOffset',
    'gBowTppTiltDegrees',
    'gBowTppHorizontalOffset',
    '[BowFishingRodTppRoute]',
    '[TppReferenceLatch]',
    '[BowFppNativeMask]',
    '[TridentFppHorizontal]',
    '[TridentFppPoleRotation]',
):
    assert forbidden not in SOURCE, f'obsolete renderer path remains: {forbidden}'

for forbidden in (
    'normalizeBowTppHorizontalOffset(',
    'mirrorAndOffsetBowLocalPose(',
    'mirrorAndRotateTridentLocalPose(',
    'normalizeBowTppTiltDegrees(',
    'normalizeTridentFppHorizontalOffset(',
    'tridentOffhandExpressionOwnerHash(',
    'rotateTridentPoleHeadUp(',
    'shouldForceTridentBindingResolve(',
    'class ResolvedBindingCache',
    'shouldRemapTridentOwnerBone(',
    'shouldFixBowLocalPose(',
    'shouldFixTridentLocalPose(',
):
    assert forbidden not in HELPER, f'obsolete helper remains: {forbidden}'

for forbidden in (
    'setBowTppTiltDegrees(',
    'bowTppTiltDegrees()',
    'setTridentFppHorizontalOffset(',
    'tridentFppHorizontalOffset()',
    'setBowTppHorizontalOffset(',
    'bowTppHorizontalOffset()',
):
    assert forbidden not in HEADER, f'obsolete public calibration API remains: {forbidden}'

for forbidden in (
    'kBowTppTiltKey',
    'kTridentFppHorizontalKey',
    'Bow TPP Tilt (TEMP)',
    'Trident FPP Horizontal (TEMP)',
    '.onConfigChanged(',
):
    assert forbidden not in MOD, f'obsolete Mod Menu calibration remains: {forbidden}'

compose_start=SOURCE.index('void composeAttachmentBoneMatrixDetour(')
compose_end=SOURCE.index('\n        }\n',compose_start)+len('\n        }\n')
compose=SOURCE[compose_start:compose_end]
assert 'ScopedLocalPoseOverride' in compose
assert 'original(boneState,pivot,matrix);' in compose
assert compose.index('ScopedLocalPoseOverride') < compose.index('original(boneState,pivot,matrix);')
assert 'kBoneMatrixCachedOffset' not in compose
assert 'kBoneComposedMatrixOffset' not in compose

print('v0.2.63 native-only Bow/Trident renderer contract passed')
