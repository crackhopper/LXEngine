---
name: finish-req
description: Verify a requirement doc against the current code, fix small drift or defects, update implementation status, archive it to notes/requirements/finished, and hide associated Superpowers specs/plans from the active docs/superpowers/specs and docs/superpowers/plans surfaces. Use when the user wants to close out an active requirement.
---

Finish a requirement by verifying it against the current code and archiving it only after verification succeeds.

## Core Rules

- Never archive without verification.
- Never archive with unresolved drift or missing implementation unless the user explicitly changes scope.
- Never archive a requirement whose number does not match actual implementation order.
- Prefer code as truth when the doc is stale and the implementation is clearly correct.
- Keep fixes narrow; stop and ask before large rewrites.
- Requirement numbers represent implementation order. If another active requirement now spans work before and after the target, split that active requirement before numbering changes.
- One active requirement file represents one continuous implementation cycle.
- Prefer stable suffix families for split work in one implementation slot: split `020-foo.md` into `020-a-foo.md`, `020-b-bar.md`, etc. instead of shifting every later active requirement.
- When splitting an active requirement, add a short trace note to both resulting docs.
- Treat requirement archival as a bundle: archive the requirement file and hide its associated Superpowers specs/plans in the same closeout unless the user asks for a requirement-only move.
- Hide historical Superpowers files by moving them out of active directories, not deleting them:
  - `docs/superpowers/specs/` -> `docs/superpowers/finished/specs/`
  - `docs/superpowers/plans/` -> `docs/superpowers/finished/plans/`
- If the repository already has a different finished/archive directory for Superpowers files, use the existing convention instead of creating a second one.
- Never hide unrelated future specs/plans. When association is uncertain, list the candidate and ask or leave it active.

## Workflow

1. Resolve the target file under `notes/requirements/`.
2. Read the full requirement and extract:
   - requirement id
   - goals
   - concrete `R1..Rn`
   - modification scope
   - tests
   - dependencies
   - implementation status
3. Check upstream dependencies. Stop if required upstream work is unfinished unless the user waives it.
4. Verify every `R1..Rn` against the codebase and classify:
   - implemented
   - drift
   - missing
   - superseded
5. Show the verification table to the user before changing anything.
6. Look for small simplifications in the touched code.
7. Fix accepted drift or missing pieces, keeping the scope tight.
8. Run relevant builds or tests before archiving.
9. Check implementation-order numbering before archiving:
   - compare the target with finished history and active requirements
   - split lower-numbered active requirements if only part of their work must move after the target
   - use `NNN-a`, `NNN-b`, ... suffixes for local split families when that preserves later active numbers
   - renumber later active requirements only for true global order changes
   - update titles, filenames, and `REQ-NNN` / `REQ-NNN-a` references after user confirmation
10. Identify associated Superpowers specs/plans before moving files:
   - search `docs/superpowers/specs/` and `docs/superpowers/plans/`
   - match by requirement id tokens such as `073-d`, `REQ-073-d`, and stable title keywords
   - include files explicitly referenced by the requirement
   - include older precursor design/plan files only when their title and content clearly map to the same requirement
11. Show the associated specs/plans in the verification table before changing anything.
12. Update the requirement's implementation-status section with what was verified, tested, and renumbered or suffixed.
13. Move the file to `notes/requirements/finished/` only after all checks pass and numbering is consistent.
14. Move associated Superpowers specs/plans to `docs/superpowers/finished/specs/` and `docs/superpowers/finished/plans/` after the requirement is archived.
15. Run `rg` checks for stale active-path references to the moved requirement/spec/plan files, then update only active indexes or docs that would otherwise point at missing active files.

## Final Report

Report:

- requirement id
- verification outcome
- fixes applied
- simplifications applied
- splitting or numbering adjustment
- tests run
- archive path
- hidden Superpowers specs/plans paths
