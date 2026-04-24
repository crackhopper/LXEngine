## Why

Several core and infra paths currently use `std::terminate()` for programmer errors such as duplicate names or undefined pass access. That makes failure semantics harder to test and turns recoverable validation failures into process-abort-only behavior.

This change standardizes fail-fast programmer-error handling around explicit logic exceptions while leaving truly fatal runtime failures alone.

## What Changes

- Replace programmer-error `std::terminate()` sites with a consistent logic-error path.
- Keep unrecoverable backend/runtime failures outside this change.
- Define which contracts are programmer errors and therefore testable through exception-based assertions.

## Capabilities

### New Capabilities
- `programmer-error-contract`: Fail-fast programmer-error handling based on explicit logic errors for testable validation paths.

### Modified Capabilities

## Impact

- Affected code: `src/core/scene/`, `src/core/asset/material_instance.cpp`, `src/infra/material_loader/`
- Affected APIs: failure mode for invalid caller input / invalid setup
- Affected systems: tests, validation semantics, debugging
