# Swap initialization with the right-use hook installed

Target: Minecraft Bedrock Android 1.26.51.1, arm64-v8a.

The F button already submitted requests. Initialization failed because
RightUseRouter installs a hook on Player::setSelectedItem before SwapEngine
checks that function's original entry bytes. A valid hooked entry therefore
resolved to zero and every F request was ignored.

SwapEngine now validates 48 unchanged body bytes at setter RVA 0xF9F7850 + 32.
The entire 80-byte probe must be in readable executable mappings. Independent
offhand, null, copy, destructor and hand-setter helpers still require their exact
fingerprints before the selected setter is accepted. The callable entry is
retained, preserving the installed hook chain and native slot updates.

Only SwapEngine production code changes. The selected-stack layout reader,
request queue, frame hook, button, right-use routing, consumption/release logic,
rendering and the existing swap write sequence remain unchanged. Invalid bodies
still reject initialization; errors have not been relabeled as success.

## Verification

`bash tests/run_swap_engine_tests.sh` runs the production resolver and exchange
with host substitutes for the Android module mapping and native slot functions.
It covers original and patched entries, an invalid native body, unmapped targets,
two occupied hands, either empty hand, and both empty hands. Exchange assertions
cover item identity, count, metadata, native write order and snapshot cleanup.

The six helper fingerprints were also compared with the supplied game binary
(SHA-256 b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4).
These checks do not substitute for Android gameplay validation.

## Device check

On startup, `setSelected=1` and the swap storage/runtime ready messages should
replace the validation failure. A pre-hooked setter emits an informational
message confirming its native body was verified. Press F with two different
stacks, then with either hand empty; verify the actual slots, counts and item
metadata. Repeat after changing hotbar selection and confirm ordinary right-use,
offhand consumption and release still behave as before. Capture new logs if
the runtime remains unavailable or a request fails.
