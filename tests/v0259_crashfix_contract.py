from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/render/OffhandBlockRenderPatch.cpp').read_text()
MOD=(ROOT/'src/LeviOffhandMod.cpp').read_text()
MANIFEST=(ROOT/'manifest.json').read_text()
for forbidden in (
    'kMolangHashedStringViewRva',
    'molangHashedStringViewDetour(',
    'gMolangHashedStringViewHook',
    'gMolangHashedStringViewOriginal',
):
    assert forbidden not in SOURCE, f'unsafe tiny-function hook survived: {forbidden}'
assert 'kNativeOffHandSlotHash=0x5D4C22812BA3AF8CULL' in SOURCE
assert 'kNativeLeftItemResultHash=0x1CF3FDCBB0AB92F7ULL' in SOURCE
assert '[TridentFppNativeBinding] off_hand->leftitem verified statically' in SOURCE
assert 'gTridentFppHorizontalOffset' in SOURCE
assert 'setTridentFppHorizontalOffset(' in SOURCE
assert 'tridentFppHorizontalOffset()' in SOURCE
assert '[TridentFppHorizontal]' in SOURCE
assert 'kTridentFppHorizontalKey' in MOD
assert 'Trident FPP Horizontal (TEMP)' in MOD
assert '"version": "0.2.59"' in MANIFEST
print('v0.2.59 crashfix + native-left Trident calibration contract passed')
