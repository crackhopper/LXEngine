## ADDED Requirements

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
