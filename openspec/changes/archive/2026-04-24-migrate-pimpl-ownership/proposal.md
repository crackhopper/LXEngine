## Why

Several infra and backend classes still violate the project's own ownership rules by storing pImpl pointers as raw owning pointers and managing them with manual `new/delete`. The problem is already called out in the review and directly conflicts with the C++ style guide.

This change makes pImpl ownership explicit, consistent, and RAII-safe across the remaining offenders.

## What Changes

- Replace raw owning pImpl pointers with `std::unique_ptr` in remaining infra/backend classes.
- Remove manual `new/delete` pairs for those implementations.
- Document the expected pImpl ownership pattern so future subsystems do not regress.

## Capabilities

### New Capabilities
- `pimpl-ownership`: RAII ownership contract for implementation-pointer patterns used in infra and backend code.

### Modified Capabilities

## Impact

- Affected code: `src/backend/vulkan/`, `src/infra/window/`, `src/infra/gui/`, loader implementations
- Affected APIs: construction/destruction internals only
- Affected systems: ownership safety, exception safety, style-guide conformance
