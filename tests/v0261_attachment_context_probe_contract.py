from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/render/OffhandBlockRenderPatch.cpp').read_text()
MOD = (ROOT / 'src/LeviOffhandMod.cpp').read_text()
MANIFEST = (ROOT / 'manifest.json').read_text()

assert 'constexpr bool kAttachmentContextProbe=true;' in SOURCE
assert '[AttachmentContextPrepare]' in SOURCE
assert '[AttachmentContextDraw]' in SOURCE
assert '[AttachmentContextBone]' in SOURCE
assert '[AttachmentContextBinding]' in SOURCE
assert 'nativeFpp=%d' in SOURCE
assert 'legacyDataDriven=%d' in SOURCE
assert 'family=Shield' in SOURCE or 'probeFamilyName(' in SOURCE
assert 'kAttachmentContextProbe' in SOURCE

# Probe build must observe the native attachment path without applying the
# previous per-item visual calibration/mutation while the probe gate is on.
assert '&& !kAttachmentContextProbe' in SOURCE

# The temporary visual sliders must not be exposed in the probe build.
assert 'Bow TPP Tilt (TEMP)' not in MOD
assert 'Trident FPP Horizontal (TEMP)' not in MOD
assert 'kBowTppTiltKey' not in MOD
assert 'kTridentFppHorizontalKey' not in MOD

assert 'v0.2.61 attachment-context probe' in MOD
assert '"version": "0.2.61"' in MANIFEST

print('v0.2.61 attachment-context probe contract passed')
