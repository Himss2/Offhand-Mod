# Historical Branch Archive

This document preserves the useful conclusions from non-`main` branches before repository branch cleanup. It is documentation only: the accepted runtime/source baseline remains `main`, with F-swap behavior protected from build #721 (`73336dccfa4c86476ebb2940de99a00e321ff7b2`).

## Baseline policy

Build #721 is the accepted swap baseline. Unrelated fixes and features must not change its swap transaction/storage mechanism. In particular, preserve:

- UI-only `SwapButton` -> queued intent -> preFrame `SwapRuntime` -> `SwapEngine`.
- direct selected-stack read from Player selected state instead of calling the RightUseRouter-hooked `Player::getSelectedItem`;
- MAIN/HOTBAR mutation through `Player::setSelectedItem`;
- OFF mutation through raw writer `0xF579C24` with the matching native-equivalent bookkeeping;
- legacy InventoryAction OFF container 119 / slot 0 versus predictive Hand container 19 / OFF slot 1;
- occupied swap order `MAIN A->EMPTY -> OFF B->A -> MAIN EMPTY->B`;
- non-relocating native predictive scope lifetime;
- asynchronous legacy settlement semantics;
- 50 ms F-spam throttle/coalescing and quiet success hot path.

## Right-use and action-routing history

The historical `feature/java-like-offhand-actions` work is already superseded by the staged 1.26.51.1 path on `main`. The active entrypoint intentionally does **not** install `HandActionRouter` or `NativeSemanticBridge`; `RightUseRouter` is the validated path.

The useful build-#550 work from `test/java-right-use-repair` (`2a3a9a7e1a11ab53899e46923aee62e6cc208acc`) is retained in current source/tests:

- Sword's WeaponItem no-op must not falsely claim right-click ownership.
- Specialized MAIN actions such as Shears/Bow/Trident/Fishing Rod/food/shield keep MAIN priority.
- OFF block/air use uses detached ItemStack snapshots.
- MAIN/OFF call ordering and block-count reconciliation must stay independent from swap.
- Renderer changes must never alter gameplay/action dispatch.

The `test/offhand-consume-release` work is also in the current lineage: OFF completion/release keeps the native completion/callback envelope and redirects only the scoped final write/hand transaction to OFF. It remains subject to device validation for consumables, containers, bows/tridents, durability, reconnect, and server acceptance.

## Renderer history

`native-only-bow-trident-renderer` and the v0.2.66/v0.2.67 staging/test branches established the native attachment approach used by current renderer materialization:

- Bow stays a native offhand attachment rather than a duplicate generic draw.
- Trident/Spear native 3D paths keep their native carrier/owner-bone logic.
- Renderer source and `NativeAttachmentFix.hpp` must remain from matching materialization generations.
- Inventory/paperdoll state must not be used as a proxy for TPP world attachment state.
- Old forced generic routes, duplicate suppression hacks, and stale Banner offsets are historical/rejected approaches.

The v0.2.67 staging branches mainly preserved CI/materialization payloads. Their useful output is already retained by current `.github` payloads, workflow, generator, renderer source, and regression tests.

## Native offhand policy history

The v0.2.60/v0.2.61 experimental branches are historical and should not be resurrected as-is.

Important retained conclusion:

- arbitrary manual OFF storage should be enabled by native item/offhand policy;
- automatic insertion into OFF is filtered separately by `AutoInsertRouting`;
- ContainerValidation storage/swap hooks are not part of the accepted architecture.

The old 1.26.45.1 constructor flag model and v0.2.61 fast-init experiments were superseded on 1.26.51.1 by the verified `ItemStackBase::getAllowOffHand` policy patch plus the current auto-insert guard.

## Swap investigation history

The numerous `debug/swap-*`, `diagnosis/swap-*`, and `test/swap-*` branches were created to isolate the one-empty/occupied synchronization problem. Their useful conclusions are retained here so the rejected experiments do not need permanent branches.

### Proven boundaries retained in #721

1. `Player::setSelectedItem @ 0xF9F7850` is already hook-chained by RightUseRouter at swap install time. Swap may accept that known live executable target only after the other exact-build guards pass.
2. `Player::getSelectedItem @ 0xF9F7824` is not used by SwapEngine; selected storage is read directly from Player selected state.
3. LocalPlayer OFF bookkeeping and selected HOTBAR bookkeeping are asymmetric. OFF modern-mode writes can skip the legacy container-119 action, so SwapEngine supplies the native-equivalent missing action.
4. Legacy InventoryAction OFF uses container `119 (0x77)`, slot `0`.
5. Predictive `ContainerType::Hand (19)` uses the native Hand SimplePlayerContainer, where OFF is slot `1`.
6. `ItemStackNetManagerClient::_addLegacyTransactionRequestSetItemSlot` takes an `ItemStackNetManagerScreen&`; passing `Player*` was a rejected diagnosis.
7. The Player-aware predictive wrapper at `0xF88A434` returns an inline self-referential final-action scope. It must be constructed in its final address and must not be byte-copied or copy-assigned.
8. Current screen ownership is resolved from the manager screen stack used by `setPlayerContainer`, rather than depending on the unrelated historical top-screen lookup.
9. A valid local/removable swap can still have a pending legacy transaction in the same frame. Same-frame settlement is therefore observational, not the success criterion.
10. F-spam is coalesced and limited to roughly 20 logical swap starts per second, with per-success Logcat/thread-name diagnostics removed.

### Rejected approaches

Do not revive these without new binary evidence:

- raw selected/offhand container vtable bridge;
- Place-vs-Swap synthetic ItemStackRequest theory;
- ContainerValidation-based swap;
- synthetic InventoryTransactionPacket/ItemStackRequest packet generation;
- clear-both/refill variants that disturb the accepted 44a order;
- public OFF setter as a post-swap normalizer;
- post-settlement hand healer;
- mainhand healer setter;
- right-click-triggered recovery/healing;
- native-client/offhand/request-owner post-normalization;
- dual transaction boundaries;
- using stack pointer identity alone as transaction ownership;
- treating immediate `InventoryTransactionManager` settlement as required success.

Packet/authority trace branches were diagnostic only. Their value was observing transaction behavior; no packet tracing/logging belongs in the release hot path.

### Representative historical checkpoints

- build #610 family: established the working isolated F runtime and exposed the one-empty detach/swap problem.
- build #662/#667 family: restored the accepted storage/action architecture after rejected normalization experiments.
- scope-lifetime diagnosis (`62371048f79155dd8047341a4ee62062fbb7dadd`): reject relocated legacy scope instead of destroying stack memory.
- safe native screen registration (`8dc51b0d389f33f2ed42b4a01f1630dbf1dd1f9f`): predictive touched-slot bookkeeping without changing the accepted storage writer.
- Hand slot-domain correction leading to `f9b75172b9fc7ecbb30801ec460a7dcb704f9f91`: predictive Hand OFF slot is 1 while legacy container-119 OFF slot is 0.
- build #721 (`73336dccfa4c86476ebb2940de99a00e321ff7b2`): accepted swap baseline, quiet/coalesced F path and asynchronous-settlement semantics.
- `test/swap-perf-714` tip `2fc9dc8d07c1d320c8ff607976aafdde86cccfce` was a later test-only experiment whose workflow failed; it is not a baseline and is intentionally not merged.

## Branch families safe to delete

The knowledge needed from these branch families is now either integrated into `main` or recorded above:

- `debug/*`
- `diagnosis/*`
- `feature/java-like-offhand-actions`
- `native-only-bow-trident-renderer`
- `staging/*`
- `temp/*`
- `test/*`

After cleanup, `main` is the sole maintained branch. New experiments should be short-lived and their durable findings must be folded back into `README.md` or `docs/` before deletion.
