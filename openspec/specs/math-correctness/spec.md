## Purpose

Define targeted correctness requirements for low-level math utilities that must remain stable under regression testing.

## Requirements

### Requirement: Quaternion composition uses pre-mutation operands
Quaternion in-place multiplication SHALL compute both scalar and vector outputs from the original left-hand-side operands captured before mutation.

#### Scenario: multiply_inplace preserves correct scalar usage
- **WHEN** `multiply_inplace` composes two quaternions
- **THEN** the vector term uses the pre-mutation scalar value rather than the already-updated scalar field

### Requirement: Floating-point vector hashing is defined behavior
Floating-point vector hashing SHALL derive hash input from bit-stable value copies and MUST NOT read object bytes through aliasing-violating or width-mismatched casts.

#### Scenario: float hashing avoids undefined reads
- **WHEN** a vector of `float` values is hashed
- **THEN** the implementation uses value-preserving bit casts rather than dereferencing a mismatched integer pointer

### Requirement: Math regressions are covered by focused tests
The project SHALL provide focused math regression coverage for quaternion composition and floating-point vector hashing.

#### Scenario: quaternion and hash regressions are testable
- **WHEN** focused math tests run
- **THEN** they verify quaternion composition properties and floating-point hash behavior independently of renderer integration tests

### Requirement: Transform matrix decomposition is regression-tested
The project SHALL provide focused math regression coverage for `Transform::toMat4()` and `Transform::fromMat4(...)`, including strict TRS round-trip behavior and approximation behavior for non-strict TRS matrices.

#### Scenario: transform identity and translation-only cases are testable
- **WHEN** focused math tests run
- **THEN** they verify identity conversion and translation-only matrix decomposition independently of renderer integration tests

#### Scenario: strict TRS round-trip is stable
- **WHEN** `Transform::fromMat4(t.toMat4())` is evaluated for a transform without shear or unsupported reflection ambiguity
- **THEN** the resulting translation, rotation, and scale match the original transform within numeric tolerance

### Requirement: Non-strict TRS decomposition emits an observable warning
If `Transform::fromMat4(...)` receives a matrix whose upper 3x3 basis is not a strict reversible TRS representation for the engine's chosen `Transform` model, the implementation SHALL emit a `WARN` log indicating that the returned `Transform` is an approximation.

Inputs that MUST trigger this warning include at least:

- matrices containing shear
- matrices requiring negative-scale sign normalization or reflection repair
- singular or near-singular bases that cannot be stably decomposed into the engine's `Transform` representation

#### Scenario: shear input emits warning
- **WHEN** `Transform::fromMat4(...)` is called with a sheared matrix
- **THEN** the implementation emits a `WARN` log before returning the approximate transform

#### Scenario: negative-scale repair emits warning
- **WHEN** `Transform::fromMat4(...)` is called with a matrix that requires negative-scale sign normalization
- **THEN** the implementation emits a `WARN` log before returning the repaired transform
