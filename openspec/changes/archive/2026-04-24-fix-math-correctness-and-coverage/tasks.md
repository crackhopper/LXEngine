## 1. Math Fixes

- [x] 1.1 Fix quaternion `multiply_inplace` and `left_multiply_inplace` to use the original scalar term during vector composition.
- [x] 1.2 Replace floating-point vector hashing with `std::bit_cast`-based hashing for `float` and `double`.

## 2. Regression Coverage

- [x] 2.1 Add a math-focused test executable covering quaternion composition, conjugation, and vector rotation invariants.
- [x] 2.2 Add hash-focused checks that run cleanly under sanitizer-oriented builds.

## 3. Verification

- [x] 3.1 Build the new math test and existing affected tests.
- [x] 3.2 Run the focused math test suite and confirm no regression in dependent renderer tests.
