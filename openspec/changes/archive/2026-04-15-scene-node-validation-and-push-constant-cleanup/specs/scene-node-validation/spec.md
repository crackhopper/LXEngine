## ADDED Requirements

### Requirement: SceneNode is a self-validating high-level renderable
The system SHALL provide a high-level `SceneNode` type as the primary `IRenderable` implementation. `SceneNode` construction SHALL require `nodeName`, `MeshSharedPtr mesh`, and `MaterialPtr materialInstance`; `SkeletonSharedPtr` SHALL be optional; and `objectPC` SHALL remain present as a transitional member for the engine-wide model push constant. `SceneNode` SHALL be valid outside of any `Scene` container and SHALL perform structural validation immediately during construction.

Structural setter operations (`setMesh(...)`, `setMaterialInstance(...)`, `setSkeleton(...)`, and `setSkeleton(nullptr)`) SHALL trigger immediate re-validation of every currently enabled pass before the setter call completes.

#### Scenario: Constructor validates an independent node
- **WHEN** a `SceneNode` is constructed with a valid mesh, material instance, and node name while not attached to any `Scene`
- **THEN** the node completes structural validation successfully and exposes pass support based on its validated cache

#### Scenario: Structural setter revalidates before returning
- **WHEN** `setMaterialInstance(...)` or `setMesh(...)` is called on an existing `SceneNode`
- **THEN** the node re-runs structural validation for all enabled passes and refreshes its pass cache before the call returns

### Requirement: SceneNode maintains pass-level validated-entry cache
`SceneNode` SHALL maintain a lightweight `pass -> validated entry` structural cache. Each validated entry MUST represent the most recent successful structural validation result for that pass and MUST retain the stable structural information required to build a `RenderingItem`, including at minimum the pass-qualified object render signature, the material/pass structural result, the structural descriptor-resource conclusion, and the stable resource handles for mesh/material/object push constant consumption.

The cache SHALL be invalidated and immediately rebuilt on:
- `SceneNode` construction
- `setMesh(...)`
- `setMaterialInstance(...)`
- `setSkeleton(...)`
- `setSkeleton(nullptr)`
- any `MaterialInstance` pass-enable state change propagated to the node

The cache SHALL NOT be invalidated by non-structural runtime updates such as `setFloat`, `setInt`, `setVec*`, `setTexture`, `updateUBO`, or `objectPC`/model updates.

#### Scenario: supportsPass answers from cache
- **WHEN** `supportsPass(Pass_Forward)` is called on a node whose forward pass is enabled and has a validated entry
- **THEN** the method returns `true` without recomputing structural validation

#### Scenario: Unknown or disabled pass returns false
- **WHEN** `supportsPass(Pass_Shadow)` is called for a pass that is absent from the material or currently disabled on the instance
- **THEN** the method returns `false` and does not treat the query itself as an error

### Requirement: Scene enforces explicit names and node uniqueness
`Scene` SHALL require `sceneName` at construction time. `SceneNode` SHALL require `nodeName` at construction time. A `Scene` SHALL enforce uniqueness of `nodeName` within that scene namespace when nodes are added or renamed. `SceneNode` existence and legality MUST NOT depend on being attached to a `Scene`; scene membership only adds namespace-level uniqueness enforcement.

#### Scenario: Scene requires a name
- **WHEN** code constructs a `Scene`
- **THEN** it MUST provide a `sceneName`, and the resulting scene exposes that name for debugging and diagnostics

#### Scenario: Duplicate node name is rejected
- **WHEN** a second `SceneNode` with the same `nodeName` is inserted into the same `Scene`
- **THEN** the operation is treated as a structural validation failure rather than silently replacing or renaming the existing node

### Requirement: Structural validation failures are fatal
Any `SceneNode` structural validation failure SHALL be treated as a programmer error. The system MUST emit a `FATAL` log and terminate the process immediately. The diagnostic payload MUST include the failing pass name, material or shader identity, enabled variants, mesh vertex-layout debug information, and the missing or incompatible structural contract that caused the failure.

Structural validation SHALL cover at minimum:
- shader variant selection consistency with the material pass configuration
- vertex shader input requirements versus mesh vertex layout
- shader descriptor requirements versus material- and skeleton-provided resources
- skinning variant requirements versus `Skeleton` / Bones UBO availability

#### Scenario: Missing skeleton for skinned material terminates
- **WHEN** a pass enables a skinning variant that requires `Bones` and the node has no `Skeleton`
- **THEN** the system logs a `FATAL` validation failure for that pass and terminates immediately

#### Scenario: Vertex layout mismatch terminates
- **WHEN** the reflected vertex-stage inputs required by an enabled pass are not fully provided by the mesh vertex layout
- **THEN** the system logs a `FATAL` validation failure including the pass and vertex-layout context, and terminates immediately
