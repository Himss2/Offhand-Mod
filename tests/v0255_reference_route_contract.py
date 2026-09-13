from pathlib import Path

SOURCE = Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
MOD = Path('src/LeviOffhandMod.cpp').read_text()
MANIFEST = Path('manifest.json').read_text()
BUILD = Path('scripts/build.sh').read_text()

required_source = {
    'Bow/FishingRod route marker': '[BowFishingRodTppRoute]',
    'Trident/Shield route marker': '[TridentShieldFppRoute]',
    'TPP RenderItem exact callsite': 'kThirdPersonOffhandRenderItemCallsiteRva=0xA32F030',
    'attachable-state exact callsite': 'kRenderItemAttachableEnabledCallsiteRva=0xADDEADC',
    'attachable predicate target': 'kAttachableStateRva=0xA32F0F4',
    'Bow native attachment suppression marker': '[BowFishingRodTppNativeSuppress]',
    'Trident native attachment suppression marker': '[TridentShieldFppNativeSuppress]',
    'shield reference observation marker': '[ShieldFppReference]',
}
for name, marker in required_source.items():
    assert marker in SOURCE, f'missing {name}: {marker}'

assert 'BowTppAttachableBypass' not in SOURCE, 'stale v0.2.43 Bow bypass marker retained'
assert '[TridentFix] suppress generic item-form submission' not in SOURCE, (
    'Trident generic item-form is still suppressed; shield-route diagnostic cannot run'
)
draw_start = SOURCE.index('void drawAttachmentDetour(')
draw_end = SOURCE.index('using ComposeAttachmentBoneMatrixFn=', draw_start)
draw_body = SOURCE[draw_start:draw_end]
assert '!kReferenceRouteDiagnostic' in draw_body, (
    'legacy Bow/Trident pose scopes are not gated off during the reference-route diagnostic'
)

assert '"version": "0.2.55"' in MANIFEST
assert 'levi-offhand-v0.2.55.levipack' in BUILD
assert 'v0.2.55' in MOD
print('v0.2.55 reference-route source contract passed')
