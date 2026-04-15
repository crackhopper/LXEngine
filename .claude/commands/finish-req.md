---
name: "Finish Requirement"
description: Verify a requirement doc against current code, simplify, fix defects, and archive to finished/
category: Requirements
tags: [requirements, review, simplify, archive]
---

Take a file under `docs/requirements/`, verify the current code actually delivers what it claims, review that code for simplification opportunities and defects, fix what's found, and archive the requirement to `docs/requirements/finished/`.

**Input**: A path to a requirement file. Accepts any of these forms:
- `/finish-req docs/requirements/003b-pipeline-prebuilding.md`
- `/finish-req 003b-pipeline-prebuilding.md`
- `/finish-req 003b` (prefix match against files under `docs/requirements/`)

**IMPORTANT**: If no argument provided, list `docs/requirements/*.md` (excluding `finished/`) and use **AskUserQuestion** to let the user pick one. Never guess.

---

## Steps

### 1. Resolve the requirement file

- Normalize the argument to an absolute path under `docs/requirements/`
- If the user passed a bare prefix (e.g. `003b`), use `Glob docs/requirements/<prefix>*.md` to find a single match
- If 0 matches: fail and list available files
- If >1 matches: use **AskUserQuestion** to disambiguate
- Confirm the file exists; if it's already under `docs/requirements/finished/`, report that and stop

Announce: "Verifying: `docs/requirements/<filename>`"

### 2. Read and parse the requirement

Read the full file. Identify:
- **Requirement ID** (e.g. REQ-003b) from the title
- **Goals** section (what it claims to achieve)
- **需求 / Requirements / R1–Rn** sections (concrete deliverables)
- **修改范围 / Modification scope** table (if present — files touched)
- **测试 / Tests** section
- **依赖 / Dependencies** — confirm upstream REQs are already finished
- **实施状态 / Implementation status** — if it says "已完成", the flow is a re-verification; if "未开始" or "进行中", expect to drive it to completion

### 3. Upstream dependency check

For each dependency named in the doc:
- If it references another REQ file, check whether it lives under `docs/requirements/finished/`
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

### 8. Update the requirement's 实施状态

Before moving the file, update its `## 实施状态` section (or create one at the bottom if absent) with a concise completion summary:
- Date (use today's date)
- What was verified / fixed / simplified
- Tests run and their outcome
- Any residual TODOs (rare — ideally none)

Use the style of existing archived requirements under `docs/requirements/finished/` as a template.

### 9. Archive the file

```bash
mv docs/requirements/<filename>.md docs/requirements/finished/
```

If a file with the same name already exists under `finished/`, stop and ask the user (likely a mis-archive from a prior session).

### 10. Summary

Report:
- Requirement ID + filename
- Verification outcome (N ✓, N ⚠, N ✗ before fixes)
- Actions taken (code fixes, simplifications, doc updates)
- Tests run and results
- Final archive location

Example:

```
## Finish-Req Complete

**Requirement:** REQ-003b (003b-pipeline-prebuilding.md)
**Verification:** 7 R's — 6 ✓, 1 ⚠ (fixed)
**Fixes applied:** 2 (removed stale `getSlots()` accessor; fixed include path in command_buffer.hpp)
**Simplifications:** 1 (inlined single-caller helper `buildLayoutKey`)
**Tests:** test_pipeline_build_info / test_frame_graph / test_material_instance all passed
**Archived to:** docs/requirements/finished/003b-pipeline-prebuilding.md
```

---

## Guardrails

- **Never archive without verification**: if step 4 shows any `⚠ Drift` or `✗ Missing` that you didn't resolve, stop before step 9.
- **Never archive without a green build**: step 7 must pass.
- **Respect the doc's own banners**: if the requirement file contains `> **Superseded by REQ-X**` at the top, skip the verification and just move it to `finished/` after confirming with the user.
- **Do not delete tests** under the name of "simplification". Tests are intentionally over-specified.
- **Do not touch other requirements** during this flow. If you discover a latent problem in REQ-X while working on REQ-Y, note it in the summary and move on — don't expand scope.
- **Always ask before a large code rewrite**: "simplification" and "fix defects" have a narrow budget. Anything beyond ~50 changed lines should prompt the user with options.
- **Prefer code is truth**: stale wording in the requirement doc is cheaper to fix than rewriting working code to match a hypothetical contract.
- **If you move the file, do it as the last action**. Everything up to and including doc updates happens with the file still in `docs/requirements/`.
