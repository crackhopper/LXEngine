---
name: subsystem-doc-audit
description: Audit subsystem design docs against the current codebase from scratch. Use when the user asks to verify notes/subsystems, related specs, or directly referenced docs against code, and update docs to match implementation. Process one subsystem at a time, prefer code over docs, and remove archive/finished references rather than preserving them.
---

Audit subsystem documentation against the current codebase. Treat code as the source of truth unless the user explicitly asks to change code.

## When To Use

Use this skill when the user asks to:

- verify `notes/subsystems/*.md` against code
- resync subsystem docs after refactors
- check related specs or directly referenced docs for drift
- remove stale references to archived / finished change artifacts

Do not use this skill for feature implementation unless the user explicitly wants doc updates tied to that implementation.

## Operating Mode

- Start from scratch each time. Do not trust previous notes or earlier audit results.
- Process exactly one subsystem at a time unless the user explicitly broadens the scope.
- Keep reads narrow. Read the target subsystem doc first, then only the directly relevant implementation files.
- After the subsystem doc is aligned, check only the specs and docs that the subsystem doc directly references or depends on.
- Prefer current code over notes, specs, plans, archived changes, and historical intent.
- Do not preserve references to `archive`, `finished`, or equivalent historical change outputs. If a current doc points to them, remove the reference instead of updating it.

## Required Workflow

### 1. Scope one subsystem

Start with exactly one target such as:

- `notes/subsystems/frame-graph.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/material-system.md`

Read only:

- the target doc
- the code files that define or implement that subsystem

Avoid loading neighboring subsystem docs up front.

### 2. Compare doc to code

For the target subsystem, verify at minimum:

- type names
- field names
- function names and signatures
- ownership and lifetime model
- actual data flow
- current limitations / transitional behavior
- which layer owns which responsibility

When the doc is more idealized than the code, document the real behavior, including transitional quirks that matter for future readers.

### 3. Update the subsystem doc

Edit the target doc to match code. Favor concise, implementation-accurate descriptions over aspirational architecture.

Always capture:

- what the subsystem is for
- the current data flow
- the current boundaries and non-obvious limitations
- where to edit behavior next time

### 4. Check direct references

Only after the target doc is fixed, inspect the docs it directly cites or that directly describe the same boundary.

Typical follow-up targets:

- the referenced Superpowers design spec when one exists
- directly linked subsystem docs
- indexes that summarize subsystem docs

Update only the parts that are directly inconsistent with the code or with the corrected subsystem doc.

### 5. Remove stale references

If any checked doc references:

- archived changes
- finished changes
- historical proposal artifacts

remove those references unless the user explicitly asks to keep historical links.

### 6. Verify before moving on

Before switching to the next subsystem, confirm:

- the target subsystem doc matches the current implementation
- direct references checked in this pass are also aligned
- no new stale archive / finished references were introduced

## Reading Discipline

- Prefer `sed -n` and focused reads over large dumps.
- Read one subsystem at a time.
- Do not bulk-read all subsystem docs in one pass.
- If a subsystem references another subsystem, only read the minimal portion needed to fix the cross-reference.

## Editing Rules

- Use `apply_patch` for doc edits.
- Keep terminology consistent with code identifiers.
- Do not invent future design unless explicitly labeled as a current limitation.
- If code is in a transitional state, say so plainly.

## Output Style

When reporting progress or completion:

- state which subsystem you are working on
- mention if you also updated a directly referenced spec or doc
- summarize the concrete mismatches you corrected

Do not present the work as a broad repo-wide sync if only one subsystem was audited.
