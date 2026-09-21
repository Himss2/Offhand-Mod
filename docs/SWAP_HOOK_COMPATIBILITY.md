# Swap initialization and repeated exchanges

Target: Minecraft Bedrock Android 1.26.51.1, arm64-v8a.

## Why build 600 initialized but could still desynchronize

Build 600 accepted the setter entry already hooked by RightUseRouter. This fixed
initialization, which the device test confirmed. It did not change the old
three-write occupied exchange or make the native setters equivalent to a slot
swap. The subsequent device report includes missing/duplicate stacks, ghost
placement, and slots that cannot be moved.

The supplied binary shows these distinct paths:

- `Player::setSelectedItem` at `0xF9F7850` reads virtual selected-item view
  `+0x270` and emits a gameplay event before writing inventory. RightUseRouter
  can redirect that view during an offhand use session.
- `Inventory::setItem` at `0xF9DA128` dispatches virtual `+0x70`. Its native
  storage path at `0xF8834D4` calls `0xF9FD618`, which records the real hotbar
  slot before/after and emits its normal notifications.
- LocalPlayer's offhand setter at `0xAAD0360` compares item content/count via
  `0xFFA5B3C` and skips equal stacks. That comparison excludes the stack network
  identity; a one-sided replacement must not give both slots the same identity.
- The offhand transaction path at `0xF9FC7C8` records container 119 through
  `0x1001EC24`. The legacy manager accumulates changes and sends when balanced.

These establish concrete hazards in the old exchange. They do not prove that
all reported client/server symptoms share one cause.

## Current exchange

Swap now writes MAIN through the native inventory slot setter, and OFF through
its existing native virtual hand setter. Both sources are detached first,
including empty stacks. Each changed slot is written once; there is no temporary
MAIN clear or clear-both sequence. Equal content/count/metadata is a whole-swap
no-op, preserving both original network identities.

The physical selected slot must be writable: missing state, the selected-view
flag, and indexes outside the hotbar reject the exchange instead of pretending
MAIN is empty. Active item use is cancelled through native stop-use (not item
release), then the physical slots are read again. A scoped MAIN action protects
native inventory notification reads and restores the caller's previous scope.
The result is checked against both snapshots. A local mismatch is reported as
an error and is not blindly retried or overwritten.

Production changes remain inside `src/swap/`. Right-use, consumption/release,
rendering and button implementations are unchanged. The shared hand-context API
is used as intended; no new gameplay hook or hand-written packet is introduced.

## Verification and limits

`bash tests/run_swap_engine_tests.sh` exercises production code under UBSan with
native ABI substitutes. Fifteen cases include 1,000 repeated same-item swaps,
equal content with distinct network identities, either/both empty slots, active
use cancellation, callback changes during cancellation, invalid selected state,
wrong binary targets, metadata/count preservation and failed-write detection.
The old implementation failed the single-write and equal-content regressions.
These are host regressions, not an emulator of the network protocol.

All nine native fingerprints were checked against the supplied binary SHA-256
`b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4`.
The existing CI routing/rendering suite and Android build remain required.

Device validation must repeat swaps with: one occupied hand; two identical
stacks; different counts of one item; different items; active eating/drinking
or charging. Afterward use each hand, place/break a block, move both stacks in
inventory and reload the world. Check counts and item metadata. A local verified
log is not a server acknowledgement; authoritative reconciliation still needs
this device test. Preserve the new log if a ghost or locked slot remains.
