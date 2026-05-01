---
name: init-notes
description: Bootstrap a project's notes/ directory by scanning code + build config. Use when the user just installed notes-scaffold and wants to generate the initial notes/README.md, notes/get-started.md, and notes/subsystems/*.md from actual project state. Only touches notes/; never edits src/, notes/requirements/, or top-level README.
---

Bootstrap the project's documentation under `notes/` by scanning code and build config. Treat the codebase as the source of truth.

## When To Use

Use this skill when the user:

- has just installed `notes-scaffold` and wants a first pass of documentation
- asks to generate `notes/README.md`, `notes/get-started.md`, or subsystem docs from scratch
- wants subsystem docs regenerated after a large restructuring

Do not use this skill to modify code, top-level `README.md`, `CLAUDE.md`, `AGENTS.md`, `notes/requirements/`, or `openspec/`.

## Scope

This skill writes exactly these files:

- `notes/README.md`
- `notes/get-started.md`
- `notes/subsystems/index.md`
- `notes/subsystems/<name>.md` (one per subsystem chosen by user)
- `notes/nav.yml` (append subsystem entries under the "设计 → 子系统" group)

Nothing else.

## Required Workflow

### 1. Inspect current state

Read:

- `notes/README.md`
- `notes/get-started.md`
- `notes/subsystems/index.md`
- `notes/nav.yml`

If they look like scaffold placeholders (mention `/init-notes`, "bootstrap", "占位"), they are safe to overwrite.

If they contain user-written content, stop and ask:

> `notes/README.md` already has real content. Overwrite, only generate subsystems, or abort?
> - overwrite
> - subsystems-only
> - abort

### 2. Scan the project root

Detect:

- language and toolchain: `package.json`, `pyproject.toml`, `Cargo.toml`, `go.mod`, `CMakeLists.txt`, `Makefile`, etc.
- source entry points: `src/`, `lib/`, `app/`, `cmd/`, `internal/`, main files
- test entry points: `tests/`, `test/`, `__tests__/`
- existing human docs: top-level `README.md`, `CONTRIBUTING.md`, `AGENTS.md`, `CLAUDE.md`

Use glob + read to extract:

- the project's own description from the top-level README (without copying it wholesale)
- the real top-level subdirectories that look like subsystems vs. scaffolding noise

### 3. Identify subsystem candidates

For each top-level source subdirectory, decide whether it is a real subsystem (multiple files, outward-facing boundary, not pure tooling).

Present a list:

```
## Subsystem candidates

1. <name> — src/<path>/, <one-line summary>
2. ...

Which to document?
- all
- 1,3
- skip (skip subsystem docs this run)
```

Wait for the user's selection before drafting subsystem docs.

### 4. Extract build / run commands

Look up:

- install: `pip install`, `npm install`, `cargo build`, etc.
- build: if a separate step
- test: `pytest`, `cargo test`, `go test`, `npm test`
- dev server: if applicable

Prefer project manifest, `Makefile`, and top-level README. If a command cannot be found, leave a TODO rather than inventing one.

### 5. Draft notes/README.md

Structure:

- one-line positioning statement
- 2–4 paragraphs on what the project is, synthesised from real signals
- architecture overview (optional mermaid diagram)
- recommended reading order
- code map (directory → purpose table)
- maintenance conventions (only describe current truth; history goes in `git log`)

No fluff, no generic platitudes. Every claim must trace back to code or an existing doc.

### 6. Draft notes/get-started.md

Sections: what you'll learn, dependencies, build + run, tests, preview notes site (`scripts/notes/serve_site.sh`), how to add new docs, how to update the roadmap.

Drop any section where the project does not have a corresponding concept.

### 7. Draft notes/subsystems/*.md

For each selected subsystem:

- responsibilities
- core abstractions with `path:line` references (grep-verified, never invented)
- data flow (paragraph + optional mermaid)
- boundaries (what this subsystem does not do)
- further reading (relative links to other subsystem docs)

When implementation details are unclear, leave a TODO instead of fabricating.

### 8. Update notes/subsystems/index.md

Replace the placeholder with a short reading order that references the generated subsystem files.

### 9. Update notes/nav.yml

Append each new `subsystems/<name>.md` under the `"设计" → "子系统"` group. Preserve the order from `index.md`. Do not touch `@requirements` / `@roadmaps` / `@temporary` placeholders.

### 10. Preview + confirm

Show all drafts to the user in one message. Offer: `yes`, `edit <file>`, `redo-subsystems`, `abort`.

### 11. Apply + summarise

Write all confirmed files. Summarise what landed and suggest:

- running `scripts/notes/serve_site.sh` to preview
- using `/subsystem-doc-audit` later when code and docs drift

## Guardrails

- Only write inside `notes/`. Never touch source code, top-level `README.md`, `CLAUDE.md`, `AGENTS.md`, `notes/requirements/`, or `openspec/`.
- All `path:line` references must come from a real grep hit in this session.
- Do not invent build or test commands. If the project does not advertise one, write TODO and move on.
- One subsystem per file. Do not merge multiple subsystems into a combined "architecture" document — that is a separate follow-up.
- Match the tone of existing `notes/requirements/` — Chinese prose, English identifiers.
- If the user already has real content in `notes/README.md`, stop and ask before overwriting.

## Output Style

- State which files you're about to draft before drafting.
- Present all drafts together before writing, so the user can approve in one pass.
- Do not claim "repo-wide sync" when the user selected `subsystems-only`.
