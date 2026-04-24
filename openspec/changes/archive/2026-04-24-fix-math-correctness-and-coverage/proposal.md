## Why

Core math layer still has two correctness hazards called out in `notes/temporary/claude-review.md`: quaternion composition uses the wrong scalar term, and floating-point vector hashing reads object bytes through undefined behavior. Both bugs can silently corrupt transforms or make future sanitizer runs noisy.  

This change isolates math correctness work from renderer/back-end changes and adds the first focused math coverage so these regressions stop recurring.

## What Changes

- Fix quaternion multiply paths to use the pre-mutation scalar term consistently.
- Replace floating-point vector hashing with `std::bit_cast`-based byte-stable hashing.
- Add focused math tests for quaternion composition, conjugation, vector rotation, and float-hash stability.
- Define sanitizer-friendly expectations for core math behavior.

## Capabilities

### New Capabilities
- `math-correctness`: Correctness contract for quaternion composition, floating-point hashing, and math regression coverage.

### Modified Capabilities

## Impact

- Affected code: `src/core/math/quat.hpp`, `src/core/math/vec.hpp`, `src/test/`
- Affected APIs: none expected
- Affected systems: math correctness, sanitizer readiness, test coverage
