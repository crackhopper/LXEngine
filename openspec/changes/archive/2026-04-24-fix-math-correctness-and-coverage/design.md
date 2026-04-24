## Context

The engine currently has no dedicated math test executable, and the two known defects live in header-only code that is reused across scene, camera, and renderer paths. The safest fix is a small change set with direct property-style tests.

## Goals / Non-Goals

**Goals:**
- Fix quaternion multiplication and left-multiplication semantics.
- Remove UB from floating-point vector hashing.
- Add repeatable regression coverage for the affected math paths.

**Non-Goals:**
- No large math-library redesign.
- No SIMD optimization work.
- No broad rewrite of matrix/vector APIs beyond touched hazards.

## Decisions

- Fix both quaternion mutation helpers, because `operator*` and `operator*=` depend on them.
- Use `std::bit_cast` for floating-point hash input, because it preserves exact bit identity without aliasing violations.
- Add a dedicated math-focused test executable instead of burying checks inside renderer integration tests.

## Risks / Trade-offs

- [Header-only math changes can affect many callers] → Keep scope to the known incorrect formulas and add regression tests.
- [Hash behavior changes may alter container ordering] → Acceptable; stable defined behavior is preferable to UB.
