---
name: update-notes
description: Generate or incrementally update human-readable Chinese notes under notes/ based on the current codebase and git diff since the last sync. Use when notes need to be regenerated, incrementally refreshed, or updated for a specific subsystem.
---

Maintain human-readable Chinese project notes under `notes/`.

## Core Rules

- Notes describe the current implementation only.
- Deleted or renamed concepts must be physically removed from notes; do not leave tombstones.
- Notes are summaries and navigation, not copies of Superpowers design specs.
- Preserve manual sections marked with `<!-- manual --> ... <!-- manual:end -->`, but still flag stale content inside them.
- Treat `src/`, current `notes/`, and approved Superpowers specs as context; do not describe unimplemented design as current behavior.

## Modes

- default: incremental from `notes/.sync-meta.json`
- `--full`: full regeneration
- `--dry-run`: report planned writes only
- `<subsystem>`: refresh one subsystem note directly

## Workflow

1. Read `notes/.sync-meta.json` if present.
2. Decide mode:
   - first run / full
   - incremental
   - one subsystem
   - dry run
3. In incremental mode, diff `lastSyncedCommit..HEAD` and map changed source files to target notes files.
4. In full mode, scan:
   - `AGENTS.md`
   - `CLAUDE.md`
   - `docs/superpowers/specs/*.md`
   - `notes/subsystems/*.md`
   - top-level project structure
   - key public APIs in headers
5. For each target note:
   - read the current note
   - read its mapped source files
   - rewrite only affected sections
   - remove references to deleted classes, functions, files, or designs
6. Update `notes/.sync-meta.json`.
7. If the local notes server is running, refresh it after writing.

## Minimum Outputs In Full Mode

- `notes/README.md`
- `notes/architecture.md`
- `notes/glossary.md`
- relevant `notes/subsystems/*.md`

## Writing Style

- Chinese prose
- code symbols stay in original English
- cite real files and line numbers when useful
