---
name: finish-req
description: Verify a requirement doc against the current code, fix small drift or defects, update implementation status, and archive it to notes/requirements/finished. Use when the user wants to close out an active requirement.
---

Finish a requirement by verifying it against the current code and archiving it only after verification succeeds.

## Core Rules

- Never archive without verification.
- Never archive with unresolved drift or missing implementation unless the user explicitly changes scope.
- Prefer code as truth when the doc is stale and the implementation is clearly correct.
- Keep fixes narrow; stop and ask before large rewrites.

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
9. Update the requirement's implementation-status section with what was verified and tested.
10. Move the file to `notes/requirements/finished/` only after all checks pass.

## Final Report

Report:

- requirement id
- verification outcome
- fixes applied
- simplifications applied
- tests run
- archive path
