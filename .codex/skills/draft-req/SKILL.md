---
name: draft-req
description: Turn an idea into a formal requirement doc under notes/requirements/ through interactive discovery. Use when the user wants to draft a new requirement without implementing code yet.
---

Draft a new requirement document under `notes/requirements/`. This skill produces documentation only.

## Core Rules

- Do not implement code.
- Do not modify existing requirement docs unless the user explicitly asks.
- Use interactive discovery when the request is underspecified.
- Align filename, numbering, and structure with the existing requirement library.

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
8. Compute the next requirement number and confirm title + filename with the user.
9. Draft the final requirement doc in the existing project style.
10. Show the draft before saving.

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
