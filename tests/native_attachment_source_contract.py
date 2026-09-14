from pathlib import Path

SOURCE=Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
HELPER=Path('src/render/NativeAttachmentFix.hpp').read_text()
HEADER=Path('src/render/OffhandBlockRenderPatch.hpp').read_text()
MOD=Path('src/LeviOffhandMod.cpp').read_text()
WORKFLOW=Path('.github/workflows/build.yml').read_text()
PRODUCTION='\n'.join((SOURCE,HELPER,HEADER,MOD))

required={
    'native helper include':'#include "render/NativeAttachmentFix.hpp"',
    'Bow prepare RVA':'kPrepareAttachmentRva=0x9B36A80',
    'Bow resolver RVA':'kResolveOwnerBoneByNameRva=0xAF3A1E4',
    'Bow resolver caller 1':'kResolveOwnerBoneFirstCallsiteRva=0x9B3779C',
    'Bow resolver caller 2':'kResolveOwnerBoneSecondCallsiteRva=0x9B37814',
    'Trident route RVA':'kLegacyAttachmentRouteRva=0x9B368D4',
    'Trident compose RVA':'kComposeAttachmentBoneMatrixRva=0xF147ED0',
    'Trident compose caller':'kComposeAttachmentBoneMatrixCallsiteRva=0x9B254C8',
    'native FPP query hash':'kVariableIsFirstPersonHash=0x2739F381184DE4AEULL',
    'native FPP string RVA':'kVariableIsFirstPersonStringRva=0x2652B1D',
    'native actor type RVA':'kAttachmentActorTypeRva=0xEC8A478',
    'native Molang lookup RVA':'kMolangVariableLookupRva=0xEE63508',
    'tiny value view callable':'kMolangValueViewRva=0xEEA721C',
    'Bow prepare detour':'prepareAttachmentDetour(',
    'Bow resolver detour':'resolveOwnerBoneByNameDetour(',
    'Trident route detour':'legacyAttachmentRouteDetour(',
    'native FPP query':'queryNativeFirstPerson(',
    'Trident compose detour':'composeAttachmentBoneMatrixDetour(',
    'Bow scope':'gBowOffhandBindingDepth',
    'Trident scope':'gTridentOffhandFppDepth',
    'ready gate':'gNativeAttachmentHooksReady',
    'trampoline gate':'gNativeAttachmentTrampolinesAvailable',
    'reader drain':'waitForNativeAttachmentHookReaders()',
    'Bow marker':'[BowNativeOwnerBinding]',
    'Trident marker':'[TridentNativeFppPose]',
}
for name,marker in required.items():
    assert marker in SOURCE,f'missing {name}: {marker}'

for marker in (
    'kPrepareAttachmentFingerprint',
    'kResolveOwnerBoneByNameFingerprint',
    'kLegacyAttachmentRouteFingerprint',
    'kComposeAttachmentBoneMatrixFingerprint',
    'kAttachmentActorTypeFingerprint',
    'kMolangVariableLookupFingerprint',
    'kMolangValueViewFingerprint',
    'matchesFingerprint(',
):
    assert marker in SOURCE,f'missing fingerprint/validation marker: {marker}'

for marker in (
    'kBoneLocalPoseOffset=0x70',
    'kBoneComposedMatrixOffset=0x30',
    'kBoneMatrixCachedOffset=0xDE',
    'mirrorTridentOffhandLocalPose(',
    'class ScopedLocalPoseOverride',
    'shouldRemapBowOwnerBone(',
    'mapRightOwnerBoneToLeft(',
    'class ScopedHookRead',
):
    assert marker in HELPER,f'missing helper contract: {marker}'

for marker in (
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
    'gAttachmentBindingModeHook',
    'gTridentFppBindingGeneration',
    'gResolvedTridentFppBindingBones',
    'drawAttachmentDetour(',
    'gDrawAttachmentHook',
    'gBowTppAttachmentDepth',
    'gTridentFppAttachmentDepth',
    'rotateTridentPoleHeadUp(',
    'gTridentFppHorizontalOffset',
    'gBowTppTiltDegrees',
    'gBowTppHorizontalOffset',
    '[BowFishingRodTppRoute]',
    '[BowFishingRodTppNativeSuppress]',
    '[TppReferenceLatch]',
    '[BowFppNativeMask]',
    '[TridentFppHorizontal]',
    '[TridentFppPoleRotation]',
    '[TridentFppBindingProbe]',
):
    assert marker not in SOURCE,f'obsolete renderer architecture remains: {marker}'

for marker in (
    'mirrorAndOffsetBowLocalPose(',
    'mirrorAndRotateTridentLocalPose(',
    'normalizeBowTppTiltDegrees(',
    'normalizeTridentFppHorizontalOffset(',
    'rotateTridentPoleHeadUp(',
    'shouldForceTridentBindingResolve(',
    'class ResolvedBindingCache',
    'shouldRemapTridentOwnerBone(',
):
    assert marker not in HELPER,f'obsolete helper remains: {marker}'

for marker in (
    'setBowTppTiltDegrees(',
    'bowTppTiltDegrees()',
    'setTridentFppHorizontalOffset(',
    'tridentFppHorizontalOffset()',
    'setBowTppHorizontalOffset(',
    'bowTppHorizontalOffset()',
):
    assert marker not in HEADER,f'obsolete calibration API remains: {marker}'

for marker in (
    'Bow TPP Tilt (TEMP)',
    'Trident FPP Horizontal (TEMP)',
    '.onConfigChanged(',
    'strtof(',
):
    assert marker not in MOD,f'obsolete Mod Menu calibration remains: {marker}'

# Bow: prepare opens only Bow+slot6 scope. Perspective is deliberately absent.
ps=SOURCE.index('void prepareAttachmentDetour(')
pe=SOURCE.index('using ResolveOwnerBoneByNameFn=',ps)
prepare=SOURCE[ps:pe]
assert 'shouldRemapBowOwnerBone(' in prepare
assert 'gBowOffhandBindingDepth' in prepare
assert 'isFirstPerson' in prepare  # forwarded ABI arg
call=prepare[prepare.index('shouldRemapBowOwnerBone('):]
call=call[:call.index(');')+2]
assert 'isFirstPerson' not in call,'Bow owner remap must not be perspective-scoped'
assert 'stackMatchesId(stack,kBowIdRva)' in prepare

# Resolver clones the local binding and copies only resolved owner indices back.
rs=SOURCE.index('bool resolveOwnerBoneByNameDetour(')
re=SOURCE.index('using LegacyAttachmentRouteFn=',rs)
resolver=SOURCE[rs:re]
assert 'BindingPrefix candidate=source;' in resolver
assert 'mapRightOwnerBoneToLeft(' in resolver
assert 'candidate.nameHash=leftHash;' in resolver
assert 'writeValue<std::int32_t>(' in resolver
assert 'writeValue<std::uint64_t>' not in resolver
assert resolver.count('writeValue<std::int32_t>(')==2

# Trident scope is native slot6 + native variable.is_first_person, not a render latch.
qs=SOURCE.index('bool queryNativeFirstPerson(')
qe=SOURCE.index('class ScopedNativeDepth',qs)
query=SOURCE[qs:qe]
assert 'actorType(actor)!=kLegacyFirstPersonActorType' in query
assert 'readValue<void*>(parentContext,8,nullptr)' in query
assert 'kVariableIsFirstPersonHash' in query
assert 'gMolangVariableLookupTarget' in query
assert 'gMolangValueViewTarget' in query

ls=SOURCE.index('void legacyAttachmentRouteDetour(')
le=SOURCE.index('using ComposeAttachmentBoneMatrixFn=',ls)
legacy=SOURCE[ls:le]
assert 'slot==native_attachment_fix::kOffhandSlot' in legacy
assert 'stackMatchesId(stack,kTridentIdRva)' in legacy
assert 'queryNativeFirstPerson(parentContext,actor)' in legacy
assert 'ScopedNativeDepth scope(' in legacy
assert 'gTridentOffhandFppDepth' in legacy

# F147ED0 gets a temporary local pose mutation before original compose. Cache and
# composed owner matrix are not touched by the production compose detour.
cs=SOURCE.index('void composeAttachmentBoneMatrixDetour(')
ce=SOURCE.index('\n        }\n',cs)+len('\n        }\n')
compose=SOURCE[cs:ce]
assert 'gTridentOffhandFppDepth!=0' in compose
assert 'kPoleBoneHash' in compose
assert 'ScopedLocalPoseOverride poseOverride(' in compose
assert 'mirrorTridentOffhandLocalPose' in compose
assert compose.index('ScopedLocalPoseOverride') < compose.index(
    'original(boneState,pivot,matrix);'
)
assert 'kBoneMatrixCachedOffset' not in compose
assert 'kBoneComposedMatrixOffset' not in compose

# Install publishes Bow trampolines before mutation readiness and installs both
# Trident route+compose hooks before flipping ready=true.
is_=SOURCE.index('OffhandBlockRenderPatch::\n    install(')
ie=SOURCE.index('OffhandBlockRenderPatch::\n    uninstall(',is_)
install=SOURCE[is_:ie]
assert install.index('gPrepareAttachmentOriginalPublished.store(') < install.index(
    'gNativeAttachmentHooksReady.store(true'
)
assert install.index('gResolveOwnerBoneByNameOriginalPublished.store(') < install.index(
    'gNativeAttachmentHooksReady.store(true'
)
assert install.index('gLegacyAttachmentRouteHook=') < install.index(
    'gNativeAttachmentHooksReady.store(true'
)
assert install.index('gComposeAttachmentBoneMatrixHook=') < install.index(
    'gNativeAttachmentHooksReady.store(true'
)

us=SOURCE.index('OffhandBlockRenderPatch::\n    uninstall(')
ue=SOURCE.index('OffhandBlockRenderPatch::\n    setToolCalibration(',us)
uninstall=SOURCE[us:ue]
assert uninstall.index('gNativeAttachmentHooksReady.store(false') < uninstall.index(
    'waitForNativeAttachmentHookReaders();'
)
assert uninstall.index('waitForNativeAttachmentHookReaders();') < uninstall.index(
    'gResolveOwnerBoneByNameOriginalPublished.store('
)
assert uninstall.index('gResolveOwnerBoneByNameOriginalPublished.store(') < uninstall.index(
    'if(gResolveOwnerBoneByNameHook)'
)

ins=SOURCE.index('OffhandBlockRenderPatch::\n    installed()')
ine=SOURCE.index('OffhandBlockRenderPatch::\n    renderOffhandDetour(',ins)
installed=SOURCE[ins:ine]
for marker in (
    'gPrepareAttachmentHook','gResolveOwnerBoneByNameHook',
    'gLegacyAttachmentRouteHook','gComposeAttachmentBoneMatrixHook',
):
    assert marker in installed,f'installed() missing {marker}'

assert '0xEEAB3AC' not in PRODUCTION
assert 'reinterpret_cast<void*>(&gMolangValueViewTarget)' not in SOURCE
assert 'bash tests/run_native_attachment_fix_tests.sh' in WORKFLOW

print('native attachment source contract passed: native-only Bow/Trident lifecycle')
