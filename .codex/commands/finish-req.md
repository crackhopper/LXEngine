---
name: "Finish Requirement"
description: Verify a requirement doc against current code, simplify, fix defects, and archive to finished/
category: Requirements
tags: [requirements, review, simplify, archive]
---

Take a file under `notes/requirements/`, verify the current code actually delivers what it claims, review that code for simplification opportunities and defects, fix what's found, make sure the REQ number matches actual implementation order, split any active requirement that now spans multiple implementation phases, and archive the requirement to `notes/requirements/finished/`.

**REQ queue invariant**:
- `REQ 文件号 = 实施顺序`。Active requirements must be implementable from smallest filename prefix to largest.
- `一个 REQ 文件 = 一个连续实施周期`。If an active requirement contains both already-actionable work and work that belongs after the target, split it before archiving.
- If a local split or inserted follow-up belongs inside one numeric slot, use suffixes such as `020-a`, `020-b`, `020-c` instead of shifting every later active REQ.
- Users should not have to remember hidden partial order such as "REQ-010 R1-R2 now, R3-R5 after REQ-011".

**Input**: A path to a requirement file. Accepts any of these forms:
- `/finish-req notes/requirements/003b-pipeline-prebuilding.md`
- `/finish-req 003b-pipeline-prebuilding.md`
- `/finish-req 003b` (prefix match against files under `notes/requirements/`)
- `/finish-req 020-a`

**IMPORTANT**: If no argument provided, list `notes/requirements/*.md` (excluding `finished/`) and use **AskUserQuestion** to let the user pick one. Never guess.

---

## Steps

### 1. Resolve the requirement file

- Normalize the argument to an absolute path under `notes/requirements/`
- If the user passed a bare prefix (e.g. `003b`), use `Glob notes/requirements/<prefix>*.md` to find a single match
- If 0 matches: fail and list available files
- If >1 matches: use **AskUserQuestion** to disambiguate
- Confirm the file exists; if it's already under `notes/requirements/finished/`, report that and stop

Announce: "Verifying: `notes/requirements/<filename>`"

### 2. Read and parse the requirement

Read the full file. Identify:
- **Requirement ID** (e.g. REQ-020-a, or historical REQ-003b) from the title
- **Goals** section (what it claims to achieve)
- **需求 / Requirements / R1–Rn** sections (concrete deliverables)
- **修改范围 / Modification scope** table (if present — files touched)
- **测试 / Tests** section
- **依赖 / Dependencies** — confirm upstream REQs are already finished
- **实施状态 / Implementation status** — if it says "已完成", the flow is a re-verification; if "未开始" or "进行中", expect to drive it to completion

### 3. Upstream dependency check

For each dependency named in the doc:
- If it references another REQ file, check whether it lives under `notes/requirements/finished/`
- If a dependency is **not** finished, stop and ask the user whether to proceed anyway or finish the upstream first. This is a blocker unless the user explicitly waives it.

### 4. Verify each Rn against actual code

For every R1…Rn in the requirement:

1. Extract the concrete claim (e.g. "Class `Foo` exposes method `bar()` returning `StringID`")
2. Use **Grep** / **Read** to confirm the claim in the actual codebase
3. Classify the verification result:
   - ✓ **Implemented** — matches the doc precisely
   - ⚠ **Drift** — implemented but diverges from the doc (wrong name, extra params, different semantics)
   - ✗ **Missing** — not found in the codebase
   - ⊘ **Superseded** — doc already has a "Superseded by REQ-X" banner; skip the verification, trust the banner

For each `⚠ Drift` and `✗ Missing` case: decide which side of the truth is correct — the doc (and the code needs fixing) or the code (and the doc is stale). Default to **code is truth** if the code is sensible and the doc is just stale wording; default to **doc is truth** if the doc describes a specific contract that the code half-implements.

Produce a verification table:

```
| R# | Claim                             | Status        | Action              |
|----|-----------------------------------|---------------|---------------------|
| R1 | Foo::bar() returns StringID       | ✓ Implemented | none                |
| R2 | Bar::baz() removed                 | ⚠ Drift       | still exists; delete |
| R3 | Tests cover happy path            | ✗ Missing     | add test            |
```

Show this table to the user **before** making any changes.

### 5. Review for simplification (opt-in)

For the files touched by the requirement (from the 修改范围 table or inferred via grep), look for:
- Dead code / unused overloads / redundant helpers the requirement's delta may have left behind
- Duplicated logic that's now consolidatable (e.g. multiple places calling the same factory with different boilerplate)
- Over-engineered abstractions that the real usage didn't vindicate
- Forward declarations that can be removed now that headers have settled
- Unused `#include`s

Propose concrete simplification deltas — **do not apply them yet**. Present them as a list:

```
Simplification proposals:
1. src/core/foo.hpp:42 — `Foo::helper()` has one caller, inline it
2. src/core/bar.cpp:10 — duplicated error-handling branches, extract `reportError()`
3. ...
```

### 6. Fix defects

For each `⚠ Drift` / `✗ Missing` case from step 4 AND each accepted simplification from step 5:
- Make the minimal edit
- After every 2–3 edits, run `cmake --build ./build` and fix compile errors before continuing
- Prefer correcting code over rewriting the doc — unless the doc describes something that's genuinely wrong or outdated (then update the doc and note the delta in the status section)

If the scope of fixes balloons beyond "simplification + minor drift" (e.g. discovering R4 was never implemented and needs 500 lines of new code), **stop and ask the user** whether to:
- (a) treat this as a true implementation task and invoke `/opsx:propose` to scope it properly
- (b) finish what's verifiable and leave a `TODO` in the requirement's 实施状态 section
- (c) skip the requirement entirely (do not archive)

### 7. Run regression tests

```bash
cmake --build ./build
./build/src/test/<relevant>   # pick tests related to the requirement's scope
```

If the requirement references specific tests (e.g. `test_foo.cpp`), run those. Otherwise run a sensible superset:
- Always run `test_string_table`, `test_pipeline_identity`, `test_pipeline_build_info`, `test_frame_graph`, `test_material_instance` if they exist — these are the "non-GPU core" floor
- Run any test file that imports from the files the requirement touched (grep for the new include paths)

Every test must exit 0 before proceeding to archive.

### 8. Check and adjust implementation-order numbering

Before updating status or archiving, make sure the target REQ number matches the order in which requirements are actually being implemented.

Build context:
- Scan `notes/requirements/*.md` and `notes/requirements/finished/*.md`
- Treat `finished/` as completed history; do not renumber finished requirements unless the user explicitly asks for a history migration
- Treat active files as the current planned order
- Compare the target against lower-numbered active requirements, declared dependencies, and downstream references

Rules:
- **REQ numbers represent implementation order, not creation order.**
- **One active REQ file represents one continuous implementation cycle.**
- If the target is being completed before lower-numbered active requirements, and those lower-numbered requirements are not required upstream dependencies, the target must be renumbered ahead of them before archive.
- If a lower-numbered active requirement contains both work that should remain before the target and work that now belongs after the target, split that active requirement first. Use a suffix family when it is a local split: `010-a` keeps the still-earlier `R` items, the target can become `010-b`, and the later `R` items can move to `010-c`.
- If the target's number is lower than or equal to an already finished requirement and this is not an explicit re-verification of that same archived history, renumber the active target after the finished prefix.
- If a lower-numbered active requirement is a true upstream dependency, stop and finish that upstream first unless the user explicitly waives the dependency; if waived, still renumber to match the actual completion order.
- Letter suffixes (`020-a`, `020-b`) are valid for work implemented inside the base numeric slot and before the next numeric REQ. If a suffixed requirement is finishing out of that slot, convert it to the correct normal sequence number or suffix family.
- Historical compact suffixes such as `003a` / `003b` may remain in archived history, but new active splits should use the dashed form `NNN-a`.

If numbering is already consistent, continue.

If numbering is inconsistent:
1. Produce a numbering plan before editing anything:

   ```
   Implementation-order numbering plan:
   - split: REQ-010 -> REQ-010-a keeps R1-R2
   - target: REQ-011 -> REQ-010-b, notes/requirements/011-foo.md -> notes/requirements/010-b-foo.md
   - later split: R3-R5 move to REQ-010-c, notes/requirements/010-c-later-foo.md
   - unchanged later active requirements: REQ-012+
   - references to update: notes/requirements/*.md, notes/roadmaps/*.md, openspec/changes/**/*.md
   ```

2. Ask the user to confirm the numbering plan.
3. Apply confirmed renames before archiving:
   - Split active requirement files when the plan says so: narrow the original doc's goals / `R1..Rn` / tests / modification scope, and create a new active doc for the later work
   - Add a short blockquote trace note to both split docs: source REQ, split date, retained scope, moved scope
   - Rename active requirement files
   - Update `# REQ-NNN:` / `# REQ-NNN-a:` titles
   - Update `REQ-NNN` / `REQ-NNN-a` references in active requirements, roadmaps, and open OpenSpec changes
   - Do not edit `notes/requirements/finished/*.md` unless the user explicitly requested a history migration
4. If the user rejects the plan, stop before archive. Do not archive an out-of-order requirement.

### 9. Update the requirement's 实施状态

Before moving the file, update its `## 实施状态` section (or create one at the bottom if absent) with a concise completion summary:
- Date (use today's date)
- What was verified / fixed / simplified
- Tests run and their outcome
- Any splitting or numbering adjustment applied in step 8
- Any residual TODOs (rare — ideally none)

Use the style of existing archived requirements under `notes/requirements/finished/` as a template.

### 10. Archive the file

```bash
mv notes/requirements/<final-filename>.md notes/requirements/finished/
```

If a file with the same name already exists under `finished/`, stop and ask the user (likely a mis-archive from a prior session).

### 11. Summary

Report:
- Requirement ID + filename
- Verification outcome (N ✓, N ⚠, N ✗ before fixes)
- Actions taken (code fixes, simplifications, doc updates)
- Splitting / numbering adjustment, if any
- Tests run and results
- Final archive location

Example:

```
## Finish-Req Complete

**Requirement:** REQ-003b (003b-pipeline-prebuilding.md)
**Verification:** 7 R's — 6 ✓, 1 ⚠ (fixed)
**Fixes applied:** 2 (removed stale `getSlots()` accessor; fixed include path in command_buffer.hpp)
**Simplifications:** 1 (inlined single-caller helper `buildLayoutKey`)
**Order adjustment:** unchanged
**Tests:** test_pipeline_build_info / test_frame_graph / test_material_instance all passed
**Archived to:** notes/requirements/finished/003b-pipeline-prebuilding.md
```

---

## Guardrails

- **Never archive without verification**: if step 4 shows any `⚠ Drift` or `✗ Missing` that you didn't resolve, stop before step 10.
- **Never archive without a green build**: step 7 must pass.
- **Never archive out of implementation order**: step 8 must either confirm the number is consistent or apply a user-confirmed split / numbering plan.
- **Respect the doc's own banners**: if the requirement file contains `> **Superseded by REQ-X**` at the top, skip the verification and just move it to `finished/` after confirming with the user.
- **Do not delete tests** under the name of "simplification". Tests are intentionally over-specified.
- **Do not touch other requirements** during this flow except for a user-confirmed implementation-order split / numbering plan. If you discover a latent problem in REQ-X while working on REQ-Y, note it in the summary and move on — don't expand scope.
- **Keep the active list as the implementation queue**: if one active REQ now spans work before and after the target, split it instead of asking the user to remember a hidden partial order.
- **Always ask before a large code rewrite**: "simplification" and "fix defects" have a narrow budget. Anything beyond ~50 changed lines should prompt the user with options.
- **Prefer code is truth**: stale wording in the requirement doc is cheaper to fix than rewriting working code to match a hypothetical contract.
- **If you archive the file, do it as the last action**. Everything up to and including doc updates and confirmed numbering changes happens while the target is still under `notes/requirements/`.
