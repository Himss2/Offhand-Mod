from pathlib import Path
r=Path('src/render/OffhandBlockRenderPatch.cpp').read_text()
s=Path('src/runtime/OffhandValidationHook.cpp').read_text()
assert 'resolveSignature(' not in r, 'visual installer still scans libminecraftpe.so'
assert 'resolveSignature(' not in s, 'storage installer still scans libminecraftpe.so'
assert 'moduleBaseByName(' in r and 'moduleBaseByName(' in s
for x in ['0xADE4590','0xAF21B18','0xADE84A8','0xADDDE6C','0xAF21B50','0xF644970']:
    assert x in r
for x in ['0xF01AD14','0xF024024','0xF705380','0xF704CA0','0xF644930']:
    assert x in s
assert 'matchesFingerprint' in r
assert 'matchesFingerprint' in s
print('v0.2.61.1 fast-init contract passed')
