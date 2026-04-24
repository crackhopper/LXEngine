## Context

The current scene is intentionally flat, which simplified early rendering work but now blocks natural object composition. A first hierarchy needs to remain compatible with the existing renderable validation/cache flow and avoid overreaching into a full ECS redesign.

## Goals / Non-Goals

**Goals:**
- Introduce parent-child transform relationships.
- Define local-to-world transform propagation.
- Keep renderable world-transform consumption compatible with the current renderer path.

**Non-Goals:**
- No full gameplay entity system.
- No spatial partitioning or culling redesign in the same change.

## Decisions

- Add hierarchy semantics at the scene-object level rather than bolting them onto renderer-only data.
- Distinguish local transform from derived world transform explicitly.
- Keep the first version focused on correctness and predictable update flow.

## Risks / Trade-offs

- [Hierarchy adds invalidation complexity] → Tie transform propagation to explicit update/revalidation rules.
- [Flat-scene assumptions exist in current code/tests] → Update affected scene assembly paths deliberately instead of partially emulating hierarchy.
