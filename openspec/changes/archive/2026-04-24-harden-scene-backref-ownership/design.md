## Context

The scene already uses shared ownership at the top level, so a raw back-reference stands out as a design inconsistency. The change needs to harden lifecycle semantics without making common node operations awkward.

## Goals / Non-Goals

**Goals:**
- Remove raw back-reference ownership ambiguity.
- Define safe attach/detach/destruction behavior.
- Preserve straightforward scene-node usage.

**Non-Goals:**
- No transform hierarchy in this change.
- No full scene ownership redesign beyond the back-reference contract.

## Decisions

- Use an ownership-safe back-reference mechanism consistent with current scene lifetime assumptions.
- Define node detachment/destruction semantics explicitly rather than relying on incidental ordering.
- Keep current user-facing scene-node ergonomics where possible.

## Risks / Trade-offs

- [Changing back-reference semantics may touch many scene helpers] → Keep the contract narrow and focused on lifecycle safety.
- [Weak-reference style can require null checking] → Acceptable if it removes raw dangling-pointer risk.
