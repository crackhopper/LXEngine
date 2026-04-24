## Purpose

Define the current scene-node validation contract for high-level renderables and their structural pass validation behavior.
## Requirements
### Requirement: SceneNode is a self-validating high-level renderable
The system SHALL provide a high-level `SceneNode` type as the primary `IRenderable` implementation. `SceneNode` construction SHALL require `nodeName`, `MeshSharedPtr mesh`, and `MaterialInstanceSharedPtr materialInstance`; `SkeletonSharedPtr` SHALL be optional; and `objectPC` SHALL remain present as a transitional member for the engine-wide model push constant. `SceneNode` SHALL be valid outside of any `Scene` container and SHALL perform structural validation immediately during construction.

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

### Requirement: SceneNode validates forward-shader resource requirements from variants
For any enabled pass backed by the `blinnphong_0` forward shader family, `SceneNode` SHALL validate mesh and node resources against the active shader variant set before the node is considered structurally valid.

At minimum, the validation rules SHALL be:

- `USE_VERTEX_COLOR => mesh` provides `inColor`
- `USE_UV => mesh` provides `inUV`
- `USE_LIGHTING => mesh` provides `inNormal`
- `USE_NORMAL_MAP => mesh` provides `inTangent` and `inUV`
- `USE_SKINNING => mesh` provides `inBoneIDs` and `inBoneWeights`, and the node provides `Skeleton/Bones`

Additionally, `SceneNode` SHALL validate descriptor resource ownership using the `isSystemOwnedBinding()` query from `shader-binding-ownership`. The validation logic MUST NOT use hardcoded binding name comparisons (such as checking for `"MaterialUBO"`, `"CameraUBO"`, or `"LightUBO"` by literal string). Instead:

- For each binding in the shader reflection:
  - If `isSystemOwnedBinding(binding.name)` returns `true` and the binding is `Bones`: validate that the node provides a `Skeleton` with a `Bones` UBO resource.
  - If `isSystemOwnedBinding(binding.name)` returns `true` and the binding is not `Bones`: skip (scene provides these resources, not the renderable).
  - If `isSystemOwnedBinding(binding.name)` returns `false`: validate that the material's descriptor resources contain a resource with a matching `getBindingName()`.

Reserved binding name misuse (type mismatch against the system contract) SHALL be validated as specified by `shader-binding-ownership` and SHALL be treated as `FATAL + terminate`.

Any mismatch between the enabled variant set and the available mesh/skeleton/material resources MUST be treated as a structural validation failure and handled as `FATAL + terminate`.

#### Scenario: Missing vertex color attribute terminates
- **WHEN** a pass enables `USE_VERTEX_COLOR` and the mesh vertex layout does not provide `inColor`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing UV for textured forward pass terminates
- **WHEN** a pass enables `USE_UV` and the mesh vertex layout does not provide `inUV`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing tangent for normal-mapped pass terminates
- **WHEN** a pass enables `USE_NORMAL_MAP` and the mesh vertex layout does not provide `inTangent`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing skeleton resources for skinned pass terminates
- **WHEN** a pass enables `USE_SKINNING` and the node lacks a `Skeleton` or `Bones` resource
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Non-reserved material binding validated by name
- **WHEN** a shader declares a non-reserved `UniformBuffer` binding named `SurfaceParams` and the material's descriptor resources include a resource with `getBindingName() == StringID("SurfaceParams")`
- **THEN** the validation passes for that binding

#### Scenario: Missing non-reserved material binding terminates
- **WHEN** a shader declares a non-reserved `UniformBuffer` binding named `SurfaceParams` and the material's descriptor resources do not include a resource with that binding name
- **THEN** `SceneNode` logs a `FATAL` structural validation failure and terminates immediately

#### Scenario: Reserved name type mismatch terminates
- **WHEN** a shader declares `CameraUBO` as `Texture2D` instead of `UniformBuffer`
- **THEN** the system logs a `FATAL` error with type mismatch details and terminates immediately

### Requirement: SceneNode validates only currently enabled material passes
`SceneNode` structural validation SHALL cover every pass that is both:

- defined by the current `MaterialTemplate`
- currently enabled on the bound `MaterialInstance`

Passes that are defined on the template but disabled on the instance SHALL NOT participate in the node's structural validity requirements for that moment.

If any enabled pass fails validation, the system MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: Disabled shadow pass no longer blocks a valid forward node
- **WHEN** a template defines both `Pass_Forward` and `Pass_Shadow`, the instance disables `Pass_Shadow`, and the node is structurally valid only for `Pass_Forward`
- **THEN** the node remains valid and `Pass_Shadow` does not invalidate it while disabled

### Requirement: Material pass-state changes are structural revalidation events
Changes to a `MaterialInstance` pass enable/disable state SHALL be treated as structural changes for every `SceneNode` that references that instance. After such a change, every affected node MUST be revalidated against the new enabled-pass set before further pass participation queries are trusted.

Ordinary material parameter updates MUST NOT trigger this structural revalidation path.

#### Scenario: Enabling a previously disabled pass forces revalidation
- **WHEN** a node references a shared `MaterialInstance` and that instance enables `Pass_Shadow`
- **THEN** the node is revalidated for `Pass_Shadow` before `supportsPass(Pass_Shadow)` may return `true`

#### Scenario: Non-structural material write does not revalidate nodes
- **WHEN** `setTexture(...)` or `setFloat(...)` is called on a shared `MaterialInstance`
- **THEN** the affected nodes are not structurally revalidated solely because of that parameter write

### Requirement: Scene propagates shared MaterialInstance pass-state changes
`Scene` or an equivalent scene-level owner SHALL be responsible for propagating `MaterialInstance` pass-state changes to every `SceneNode` that references that instance. The material instance itself SHALL NOT own reverse references to nodes.

The scene-level API MAY use scanning or an index internally, but the externally observable behavior MUST be equivalent to a semantic operation such as `revalidateNodesUsing(materialInstance)`.

If any affected node fails validation under the new enabled-pass set, the system MUST emit a `FATAL` log and terminate immediately.

#### Scenario: Shared instance pass disable affects all referencing nodes
- **WHEN** two `SceneNode` objects in the same scene share one `MaterialInstance` and that instance disables `Pass_Shadow`
- **THEN** the scene propagates the change to both nodes and both nodes stop reporting support for `Pass_Shadow`

