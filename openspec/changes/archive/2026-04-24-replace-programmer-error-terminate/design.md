## Context

The current code mixes assertions, fatal runtime paths, and unconditional termination. The review specifically calls out programmer errors that should remain fail-fast but become testable and diagnosable.

## Goals / Non-Goals

**Goals:**
- Replace programmer-error termination sites with explicit logic exceptions.
- Preserve fail-fast semantics for invalid contracts.
- Make these paths verifiable in tests.

**Non-Goals:**
- No attempt to recover from invalid contracts.
- No changes to truly fatal Vulkan/device failure handling in this change.

## Decisions

- Use `std::logic_error` for programmer errors, because these sites represent violated invariants or invalid caller behavior.
- Keep the migration limited to review-identified call sites first.
- Prefer one helper pattern where repeated context formatting is needed.

## Risks / Trade-offs

- [Behavior changes from abort to throw] → Acceptable for programmer-error paths; tests can now assert behavior directly.
- [Some callers may rely on immediate process abort] → Keep scope to clearly invalid-contract sites, not backend fatal failures.
