---
name: update-roadmap
description: Discuss and directly update notes/roadmaps/*.md by cross-checking against notes/requirements/ (active + finished). Use when the user wants to add a new phase, reconcile roadmap with what's actually shipping, or prune stale items. Writes to roadmap files directly after user confirmation.
---

Update `notes/roadmaps/` by reconciling it with `notes/requirements/`. Discuss with the user, then edit the roadmap files directly.

## When To Use

Trigger this skill when the user:

- wants to add or split a roadmap phase
- asks to align roadmap items with current `notes/requirements/`
- wants to mark done items as completed (or prune items that were abandoned)
- says something like "check roadmap drift" / "update roadmap"

Do not use this skill to generate subsystem design docs (that's `/init-notes` or manual authoring) or to draft requirements (that's `/draft-req`).

## Scope

Files this skill is allowed to edit:

- `notes/roadmaps/*.md`
- `notes/roadmaps/README.md` (only to refresh the phase index, if present)

Never edit `notes/requirements/`, `openspec/`, source code, or `notes/subsystems/`.

## Required Workflow

### 1. Load the roadmap corpus

- `Glob notes/roadmaps/*.md` — all phase/theme files
- `Read` each — extract: phase name, stated goals, listed items, status markers

If the directory only has `README.md` (empty roadmap), ask the user what the first phase should be about before proceeding.

### 2. Load the requirements corpus

- `Glob notes/requirements/*.md` — active requirements
- `Glob notes/requirements/finished/*.md` — archived ones

For each file read the header + 实施状态 section. Build an index:

```
REQ-NNN | title | status (未开始 / 进行中 / 已完成) | location (active or finished)
```

### 3. Cross-check for drift

Produce a diff table:

| Roadmap item | Matching REQ(s) | Status | Action |
|---|---|---|---|
| ... | REQ-005, REQ-007 | 已完成/已完成 | mark done |
| ... | REQ-012 | 进行中 | keep as in-progress |
| ... | (none) | — | either drop or ask the user |

Also flag:

- orphan REQs: finished or in-progress requirements that no roadmap item points to
- stale goals: roadmap items contradicted by a shipped/finished REQ
- scope mismatch: items too vague or too narrow to be a real phase

### 4. Propose a change plan

Present the plan before editing files:

```
## Roadmap change plan

notes/roadmaps/phase-01-foundation.md
  - mark "初版资源加载" 完成 (REQ-003, REQ-005 已 finished)
  - remove "XYZ" — direction abandoned
  - add "frame graph 扩展" — from REQ-011 (进行中)

notes/roadmaps/phase-02-materials.md (new)
  - goal: <1 sentence>
  - covers REQ-008, REQ-009, REQ-012
```

Ask the user:

> Apply this plan?
> - yes
> - edit <item>
> - abort

### 5. Surface open questions

Stop and ask before guessing when:

- a REQ has no obvious phase home
- a roadmap item has no matching REQ
- phase ordering needs a call the skill cannot make on its own

Never invent phases silently. Never drop items silently.

### 6. Apply edits

Use `apply_patch` for in-place edits; create new files only for genuinely new phase docs.

File conventions:

- filename: `phase-NN-<kebab-case-theme>.md`
- top heading: `# Phase NN: <theme>`
- sections: 目标 / 范围 / 里程碑 / 关联需求 / 完成判据
- mark done items with `- [x]` and keep `REQ-NNN` back-references

### 7. nav.yml is auto-managed

`notes/nav.yml` has a `@roadmaps` placeholder that auto-expands `notes/roadmaps/*.md` (excluding `README.md`). Do not edit nav.yml for roadmap changes.

### 8. Report

After writing, summarise the edits applied, plus any open items the user still needs to decide.

## Guardrails

- Always present a change plan before editing, and wait for the user's yes.
- Never edit `notes/requirements/*.md`, source code, or files outside `notes/roadmaps/`.
- Roadmap references to requirements must use `REQ-NNN`, not vague prose.
- Do not keep tombstones for dropped items — delete them; history belongs in `git log`.
- When a fact from `notes/requirements/` contradicts the current roadmap, report it to the user rather than amending the requirement.
- Chinese-first prose, English identifiers — match `notes/roadmaps/README.md` style.
