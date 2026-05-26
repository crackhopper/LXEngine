---
name: draft-req
description: Turn an idea into a formal requirement doc under notes/requirements/ through interactive discovery. Use when the user wants to draft a new requirement without implementing code yet.
---

Draft requirement documents under `notes/requirements/`. This skill produces
documentation only and stays focused on the requirement queue itself.

## Core Rules

- Do not implement code.
- Do not modify existing requirement docs unless implementation-order numbering requires a user-confirmed split / numbering plan.
- Use interactive discovery when the request is underspecified.
- Align filename, numbering, and structure with the existing requirement library.
- Requirement numbers represent implementation order, not creation order.
- One requirement file represents one continuous implementation cycle.
- New top-level requirement slots default to a suffix family that starts at
  `NNN-a`, even when there is only one requirement in that slot. For example,
  after `045-c`, the next independent mainline slots should be named
  `046-a-...`, `047-a-...`, `048-a-...`, rather than bare `046-...`.
- Use later suffixes (`NNN-b`, `NNN-c`, etc.) only when adding another
  requirement to the same implementation slot, such as a follow-up, split, or
  insertion that should stay anchored to the same base number.
- Keep `notes/requirements/*.md` as the implementation queue: if an old active requirement spans work that should happen before and after the new requirement, split the old requirement first.
- Prefer stable suffix families for split or inserted work within the same implementation slot: split or extend `020-a-foo.md` with `020-b-bar.md`, `020-c-baz.md`, etc. instead of shifting every later active requirement.
- When splitting an active requirement, leave a short trace note in both resulting docs explaining the source REQ, retained scope, moved scope, and split date.
- Keep dependency references limited to current code, active/finished
  requirements, notes, assets, and project specs that are directly relevant to
  the requirement. Do not depend on historical change archives or unrelated
  planning systems.

## Workflow

1. Scan:
   - `notes/requirements/*.md`
   - `notes/requirements/finished/*.md`
   - any immediately relevant `src/`, `assets/`, or `notes/` files needed to
     make accurate current-state claims
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
   - For a new independent mainline slot, choose the next base number and use
     the `-a` suffix by default, e.g. `046-a-topic.md`.
   - For a follow-up, split, or inserted requirement that belongs to an
     existing base number, choose the next suffix in that family, e.g.
     `046-b-topic.md`.
   - Avoid bare `NNN-topic.md` filenames for new requirements; reserve the
     numeric base as the implementation-order anchor and express concrete docs
     through suffixes.
9. If the new requirement inserts before existing active work:
   - split any active requirement whose `R1..Rn` now spans multiple implementation phases
   - extend the affected suffix family when the insertion belongs to that same slot, e.g. `020-a`, `020-b`, `020-c`
   - keep unrelated later numeric requirements unchanged whenever the suffix family preserves implementation order
   - renumber later active requirements only when the order change is global and cannot be represented as a local suffix split
   - update `REQ-NNN-a` / `REQ-NNN-b` references in active requirements,
     roadmaps, notes, and other current requirement documents
10. Confirm title, filename, and the full split / numbering plan with the user.
11. Draft the requirement document in the existing project style.
12. Show the draft before saving unless the user has already approved the
    complete structure and asked to write the files directly.
13. Save the approved document under `notes/requirements/`.
14. Self-review the saved document for:
    - missing current-state evidence
    - ambiguous requirements
    - dependencies that are not real prerequisites
    - scope that spans more than one continuous implementation cycle

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
