from pathlib import Path


SOURCE = Path("src/render/OffhandBlockRenderPatch.cpp").read_text()
NATIVE_HELPER = Path("src/render/NativeAttachmentFix.hpp").read_text()
PATCH_HEADER = Path("src/render/OffhandBlockRenderPatch.hpp").read_text()
MOD_SOURCE = Path("src/LeviOffhandMod.cpp").read_text()
WORKFLOW = Path(".github/workflows/build.yml").read_text()
PRODUCTION_SOURCE = SOURCE + "\n" + NATIVE_HELPER + "\n" + MOD_SOURCE


required = {
    "native helper include": '#include "render/NativeAttachmentFix.hpp"',
    "attachment prepare RVA": "kPrepareAttachmentRva=0x9B36A80",
    "binding mode RVA": "kAttachmentBindingModeRva=0xF147CB0",
    "binding mode first exact caller":
        "kAttachmentBindingModeFirstCallsiteRva=0x9B37780",
    "binding mode second exact caller":
        "kAttachmentBindingModeSecondCallsiteRva=0x9B377D8",
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
    "binding mode fingerprint": "kAttachmentBindingModeFingerprint",
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
    "Trident binding mode hook": "attachmentBindingModeDetour(",
    "Trident binding cache predicate":
        "shouldForceTridentBindingResolve(",
    "resolved bone state guard": "gResolvedTridentFppBindingBone",
    "cross-thread binding generation": "gTridentFppBindingGeneration",
    "native attachment ready gate": "gNativeAttachmentHooksReady",
    "native trampoline lifetime gate":
        "gNativeAttachmentTrampolinesAvailable",
    "global active Trident binding scope":
        "gActiveTridentFppBindingScopes",
    "published binding-mode trampoline":
        "gAttachmentBindingModeOriginalPublished",
    "published prepare trampoline":
        "gPrepareAttachmentOriginalPublished",
    "published resolver trampoline":
        "gResolveOwnerBoneByNameOriginalPublished",
    "native attachment reader count":
        "gActiveNativeAttachmentHookReaders",
    "native attachment reader drain":
        "waitForNativeAttachmentHookReaders(",
    "native binding-mode fallback": "nativeBindingModeDirect(",
    "Trident native prepare probe": "[TridentFppPrepareProbe]",
    "Trident owner binding probe": "[TridentFppBindingProbe]",
    "bounded native probe budget": "consumeProbeBudget(",
    "FPP Bow mask retained": "BowFppWeakItemMask bowMask",
    "v0.2.55 route diagnostic gate": "kReferenceRouteDiagnostic=true",
    "Bow/FishingRod route marker": "[BowFishingRodTppRoute]",
    "Bow native route suppression": "[BowFishingRodTppNativeSuppress]",
    "Trident native route suppression": "[TridentShieldFppNativeSuppress]",
    "Trident/Shield route marker": "[TridentShieldFppRoute]",
    "Shield reference marker": "[ShieldFppReference]",
}

for name, marker in required.items():
    assert marker in SOURCE, f"missing {name}: {marker}"

assert "void setBowTppHorizontalOffset(float value) noexcept;" in PATCH_HEADER, (
    "Bow TPP offset setter implementation has no class declaration"
)
assert "[[nodiscard]] float bowTppHorizontalOffset() const noexcept;" in PATCH_HEADER, (
    "Bow TPP offset getter implementation has no class declaration"
)


assert "kBoneLocalPoseOffset=0x70" in NATIVE_HELPER, (
    "missing binary-proven local pose state offset"
)
assert "kBoneComposedMatrixOffset=0x30" in NATIVE_HELPER, (
    "missing binary-proven composed-matrix cache offset"
)
assert "kBoneMatrixCachedOffset=0xDE" in NATIVE_HELPER, (
    "missing binary-proven matrix-cache flag offset"
)
assert "kBoneBindingModeOffset=0xDC" in NATIVE_HELPER, (
    "missing binary-proven owner-binding mode offset"
)
assert "kBowTppHorizontalMin=0.10F" in NATIVE_HELPER, (
    "Bow TPP slider minimum is not locked to the proven 0.10 bound"
)
assert "kBowTppHorizontalMax=0.20F" in NATIVE_HELPER, (
    "Bow TPP slider maximum is not locked to the proven 0.20 bound"
)
assert "kBowTppHorizontalDefault=0.10F" in NATIVE_HELPER, (
    "Bow TPP slider default is not the current 0.10 position"
)
assert "class ResolvedBindingCache" in NATIVE_HELPER, (
    "resolved binding cache lifecycle is not independently testable"
)
assert "bash tests/run_native_attachment_fix_tests.sh" in WORKFLOW, (
    "CI does not run the native attachment unit/source contracts"
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
    "obsolete Trident pole pose probe": "[TridentFppPolePose]",
    "obsolete Trident pole matrix probe": "[TridentFppPoleMatrix]",
}

for name, marker in forbidden.items():
    assert marker not in PRODUCTION_SOURCE, (
        f"stale or unsafe {name}: {marker}"
    )


draw_start = SOURCE.index("void drawAttachmentDetour(")
draw_end = SOURCE.index("using ComposeAttachmentBoneMatrixFn=", draw_start)
draw_body = SOURCE[draw_start:draw_end]
assert "kReferenceRouteDiagnostic" in draw_body
assert "suppressBowNative" in draw_body
assert "suppressTridentNative" in draw_body
assert draw_body.index("suppressBowNative") < draw_body.index("original(self,stack,slotPointer,parentContext,actor);")
assert draw_body.index("suppressTridentNative") < draw_body.index("original(self,stack,slotPointer,parentContext,actor);")

prepare_start_diag = SOURCE.index("void prepareAttachmentDetour(")
prepare_end_diag = SOURCE.index("using AttachmentBindingModeFn=", prepare_start_diag)
prepare_diag = SOURCE[prepare_start_diag:prepare_end_diag]
assert "!kReferenceRouteDiagnostic" in prepare_diag, (
    "owner-bone remap must be disabled during the reference-route diagnostic"
)


mode_start = SOURCE.index("std::uint8_t attachmentBindingModeDetour(")
mode_end = SOURCE.index("using ResolveOwnerBoneByNameFn=", mode_start)
mode_body = SOURCE[mode_start:mode_end]
direct_index = mode_body.index("nativeBindingModeDirect(bindingState)")
reader_index = mode_body.index("ScopedHookRead readGuard(")
published_index = mode_body.index(
    "gAttachmentBindingModeOriginalPublished.load("
)
active_index = mode_body.index("gActiveTridentFppBindingScopes.load(")
scope_index = mode_body.index("gTridentFppBindingDepth==0")
callsite_index = mode_body.index("isExactMinecraftCallsite(")
hash_index = mode_body.index("const std::uint64_t sourceHash=")
assert direct_index < reader_index < published_index < active_index < scope_index, (
    "binding-mode hook must acquire its published chained original safely, "
    "then reject inactive Trident scope before touching thread-local state"
)
assert mode_body.index("gNativeAttachmentHooksReady.load(") < (
    mode_body.index("synchronizeTridentFppBindingGeneration();")
), "mode cache mutation must stop when lifecycle readiness closes"
assert scope_index < callsite_index < hash_index, (
    "global binding-mode hook must reject non-Trident scopes before "
    "callsite/hash work"
)
assert "minecraftCallsiteRva(" not in mode_body, (
    "exact first-callsite binding gate must not pay for dladdr on its hot path"
)
assert "gAttachmentBindingModeOriginal\n" not in mode_body, (
    "binding-mode detour must not race the unpublished raw trampoline pointer"
)
assert mode_body.index("featureEnabled()") < mode_body.index(
    "synchronizeTridentFppBindingGeneration();"
), "mode detour must acquire feature state before synchronizing generation"


prepare_start = SOURCE.index("void prepareAttachmentDetour(")
prepare_end = mode_start
prepare_body = SOURCE[prepare_start:prepare_end]
assert prepare_body.index("ScopedHookRead readGuard(") < prepare_body.index(
    "gPrepareAttachmentOriginalPublished.load("
), "prepare detour must guard the lifetime of its published trampoline"
assert "gNativeAttachmentHooksReady.load(" in prepare_body, (
    "Trident prepare scope must reject partial hook lifecycle state"
)
assert prepare_body.index("featureEnabled()") < prepare_body.index(
    "synchronizeTridentFppBindingGeneration();"
), "prepare hook must acquire feature state before synchronizing generation"


resolver_start = SOURCE.index("bool resolveOwnerBoneByNameDetour(")
resolver_end = SOURCE.index("using DrawAttachmentFn=", resolver_start)
resolver_body = SOURCE[resolver_start:resolver_end]
assert resolver_body.index("ScopedHookRead readGuard(") < resolver_body.index(
    "gResolveOwnerBoneByNameOriginalPublished.load("
), "resolver detour must guard the lifetime of its published trampoline"
assert "gNativeAttachmentHooksReady.load(" in resolver_body, (
    "Trident resolver mutation must reject partial hook lifecycle state"
)


install_start = SOURCE.index("gPrepareAttachmentHook=")
install_end = SOURCE.index("return true;", install_start)
install_body = SOURCE[install_start:install_end]
assert install_body.index("gResolveOwnerBoneByNameHook=") < install_body.index(
    "gAttachmentBindingModeHook="
), "resolver must be installed before the binding cache gate"
assert install_body.index("gAttachmentBindingModeHook=") < install_body.index(
    "gNativeAttachmentHooksReady.store("
), "binding cache gate must stay disabled until hook installation completes"
assert install_body.index(
    "gAttachmentBindingModeOriginalPublished.store("
) < install_body.index("gNativeAttachmentHooksReady.store("), (
    "binding-mode trampoline must publish before the ready flag"
)
assert install_body.index(
    "gPrepareAttachmentOriginalPublished.store("
) < install_body.index("gNativeAttachmentHooksReady.store("), (
    "prepare trampoline must publish before the ready flag"
)
assert install_body.index(
    "gResolveOwnerBoneByNameOriginalPublished.store("
) < install_body.index("gNativeAttachmentHooksReady.store("), (
    "resolver trampoline must publish before the ready flag"
)
assert install_body.index(
    "gResolveOwnerBoneByNameOriginalPublished.store("
) < install_body.index("gNativeAttachmentTrampolinesAvailable.store("), (
    "all native trampolines must publish before forwarding opens"
)
assert install_body.index("gNativeAttachmentTrampolinesAvailable.store(") < (
    install_body.index("gNativeAttachmentHooksReady.store(")
), "native forwarding must open before mutation readiness"

uninstall_start = SOURCE.index("OffhandBlockRenderPatch::\n    uninstall(\n")
uninstall_body = SOURCE[uninstall_start:]
assert uninstall_body.index("gNativeAttachmentHooksReady.store(") < (
    uninstall_body.index("gNativeAttachmentTrampolinesAvailable.store(")
), "mutation readiness must close before native forwarding"
assert uninstall_body.index("gNativeAttachmentTrampolinesAvailable.store(") < (
    uninstall_body.index("waitForNativeAttachmentHookReaders();")
), "new native readers must close before admitted transactions drain"
assert uninstall_body.index("waitForNativeAttachmentHookReaders();") < (
    uninstall_body.index("gAttachmentBindingModeOriginalPublished.store(")
), "published trampoline must remain available until readers drain"
assert uninstall_body.index("gAttachmentBindingModeOriginalPublished.store(") < (
    uninstall_body.index("if(gAttachmentBindingModeHook)")
), "published trampoline must clear before trampoline removal"
assert uninstall_body.index("waitForNativeAttachmentHookReaders();") < (
    uninstall_body.index("gResolveOwnerBoneByNameOriginalPublished.store(")
), "resolver trampoline must remain available until readers drain"
assert uninstall_body.index("gResolveOwnerBoneByNameOriginalPublished.store(") < (
    uninstall_body.index("if(gResolveOwnerBoneByNameHook)")
), "published resolver trampoline must clear before hook removal"
assert uninstall_body.index("waitForNativeAttachmentHookReaders();") < (
    uninstall_body.index("gPrepareAttachmentOriginalPublished.store(")
), "prepare trampoline must remain available until readers drain"
assert uninstall_body.index("gPrepareAttachmentOriginalPublished.store(") < (
    uninstall_body.index("if(gPrepareAttachmentHook)")
), "published prepare trampoline must clear before hook removal"
assert uninstall_body.index("if(gAttachmentBindingModeHook)") < (
    uninstall_body.index("if(gResolveOwnerBoneByNameHook)")
), "binding cache gate must be removed before the resolver"

installed_start = SOURCE.index("OffhandBlockRenderPatch::\n    installed()")
installed_end = SOURCE.index("    void\n", installed_start)
installed_body = SOURCE[installed_start:installed_end]
assert "gAttachmentBindingModeHook" in installed_body, (
    "installed() must include the mandatory binding-mode hook"
)

feature_start = SOURCE.index("OffhandBlockRenderPatch::\n    setFeatureEnabled(")
feature_end = SOURCE.index("OffhandBlockRenderPatch::\n    featureEnabled()", feature_start)
feature_body = SOURCE[feature_start:feature_end]
assert "invalidateTridentFppBindingGeneration();" in feature_body, (
    "feature changes must invalidate render-thread binding caches"
)
assert feature_body.index("invalidateTridentFppBindingGeneration();") < (
    feature_body.index("mFeatureEnabled.store(")
), "generation must publish before the new feature state"


print(f"native attachment source contract passed: {len(required)} required, "
      f"{len(forbidden)} forbidden")
