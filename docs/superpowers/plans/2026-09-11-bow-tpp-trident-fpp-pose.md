# Bow TPP and Trident FPP Pose Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the single native Bow slightly inward in TPP and make the native offhand Trident appear left-side-up in FPP.

**Architecture:** Preserve the proven Bow owner-bone remap and adjust only its composed root matrix inside a slot-6 TPP draw scope. Use semantic horizontal matrix column 1 for Bow. For Trident, use an exact FPP-scoped owner-bone remap for hand placement and a translation-preserving local pole rotation for orientation.

**Tech Stack:** C++20, Levi Launcher HookHandle, AArch64 static disassembly, Python source contracts, ASan/UBSan host unit tests.

**Spec:** `docs/superpowers/specs/2026-09-11-bow-tpp-trident-fpp-pose.md`

## Global Constraints

- Target Minecraft Bedrock 1.26.45.1 with Build ID `868e275cb295e9a275bb29d2258edc2f7dc48761`.
- Preserve exactly one Bow in TPP and the existing Bow FPP mask.
- Never affect mainhand, Trident TPP, inventory player rendering, or unrelated item families.
- Keep all native RVA hooks protected by exact instruction fingerprints.

---

### Task 1: Pure pose rules

**Files:**
- Modify: `tests/native_attachment_fix_test.cpp`
- Modify: `src/render/NativeAttachmentFix.hpp`

**Interfaces:**
- Consumes: `MatrixValues`, slot constants, item/view booleans.
- Produces: `isEffectiveOffhandDrawCallsite(std::uintptr_t)`, `shouldOffsetBowPose(bool, std::uint32_t, bool)`, `offsetBowRight(MatrixValues&, float)`, `shouldRemapTridentOwnerBone(...)`, and `rotateTridentPoleHeadUp(...)`.

- [ ] **Step 1: Write failing unit tests**

Add assertions proving that only `0x9B36370` is the effective native offhand
draw caller, Bow offset scope accepts only Bow + slot 6 + TPP, and a known
known matrix moves by exactly `0.20F` along normalized semantic-horizontal
column 1, and Trident rotation preserves translation.

- [ ] **Step 2: Run the unit test and verify RED**

Run: `bash tests/run_native_attachment_fix_tests.sh`

Expected: compilation fails because the three new helper interfaces do not
exist.

- [ ] **Step 3: Implement the minimal pure helpers**

Define `kEffectiveOffhandDrawCallsiteRva=0x9B36370`,
`kV2AttachmentDrawCallsiteRva=0xA2C87BC`,
`kBowTppRightOffset=0.20F`, exact Bow/Trident scope logic, normalized column-1
translation, and translation-preserving pole rotation.

- [ ] **Step 4: Run the unit test and verify GREEN**

Run: `bash tests/run_native_attachment_fix_tests.sh`

Expected: all native attachment host tests pass under ASan/UBSan.

### Task 2: Native draw and composition scopes

**Files:**
- Modify: `tests/native_attachment_source_contract.py`
- Modify: `src/render/OffhandBlockRenderPatch.cpp`

**Interfaces:**
- Consumes: Task 1 helpers and existing `gFirstPersonDataDrivenDepth`.
- Produces: distinct Bow TPP and Trident FPP binding/draw scopes.

- [ ] **Step 1: Add failing source-contract assertions**

Require the effective offhand caller helper, Bow TPP draw depth, Bow root-only
offset, the updated Bow marker, and the existing Trident marker. Forbid the
obsolete `kDrawAttachmentCallsiteRva=0x9B36A18` guard.

- [ ] **Step 2: Run the source contract and verify RED**

Run: `python3 tests/native_attachment_source_contract.py`

Expected: failures for the missing Bow pose scope and obsolete Trident caller.

- [ ] **Step 3: Implement the minimal hook integration**

At `drawAttachmentDetour`, require exact legacy caller `0x9B36370` or exact v2
caller `0xA2C87BC`, slot 6, and item identity. Open Bow depth only outside the
FPP pass and Trident depth only inside it. At
`composeAttachmentBoneMatrixDetour`, offset Bow only for
`rightitem`/`rightItem` root hashes. Remap Trident's owner bone only inside the
exact FPP pass, rotate only `pole`, and preserve its translation; update both
returned and cached matrices.

- [ ] **Step 4: Run unit and source tests and verify GREEN**

Run: `bash tests/run_native_attachment_fix_tests.sh`

Expected: unit tests and source contract pass.

### Task 3: Static binary contract

**Files:**
- Create: `tests/native_attachment_binary_contract.py`
- Modify: `tests/run_native_attachment_fix_tests.sh`

**Interfaces:**
- Consumes: fixed RVAs and the uploaded `libminecraftpe.so` analysis path.
- Produces: assertions for slot-6 load, upstream `BL`, tail `B`, compose `BL`, and entry fingerprints.

- [ ] **Step 1: Add the effective call-chain assertions**

Verify `0x9B36358` loads slot 6, `0x9B36370` is `BL 0x9B368D4`,
`0x9B36A18` is `B 0x9B3A228`, `0xA2C87BC` is `BL 0x9B3A228`, and
`0x9B254C8` is `BL 0xF147ED0`.

- [ ] **Step 2: Run the complete native attachment test script**

Run: `bash tests/run_native_attachment_fix_tests.sh`

Expected: all source, host, and static binary checks pass.

### Task 4: Versioning, documentation, and packaging

**Files:**
- Modify: `README.md`
- Modify: `manifest.json`
- Modify: `scripts/build.sh`
- Modify: `.github/workflows/build.yml`
- Modify: `src/LeviOffhandMod.cpp`

**Interfaces:**
- Consumes: verified host/static v0.2.49 implementation.
- Produces: consistent v0.2.49 metadata and source archives.

- [ ] **Step 1: Update user-facing behavior and version references**

Document the runtime-corrected Bow horizontal basis and Trident owner-binding
strategy; change every package/version reference to `0.2.49`.

- [ ] **Step 2: Run full verification**

Run syntax checks for all translation units with `-std=c++20 -Wall -Wextra
-Wpedantic -Werror`, run `bash tests/run_native_attachment_fix_tests.sh`, scan
for stale version strings, and validate JSON.

- [ ] **Step 3: Commit and package**

Commit the verified branch, then create full-source and changed-files ZIP
archives with repository-relative paths and verify both archives with
`unzip -t`.
