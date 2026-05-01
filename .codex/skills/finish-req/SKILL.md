---
name: finish-req
description: Verify a requirement doc against the current code, fix small drift or defects, update implementation status, and archive it to notes/requirements/finished. Use when the user wants to close out an active requirement.
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
10. Update the requirement's implementation-status section with what was verified, tested, and renumbered or suffixed.
11. Move the file to `notes/requirements/finished/` only after all checks pass and numbering is consistent.

## Final Report

Report:

- requirement id
- verification outcome
- fixes applied
- simplifications applied
- splitting or numbering adjustment
- tests run
- archive path
