from pathlib import Path

SOURCE = Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
MOD = Path('src/LeviOffhandMod.cpp').read_text()
MANIFEST = Path('manifest.json').read_text()
BUILD = Path('scripts/build.sh').read_text()
WORKFLOW = Path('.github/workflows/build.yml').read_text()

# Bow/Fishing Rod: the exact slot-34 RenderItem probe happens before the
# actual generic renderOffhandItem/final-matrix call.  v0.2.57 must therefore
# carry the family across that boundary with a one-shot pending latch.
assert 'gPendingTppReferenceFamily' in SOURCE
assert 'gPendingTppReferenceArmed' in SOURCE
assert 'class TppReferenceRenderScope final' in SOURCE
assert '[TppReferenceLatch]' in SOURCE

route_start = SOURCE.index('void renderItemRouteDetour(')
route_end = SOURCE.index('using AttachableStateRouteFn=', route_start)
route_body = SOURCE[route_start:route_end]
assert 'gPendingTppReferenceFamily=family;' in route_body
assert 'gPendingTppReferenceArmed=true;' in route_body

render_start = SOURCE.index('OffhandBlockRenderPatch::\n    renderOffhandDetour(')
render_end = SOURCE.index('bool\n    OffhandBlockRenderPatch::\n    blockRenderPredicateDetour(', render_start)
render_body = SOURCE[render_start:render_end]
assert 'TppReferenceRenderScope' in render_body
assert 'toolFamily' in render_body
assert render_body.index('TppReferenceRenderScope') < render_body.rindex('original('), (
    'TPP latch scope must enclose the actual generic offhand render call'
)

final_start = SOURCE.index('finalOffhandMatrixTopDetour(')
final_end = SOURCE.index('OffhandBlockRenderPatch::Matrix64\n    OffhandBlockRenderPatch::\n    itemTransformDetour(', final_start)
final_body = SOURCE[final_start:final_end]
assert '[BowTppGripPivot]' in final_body
assert 'const float preservedBowTx=matrix->value[12];' in final_body
assert 'matrix->value[12]=preservedBowTx;' in final_body
assert 'matrix->value[13]=preservedBowTy;' in final_body
assert 'matrix->value[14]=preservedBowTz;' in final_body

# Trident: keep the native 3D attachment and reopen only the cached owner-bone
# binding at the first proven binding-mode read.  No pole/matrix correction is
# allowed in this revision; left-hand binding must be proven first.
mode_start = SOURCE.index('std::uint8_t attachmentBindingModeDetour(')
mode_end = SOURCE.index('using ResolveOwnerBoneByNameFn=', mode_start)
mode_body = SOURCE[mode_start:mode_end]
assert 'shouldForceTridentBindingResolve(' in mode_body
assert '[TridentFppBindingCacheReset]' in mode_body
assert 'return 0;' in mode_body
assert 'gResolvedTridentFppBindingBones.contains(bindingState)' in mode_body
assert 'kAttachmentBindingModeFirstCallsiteRva' in mode_body

resolver_start = SOURCE.index('bool resolveOwnerBoneByNameDetour(')
resolver_end = SOURCE.index('using DrawAttachmentFn=', resolver_start)
resolver_body = SOURCE[resolver_start:resolver_end]
assert 'mapRightOwnerBoneToLeft(' in resolver_body
assert '[TridentFppBoneBinding]' in resolver_body
assert 'recordResolution(' in resolver_body

compose_start = SOURCE.index('void composeAttachmentBoneMatrixDetour(')
compose_end = SOURCE.index('using FirstPersonDataDrivenFn=', compose_start)
compose_body = SOURCE[compose_start:compose_end]
assert 'mirrorAndRotateTridentLocalPose' not in compose_body
assert 'rotateTridentPoleHeadUp' not in compose_body
assert '[TridentFppPoleRotation]' not in compose_body

assert '[TridentFppNative3D] native slot6 attachment retained' in SOURCE
assert '[TridentFppNative3D] suppress generic 2D item form' in SOURCE

assert '"version": "0.2.57"' in MANIFEST
assert 'levi-offhand-v0.2.57.levipack' in BUILD
assert 'dist/arm64-v8a/levi-offhand-v0.2.57.levipack' in WORKFLOW
assert 'name: levi-offhand-arm64-v0.2.57' in WORKFLOW
assert 'v0.2.57' in MOD

print('v0.2.57 TPP latch + Trident binding reopen contract passed')
