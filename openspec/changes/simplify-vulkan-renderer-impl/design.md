## Context

The review highlights a design smell rather than a single bug: two renderer inheritance layers, public implementation members, and duplicated constants. The cleanup should preserve behavior while producing a simpler internal shape.

## Goals / Non-Goals

**Goals:**
- Make `VulkanRendererImpl` a pure implementation detail.
- Reduce direct public member exposure.
- Consolidate duplicated configuration/state definitions.

**Non-Goals:**
- No renderer backend rewrite.
- No forced API widening unless a callback placement change is clearly justified.

## Decisions

- Prefer one authoritative renderer-facing class boundary.
- Treat internal members as private implementation state.
- Move shared constants to one source of truth.

## Risks / Trade-offs

- [Internal refactor may touch many call sites] → Keep public behavior stable and scope changes to encapsulation.
- [Callback relocation may widen base interfaces] → Only promote callbacks if the abstraction genuinely benefits.
