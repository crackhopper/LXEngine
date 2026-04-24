## Context

The current cache key is convenient but fragile: it depends on address uniqueness and immediate garbage collection. A better contract needs to survive temporary inactivity and avoid false matches after destruction/reallocation.

## Goals / Non-Goals

**Goals:**
- Introduce a stable identity model for CPU-side resources mirrored into backend caches.
- Decouple cache correctness from address reuse.
- Leave room for less aggressive cache eviction later.

**Non-Goals:**
- No full resource streaming system.
- No backend-agnostic cache rewrite beyond the identity contract.

## Decisions

- Treat stable identity as an explicit contract instead of an incidental pointer value.
- Keep the first implementation simple enough to layer onto the current resource manager.
- Consider GC behavior as part of the contract, not a separate accidental side effect.

## Risks / Trade-offs

- [Identity changes may touch core resource interfaces] → Keep the contract narrow and well documented.
- [Longer-lived GPU resources may increase memory retention] → Pair identity changes with explicit GC policy expectations.
