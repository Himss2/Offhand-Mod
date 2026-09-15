# Java-like Offhand Action Routing Design — Minecraft Bedrock 1.26.45.1

Date: 2026-09-15

## Scope

Add a gameplay-action subsystem on top of the current v0.2.67 `main` branch. Existing arbitrary offhand storage, protected auto-insert routing, and renderer work remain separate subsystems.

Target binary:

- Minecraft Bedrock Android 1.26.45.1
- ARM64 / arm64-v8a
- `libminecraftpe.so` SHA-256 `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`
- GNU Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`

## Goals

1. Route left-click and right-click gameplay actions to the correct held hand without physically swapping inventory slots.
2. Detect item capabilities from Minecraft native behavior/components rather than hardcoded item-name tables.
3. Preserve mainhand priority when both hands can perform the requested action.
4. Allow the offhand to perform an action when mainhand lacks the relevant real capability.
5. Keep block/entity interaction priority ahead of item-use for right-click.
6. Keep long-running actions bound to the selected hand from start through finish/cancel.
7. Preserve native durability, cooldown, enchantment, mining speed, use duration, packets, and hand/swing semantics.
8. Fail closed on target mismatch or unknown ABI/fingerprint.

## Final behavior rules

### Right-click

Order:

1. Native block or entity interaction.
2. Mainhand native item-use.
3. Offhand native item-use.
4. Vanilla fallback.

If both held items can perform right-click item-use, mainhand wins.

Examples:

- Main Bow + Off Food -> Main Bow.
- Main Food + Off Bow -> Main Food.
- Main non-use item + Off Food -> Off Food.
- Interactive chest/door/villager -> native interaction first; item-use is considered only if interaction does not handle the action.

### Left-click attack

Order:

1. Mainhand real combat capability.
2. Offhand real combat capability.
3. Mainhand generic vanilla punch fallback.

Generic punch alone is not treated as a real combat capability for hand selection.

Examples:

- Main Sword + Off Axe -> Main Sword.
- Main Bow + Off Sword -> Off Sword.
- Main Apple + Off Sword -> Off Sword.
- Main Apple + Off Apple -> Main generic punch.

### Left-click mining

Order:

1. Mainhand native suitability/effectiveness for the target block.
2. Offhand native suitability/effectiveness for the target block.
3. Mainhand vanilla mining fallback.

Capability detection is target-sensitive and must use Minecraft's native destroy-speed/correct-tool/drop-relevant logic instead of item-name classification.

Examples:

- Main Pickaxe + Off Pickaxe on stone -> Main Pickaxe.
- Main Bow + Off Pickaxe on stone -> Off Pickaxe.

## Architecture

### `HandActionRouter`

New runtime component responsible only for selecting an effective hand for a requested gameplay action.

Action kinds:

- `AttackEntity`
- `MineBlock`
- `UseAir`
- `UseBlock`
- `UseEntity`

The router queries native capability probes for mainhand and offhand, then applies the rules above. It does not mutate inventory state and does not render anything.

### `NativeCapabilityProbe`

Responsible for asking Minecraft whether a held stack has the relevant native capability.

Requirements:

- no hardcoded item-name allow/deny list;
- right-click should prefer native handled/pass semantics where available;
- melee selection should derive real combat capability from native item/component/attribute behavior;
- mining selection should derive suitability/effectiveness from native block/tool logic for the actual target block;
- a failed or unresolved probe must fall back to vanilla/mainhand behavior rather than guessing.

### `ScopedActionHand`

A synchronous, thread-local action context that identifies the effective hand while a routed native action is executing.

When inactive, every hook must be a transparent pass-through.

When active for offhand, only the verified action call chain may resolve held-stack/hand-sensitive operations to the actual offhand stack. It must never physically swap, copy, or rewrite mainhand/offhand inventory slots.

### `ActionSession`

Long-running actions lock their selected hand across ticks.

Session kinds:

- `Mining`
- `UsingItem`
- `Charging`
- `Blocking`

A session records selected hand, initiating stack identity/slot, target when relevant, and start tick/state. It ends through the appropriate native finish/release/cancel path.

Cancel the session if the initiating stack is moved/replaced, the mining target changes, the player/world becomes invalid, native action reports failure/cancel, game mode changes, or the feature is disabled.

Do not silently switch hands in the middle of a session.

## Native boundary strategy

The implementation must hook semantic gameplay boundaries, not raw Android/touch input and not renderer code.

The RE phase must identify and fingerprint the exact 1.26.45.1 boundaries that correspond to these native responsibilities:

- attack entity;
- begin/continue/finalize/cancel block destruction;
- item-use in air;
- use-on-block;
- entity interaction;
- active-use tick/update;
- release/finish/cancel using item;
- hand-aware swing/action packet emission or the nearest native hand carrier used by those paths.

Candidate names such as `GameMode::attack`, `startDestroyBlock`, `continueDestroyBlock`, `destroyBlock`, `baseUseItem`, `useItemOn`, `interact`, and `releaseUsingItem` are semantic search targets only; production hooks are allowed only after the exact binary call graph, ABI, caller scope, and fingerprints are proven for this build.

### Required hook discipline

- No `ContainerValidation` hooks.
- No raw input interception as the gameplay implementation.
- No synthetic inventory transactions.
- No custom item-use packets if the native pipeline can carry the selected hand.
- No global always-on replacement of the selected-item accessor.
- No renderer dependency for gameplay correctness.
- No arbitrary slot swap/restore around actions.
- All action-sensitive detours must fail closed and call the original path unchanged when context or fingerprint is uncertain.

## Network and animation semantics

The goal is not merely to execute the effect with another stack. When offhand is selected, the native pipeline must observe offhand as the logical action hand so that, where Minecraft exposes the distinction, the following originate from the same native action:

- packet/action hand;
- left/offhand swing;
- durability on the actual offhand stack;
- cooldown on the native item/type;
- use duration and release state;
- enchantment/tool effects;
- mining speed and block-drop qualification.

Rendering pose bugs for Bow/Trident remain separate. Gameplay routing must be testable even if a renderer pose is still visually incorrect.

## Lifecycle

`HandActionRouter` installs only after existing storage/auto-insert runtime components are healthy. If router installation fails, existing offhand storage functionality remains usable and the action router stays disabled.

Runtime disable must cancel any active routed action through the safest available native cancel path before deactivating action contexts. Unload removes router hooks before tearing down storage policy.

## Diagnostics

Log only state transitions and first-observation diagnostics, not every tick.

Required categories:

- router installed / fingerprint failure;
- selected action kind and chosen hand on transition;
- start/finish/cancel of long-running sessions;
- fail-closed fallback reason;
- first observed native hand carrier for packet/swing verification.

## Test strategy

### Pure policy tests

Test hand selection independently of Minecraft ABI:

- both capabilities true -> mainhand;
- only main true -> mainhand;
- only off true -> offhand;
- neither true -> mainhand/vanilla fallback;
- attack distinguishes real combat capability from generic punch;
- mining selection is target-sensitive;
- interaction-handled right-click prevents item-use routing.

### Source contracts

Fail if production code introduces:

- item-name hardcoded capability tables;
- physical main/offhand stack swapping;
- `ContainerValidation` action hooks;
- synthetic item-use packet construction;
- always-on selected-item redirection.

Require explicit scoped-context guards and transparent original-call fallbacks.

### Binary contracts

Against the exact supplied 1.26.45.1 binary, verify:

- SHA-256 and Build ID;
- every selected gameplay hook target fingerprint;
- caller/callee boundaries used for scoped hand routing;
- the native hand carrier used for swing/network semantics;
- long-action finish/cancel boundaries.

If any required fingerprint is unresolved, that feature remains disabled rather than guessing an RVA or ABI.

### Device matrix

1. Main Sword + Off Axe: attack uses main Sword.
2. Main Bow + Off Sword: attack uses off Sword and left/offhand swing.
3. Main Apple + Off Sword: attack uses off Sword.
4. Main Apple + Off Apple: main generic punch.
5. Main Pickaxe + Off Pickaxe on stone: main Pickaxe.
6. Main Bow + Off Pickaxe on stone: off Pickaxe; speed, durability and drops follow offhand tool.
7. Main Bow + Off Food: right-click uses main Bow.
8. Main non-use item + Off Food: right-click uses off Food.
9. Chest/door/lever/villager interaction wins before item-use.
10. Offhand Bow/Food/Shield/Trident/Spear long-use remains bound to offhand through hold/release.
11. Moving the active stack during a long action cancels cleanly and does not switch hands.
12. Feature disable during an active session cancels safely.
13. Multiplayer/server behavior is recorded separately; the mod must not bypass server validation with fabricated packets.

## Implementation order

1. RE and binary-contract the gameplay action boundaries and native hand carrier.
2. Add pure `HandActionPolicy` tests and implementation.
3. Add `ScopedActionHand` and guarded stack/hand routing.
4. Implement instant attack and right-click routing first.
5. Add mining session routing.
6. Add hold/release item-use sessions.
7. Verify swing/network/durability/cooldown behavior on device.
8. Only after gameplay is correct, reconnect renderer work to logical action-hand state if useful.

## Success criteria

This milestone is successful only when the device tests prove that the chosen hand, actual stack mutation, native action result, and hand/swing semantics agree. A visual-only change or a stack-effect-only change without correct native hand semantics does not count as complete.
