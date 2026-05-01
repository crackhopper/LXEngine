---
name: draft-req
description: Turn an idea into a formal requirement doc under notes/requirements/ through interactive discovery. Use when the user wants to draft a new requirement without implementing code yet.
---

Draft a new requirement document under `notes/requirements/`. This skill produces documentation only.

## Core Rules

- Do not implement code.
- Do not modify existing requirement docs unless implementation-order numbering requires a user-confirmed split / numbering plan.
- Use interactive discovery when the request is underspecified.
- Align filename, numbering, and structure with the existing requirement library.
- Requirement numbers represent implementation order, not creation order.
- One requirement file represents one continuous implementation cycle.
- Keep `notes/requirements/*.md` as the implementation queue: if an old active requirement spans work that should happen before and after the new requirement, split the old requirement first.
- Prefer stable suffix families for split or inserted work within the same implementation slot: split `020-foo.md` into `020-a-foo.md`, `020-b-bar.md`, etc. instead of shifting every later active requirement.
- When splitting an active requirement, leave a short trace note in both resulting docs explaining the source REQ, retained scope, moved scope, and split date.

## Workflow

1. Scan:
   - `notes/requirements/*.md`
   - `notes/requirements/finished/*.md`
2. If the user gave no brief, ask for the topic first.
3. Discuss:
   - current pain
   - why now
   - failure mode if nothing changes
4. Validate the current state against the codebase before writing claims.
5. Ask for:
   - success criteria
   - invariants
   - API impact
6. Propose an `R1..Rn` breakdown and refine it with the user.
7. Check boundaries, dependencies, downstream work, and conflicts with active or finished requirements.
8. Determine the new requirement's implementation-order slot.
9. If the new requirement inserts before existing active work:
   - split any active requirement whose `R1..Rn` now spans multiple implementation phases
   - convert the affected base requirement into a suffix family when the insertion belongs to that same slot, e.g. `020-a`, `020-b`, `020-c`
   - keep unrelated later numeric requirements unchanged whenever the suffix family preserves implementation order
   - renumber later active requirements only when the order change is global and cannot be represented as a local suffix split
   - update `REQ-NNN` / `REQ-NNN-a` references in active requirements, roadmaps, and open OpenSpec changes
10. Confirm title, filename, and the full split / numbering plan with the user.
11. Draft the final requirement doc in the existing project style.
12. Show the draft before saving.

## Required Output Shape

The requirement doc should include:

- title with REQ id
- background
- goals
- requirements / `R1..Rn`
- tests
- modification scope
- boundaries and constraints
- dependencies
- downstream work
- implementation status
