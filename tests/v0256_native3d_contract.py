from pathlib import Path
import re

SOURCE = Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
MOD = Path('src/LeviOffhandMod.cpp').read_text()
MANIFEST = Path('manifest.json').read_text()
BUILD = Path('scripts/build.sh').read_text()
WORKFLOW = Path('.github/workflows/build.yml').read_text()

# v0.2.56 keeps the proven Bow generic-left route.  The grip position stays
# untouched; a TPP-only 25.2 degree semantic-Y correction is applied at the
# final hand matrix, equivalent to the old 154.8 -> 180.0 target without
# changing Bow FPP calibration.  Fishing Rod gets only a small TPP Y delta.
assert 'kReferenceRouteDiagnostic=true' in SOURCE, 'Bow generic-left route gate was lost'
assert '[BowFishingRodTppNativeSuppress]' in SOURCE, 'Bow native TPP suppression was lost'
assert 'kBowTppGripPivotTiltDegrees=25.20f' in SOURCE, (
    'Bow TPP grip-pivot tilt delta is not locked to +25.20 degrees'
)
assert 'kFishingRodTppVerticalDelta=-0.06f' in SOURCE, (
    'Fishing Rod TPP vertical delta is not locked to -0.06'
)
assert '[BowTppGripPivot]' in SOURCE, 'Bow TPP grip-pivot marker missing'
assert '[FishingRodTppLower]' in SOURCE, 'Fishing Rod TPP lowering marker missing'
assert 'gBowTppGripPivotLogged' in SOURCE, 'Bow TPP tilt log is not bounded'
assert 'gFishingRodTppLowerLogged' in SOURCE, 'Fishing Rod TPP log is not bounded'

# Trident FPP must keep native 3D slot-6 attachment. The v0.2.55 native
# suppression route is forbidden, while generic item-form submission is suppressed.
draw_start = SOURCE.index('void drawAttachmentDetour(')
draw_end = SOURCE.index('using ComposeAttachmentBoneMatrixFn=', draw_start)
draw_body = SOURCE[draw_start:draw_end]
assert 'suppressTridentNative' not in draw_body, 'Trident native 3D attachment is still suppressed'
assert '[TridentShieldFppNativeSuppress]' not in draw_body, 'stale v0.2.55 Trident native suppression remains'
assert '[TridentFppNative3D] native slot6 attachment retained' in draw_body, (
    'native Trident 3D retention marker missing'
)

prepare_start = SOURCE.index('void prepareAttachmentDetour(')
prepare_end = SOURCE.index('using AttachmentBindingModeFn=', prepare_start)
prepare_body = SOURCE[prepare_start:prepare_end]
assert 'shouldRemapTridentOwnerBone(' in prepare_body, 'Trident owner-bone remap was removed'
# Trident remap must be allowed even while Bow reference-route mode remains enabled.
trident_decl = prepare_body[prepare_body.index('const bool remapTridentOwnerBone='):]
trident_decl = trident_decl[:trident_decl.index(';') + 1]
assert '!kReferenceRouteDiagnostic' not in trident_decl, (
    'Trident owner-bone remap is still disabled by Bow diagnostic gate'
)

# v0.2.56 deliberately avoids forcing cache invalidation / binding mode reset.
mode_start = SOURCE.index('std::uint8_t attachmentBindingModeDetour(')
mode_end = SOURCE.index('using ResolveOwnerBoneByNameFn=', mode_start)
mode_body = SOURCE[mode_start:mode_end]
assert 'shouldForceTridentBindingResolve(' not in mode_body, 'Trident binding cache reset is still active'
assert '[TridentFppBindingCacheReset]' not in mode_body, 'Trident cache-reset diagnostic still active'

compose_start = SOURCE.index('void composeAttachmentBoneMatrixDetour(')
compose_end = SOURCE.index('using FirstPersonDataDrivenFn=', compose_start)
compose_body = SOURCE[compose_start:compose_end]
assert 'mirrorAndRotateTridentLocalPose' not in compose_body, 'Trident pole local-pose mutation is still active'
assert 'gTridentFppAttachmentDepth!=0' not in compose_body, 'Trident pole compose scope is still active'

render_start = SOURCE.index('void\n    OffhandBlockRenderPatch::\n    renderObjectDetour(')
render_body = SOURCE[render_start:]
assert '[TridentFppNative3D] suppress generic 2D item form' in render_body, (
    'generic Trident 2D item form is not suppressed'
)

assert '"version": "0.2.56"' in MANIFEST
assert 'levi-offhand-v0.2.56.levipack' in BUILD
assert 'dist/arm64-v8a/levi-offhand-v0.2.56.levipack' in WORKFLOW
assert 'name: levi-offhand-arm64-v0.2.56' in WORKFLOW
assert 'v0.2.56' in MOD

# The TPP tilt must run after the already-accepted generic calibration. Otherwise
# rotating the basis first changes the later positional offset and moves the grip.
final_start = SOURCE.index('finalOffhandMatrixTopDetour(')
final_end = SOURCE.index('OffhandBlockRenderPatch::Matrix64\n    OffhandBlockRenderPatch::\n    itemTransformDetour(', final_start)
final_body = SOURCE[final_start:final_end]
assert final_body.index('applyCalibrationInHandBasis(') < final_body.index('[BowTppGripPivot]'), (
    'Bow TPP tilt runs before generic calibration and can move the accepted grip anchor'
)
assert 'const float preservedBowTx=matrix->value[12];' in final_body
assert 'matrix->value[12]=preservedBowTx;' in final_body
assert 'matrix->value[13]=preservedBowTy;' in final_body
assert 'matrix->value[14]=preservedBowTz;' in final_body

print('v0.2.56 Bow tilt + native Trident 3D contract passed')
