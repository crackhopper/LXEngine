## ADDED Requirements

### Requirement: Mesh exposes getPipelineHash

`Mesh` SHALL provide `size_t getPipelineHash() const` that incorporates vertex layout and index topology information used for pipeline identity. It MAY delegate to the existing `getLayoutHash()` implementation. The method `getLayoutHash()` SHALL remain available.

#### Scenario: Pipeline hash matches layout hash composition

- **WHEN** `getLayoutHash()` would change for a given mesh configuration
- **THEN** `getPipelineHash()` SHALL change accordingly (same effective factor for pipeline identity)

### Requirement: RenderState exposes getPipelineHash

`RenderState` SHALL provide `size_t getPipelineHash() const` with semantics identical to `getHash()` for render-state factors. The method `getHash()` SHALL remain available.

#### Scenario: Equivalent state yields equal pipeline hash

- **WHEN** two `RenderState` values compare equal for hashing purposes
- **THEN** their `getPipelineHash()` values SHALL be equal

### Requirement: ShaderProgramSet exposes getPipelineHash

`ShaderProgramSet` SHALL provide `size_t getPipelineHash() const` with semantics identical to `getHash()` for shader set identity. The method `getHash()` SHALL remain available.

#### Scenario: Equivalent shader sets yield equal pipeline hash

- **WHEN** two `ShaderProgramSet` values have the same `getHash()`
- **THEN** their `getPipelineHash()` values SHALL be equal

### Requirement: Skeleton exposes getPipelineHash for skinning

`Skeleton` SHALL implement `size_t getPipelineHash() const` as specified in the skeleton-resource capability (boolean skinning / pipeline factor for REQ-001).

#### Scenario: Consistent naming at call sites

- **WHEN** code assembles pipeline identity from resources
- **THEN** it SHALL use `getPipelineHash()` on each participating resource type rather than ad hoc method names

### Requirement: Future PipelineKey assembly uses getPipelineHash

When `PipelineKey::build()` (or equivalent) is implemented, it SHALL compose pipeline identity using `getPipelineHash()` from each participating resource (`Mesh`, `RenderState`, `ShaderProgramSet`, `Skeleton`, and any additional types added by future requirements) rather than calling `getLayoutHash()` or raw `getHash()` on those types at the key boundary.

#### Scenario: Key builder uses unified accessors

- **WHEN** `PipelineKey` is introduced or extended per REQ-002
- **THEN** its implementation SHALL call `getPipelineHash()` on resources that participate in the key
