## Context

The codebase already prefers RAII and smart-pointer ownership, but pImpl-heavy classes predate that cleanup. The remaining violations are mechanical but cross-cutting, so they need one consistent rule instead of ad hoc local fixes.

## Goals / Non-Goals

**Goals:**
- Eliminate raw owning pImpl pointers from the identified infra/backend classes.
- Preserve current public APIs while changing only ownership internals.
- Capture one repeatable pattern for forward declarations and out-of-line destructors.

**Non-Goals:**
- No redesign of class responsibilities.
- No wholesale smart-pointer migration beyond owning pImpl fields.

## Decisions

- Use `std::unique_ptr<Impl>` for all owning pImpl fields.
- Keep destructors out-of-line where incomplete-type rules require it.
- Batch related pImpl sites under one change because the technical pattern is identical.

## Risks / Trade-offs

- [Many files touched mechanically] → Keep behavioral changes out of scope and verify with targeted builds/tests.
- [Incomplete-type compilation pitfalls] → Define destructors in `.cpp` files where needed.
