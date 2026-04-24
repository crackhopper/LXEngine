## Context

The material system recently converged on canonical parameter ownership, but the repository still lacks a second concrete example that proves the current asset path is not specialized to one legacy material. The example needs to stay small and align with the now-canonical contract.

## Goals / Non-Goals

**Goals:**
- Add one real custom material example through the current loader path.
- Keep the example aligned with current reflection/material contracts.
- Use the example in docs or sample consumption.

**Non-Goals:**
- No large material library expansion.
- No attempt to solve every remaining material-asset feature gap in the same change.

## Decisions

- Use one intentionally small but real material asset rather than many incomplete variants.
- Route the example through the formal loader path so it exercises the current contract instead of bypassing it.
- Update docs to point at the concrete asset/example by name.

## Risks / Trade-offs

- [Example may become stale as material contracts evolve] → Tie the example to current canonical material rules and keep it in tests/docs.
- [Temptation to treat example as a full asset-format redesign] → Keep this scoped to one validating example.
