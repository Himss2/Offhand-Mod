from pathlib import Path

SOURCE = Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
HELPER = Path('src/render/NativeAttachmentFix.hpp').read_text()
MOD = Path('src/LeviOffhandMod.cpp').read_text()
MANIFEST = Path('manifest.json').read_text()
BUILD = Path('scripts/build.sh').read_text()
WORKFLOW = Path('.github/workflows/build.yml').read_text()

# Bow: v0.2.57 proved the latch/final matrix executes. The visible lean is
# screen-plane rotation, so semantic Rot Z must map to native Rx (first arg).
assert 'gPendingTppReferenceFamily' in SOURCE
assert 'class TppReferenceRenderScope final' in SOURCE
assert '[TppReferenceLatch]' in SOURCE
final_start = SOURCE.index('finalOffhandMatrixTopDetour(')
final_end = SOURCE.index('OffhandBlockRenderPatch::Matrix64\n    OffhandBlockRenderPatch::\n    itemTransformDetour(', final_start)
final_body = SOURCE[final_start:final_end]
assert '[BowTppGripPivot] semanticRotZDelta=' in final_body
assert 'const float bowTppTilt=' in final_body
assert 'bowTppTilt,\n                        0.0f,\n                        0.0f' in final_body
assert 'matrix->value[12]=preservedBowTx;' in final_body
assert 'matrix->value[13]=preservedBowTy;' in final_body
assert 'matrix->value[14]=preservedBowTz;' in final_body
assert 'kBowTppTiltKey' in MOD
assert '"Bow TPP Tilt (TEMP)"' in MOD
assert '.onConfigChanged(onModuleConfigChanged)' in MOD

# Trident: official geometry pole uses q.item_slot_to_bone_name(c.item_slot),
# so the actual native route is mode-3 expression binding.
assert 'kMolangHashedStringViewRva=0xEEAB3AC' in SOURCE
assert 'kTridentExpressionBindingCallsiteRva=0x9B37BD4' in SOURCE
assert 'kMolangHashedStringViewFingerprint' in SOURCE
expr_start = SOURCE.index('const void* molangHashedStringViewDetour(')
expr_end = SOURCE.index('using DrawAttachmentFn=', expr_start)
expr = SOURCE[expr_start:expr_end]
assert 'gTridentFppBindingDepth==0' in expr
assert 'gFirstPersonDataDrivenDepth==0' in expr
assert 'gNativeAttachmentHooksReady.load(' in expr
assert 'kTridentExpressionBindingCallsiteRva' in expr
assert 'tridentOffhandExpressionOwnerHash(' in expr
assert '[TridentFppExpressionBinding]' in expr
assert 'return &gTridentFppExpressionProxyHash;' in expr
assert 'kLeftItemCamelHash' in HELPER

# Native 3D remains; generic 2D remains suppressed; pole flip preserves owner T.
draw_start = SOURCE.index('void drawAttachmentDetour(')
draw_end = SOURCE.index('using ComposeAttachmentBoneMatrixFn=', draw_start)
draw = SOURCE[draw_start:draw_end]
assert '[TridentFppNative3D] native slot6 attachment retained' in draw
assert 'const bool fixTrident=' in draw
compose_start = SOURCE.index('void composeAttachmentBoneMatrixDetour(')
compose_end = SOURCE.index('using FirstPersonDataDrivenFn=', compose_start)
compose = SOURCE[compose_start:compose_end]
assert 'rotateTridentPoleHeadUp(' in compose
assert '[TridentFppPoleRotation]' in compose
assert 'const float tx=matrix->value[12];' in compose
assert 'matrix->value[12]=tx;' in compose
assert 'matrix->value[13]=ty;' in compose
assert 'matrix->value[14]=tz;' in compose
assert '[TridentFppNative3D] suppress generic 2D item form' in SOURCE

assert '"version": "0.2.58"' in MANIFEST
assert 'levi-offhand-v0.2.58.levipack' in BUILD
assert 'dist/arm64-v8a/levi-offhand-v0.2.58.levipack' in WORKFLOW
assert 'name: levi-offhand-arm64-v0.2.58' in WORKFLOW
assert 'v0.2.58' in MOD

print('v0.2.58 Bow Rot-Z + Trident expression-left/pole contract passed')
