## Context

The project now has a meaningful demo entrypoint in `scene_viewer`, while the root executable remains intentionally thin. That is acceptable only if the thin executable has a documented purpose rather than acting like a leftover product entry.

## Goals / Non-Goals

**Goals:**
- Give the root executable one clear role.
- Remove misleading dead helper code near renderer bootstrap/debug utilities.
- Keep documentation and build targets aligned with the chosen role.

**Non-Goals:**
- No large demo redesign.
- No change to `scene_viewer` feature scope in this change.

## Decisions

- Treat bootstrap/entry semantics as a documented contract, not accidental behavior.
- Consolidate environment-flag reading into one helper path where duplicates exist.
- Remove unused helpers if they have no consumer after the entry contract is clarified.

## Risks / Trade-offs

- [Users may expect the root executable to be the main demo] → Solve explicitly in docs/build naming instead of leaving ambiguity.
- [Some helper cleanup may appear cosmetic] → Keep only cleanup that supports the new entry contract.
