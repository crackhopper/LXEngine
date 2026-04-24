## Purpose

Define the current pipeline-signature contract used to compose structured pass, object, material, and pipeline identity in core.

## Requirements

### Requirement: Pass system constants
The system SHALL provide a header `src/core/frame_graph/pass.hpp` exposing `StringID` constants `Pass_Forward`, `Pass_Deferred`, and `Pass_Shadow` in namespace `LX_core`. These constants MUST be declared `inline const` (not `constexpr`) because `StringID` construction interns the underlying name into `GlobalStringTable`.

#### Scenario: Pass constants are stable across translation units
- **WHEN** `Pass_Forward` is referenced from two different translation units in the same process
- **THEN** both references resolve to the same `StringID.id` value (the `"Forward"` leaf id)

#### Scenario: Distinct pass constants produce distinct ids
- **WHEN** `Pass_Forward`, `Pass_Deferred`, and `Pass_Shadow` are compared pairwise
- **THEN** all three `StringID.id` values MUST be distinct

### Requirement: Leaf resources provide parameterless getPipelineSignature
Every leaf resource participating in pipeline identity SHALL provide a `StringID getPipelineSignature() const` method that returns a structurally-interned `StringID` computed via `GlobalStringTable::compose(...)` or `GlobalStringTable::Intern(...)`. This applies to `VertexLayoutItem`, `VertexLayout`, `IVertexBuffer`, `IndexBuffer`, `RenderState`, `ShaderProgramSet`, and `MaterialPassDefinition`.

`PrimitiveTopology` SHALL be handled by a free function `StringID topologyPipelineSignature(PrimitiveTopology)` instead of a member, since enums cannot carry methods. `IndexBuffer::getPipelineSignature()` SHALL return `topologyPipelineSignature(getTopology())`.

#### Scenario: Same VertexLayout produces same signature
- **WHEN** two `VertexLayout` instances have identical items and stride
- **THEN** `getPipelineSignature()` returns the same `StringID` id on both

#### Scenario: Different layout item order produces different signature
- **WHEN** two `VertexLayout` instances share the same items but in different declaration order
- **THEN** `getPipelineSignature()` returns different `StringID` ids (compose is order sensitive)

#### Scenario: RenderState signature reflects all seven render-state fields
- **WHEN** any of `cullMode`, `depthTestEnable`, `depthWriteEnable`, `depthOp`, `blendEnable`, `srcBlend`, `dstBlend` changes
- **THEN** `RenderState::getPipelineSignature()` returns a different `StringID` id

#### Scenario: ShaderProgramSet signature ignores variant order
- **WHEN** two `ShaderProgramSet` instances enable the same set of variant macros in different insertion order
- **THEN** `getPipelineSignature()` returns the same id (variants MUST be sorted before compose)

#### Scenario: Topology signature distinguishes primitive types
- **WHEN** `topologyPipelineSignature(PrimitiveTopology::TriangleList)` and `topologyPipelineSignature(PrimitiveTopology::LineList)` are compared
- **THEN** the returned `StringID` ids are different

### Requirement: Mesh provides pass-aware getPipelineSignature
`Mesh::getPipelineSignature(StringID pass) const` SHALL return a `StringID` composed as `compose(TypeTag::MeshRender, {vertexBufferSig, indexBufferSig})`, where `vertexBufferSig = vertexBuffer->getPipelineSignature()` and `indexBufferSig = indexBuffer->getPipelineSignature()`. The `pass` parameter is part of the signature contract for uniformity with other pass-aware types, but the current implementation MAY ignore it. Future revisions MAY consume `pass` to drop unused attributes per render pass.

#### Scenario: Mesh signature is stable across pass arguments today
- **WHEN** `mesh->getPipelineSignature(Pass_Forward)` and `mesh->getPipelineSignature(Pass_Shadow)` are called with identical mesh configuration
- **THEN** both return the same `StringID` id under the current implementation

### Requirement: MaterialTemplate stores passes by StringID and composes per-pass signature
`MaterialTemplate` SHALL store pass entries in `std::unordered_map<StringID, MaterialPassDefinition, StringID::Hash>`. Its `setPass` and `getEntry` methods SHALL accept `StringID` pass keys. It SHALL provide `StringID getPipelinePassSignature(StringID pass) const` that looks up the entry for `pass` and returns `entry.getPipelineSignature()` if present. If `pass` is not configured, the method MUST return `StringID{}` (the default / id-0 sentinel).

#### Scenario: Pass lookup by StringID returns the configured entry
- **WHEN** `tmpl->setPass(Pass_Forward, entry)` is called, then `tmpl->getPipelinePassSignature(Pass_Forward)` is queried
- **THEN** the returned `StringID` equals `entry.getPipelineSignature()`

#### Scenario: Missing pass returns default StringID
- **WHEN** `tmpl->getPipelinePassSignature(Pass_Shadow)` is called but `Pass_Shadow` was never set
- **THEN** the returned `StringID.id` equals `0`

### Requirement: MaterialInstance exposes pass-aware getPipelineSignature
`MaterialInstance` SHALL provide `StringID getPipelineSignature(StringID pass) const`, returning `compose(TypeTag::MaterialRender, {templatePassSig})` where `templatePassSig` is `m_template->getPipelinePassSignature(pass)`.

#### Scenario: Two MaterialInstances sharing the same template and pass produce the same signature
- **WHEN** two `MaterialInstance` objects built from the same `MaterialTemplate` are queried with `getPipelineSignature(Pass_Forward)`
- **THEN** both return the same `StringID` id

#### Scenario: Per-instance UBO writes do not affect signature
- **WHEN** a `MaterialInstance::setVec4` call changes per-instance UBO state but does not touch the template
- **THEN** `getPipelineSignature(Pass_Forward)` continues to return the same `StringID` id

### Requirement: IRenderable exposes pass-aware getPipelineSignature
`IRenderable` SHALL declare `virtual StringID getPipelineSignature(StringID pass) const = 0`. `SceneNode`, as the primary high-level implementation, SHALL override it as `compose(TypeTag::ObjectRender, {meshSig})` where `meshSig = mesh->getPipelineSignature(pass)`. Optional `Skeleton` presence MUST NOT contribute a separate field to the object pipeline signature.

#### Scenario: Skeleton presence does not change object signature
- **WHEN** the same `SceneNode` is queried for `getPipelineSignature(Pass_Forward)` before and after a `Skeleton` is attached, with the same mesh and material variants
- **THEN** the returned `StringID` id is unchanged

#### Scenario: Mesh change still changes object signature
- **WHEN** a `SceneNode` switches to a mesh with a different pipeline signature and `getPipelineSignature(Pass_Forward)` is queried again
- **THEN** the two `StringID` ids differ

### Requirement: passFlagFromStringID helper translates pass identity to pass flag
The header `src/core/frame_graph/pass.hpp` SHALL declare `LX_core::ResourcePassFlag passFlagFromStringID(StringID pass)` and the implementation SHALL live in `src/core/frame_graph/pass.cpp`. The function SHALL map:
- `Pass_Forward` → `ResourcePassFlag::Forward`
- `Pass_Deferred` → `ResourcePassFlag::Deferred`
- `Pass_Shadow` → `ResourcePassFlag::Shadow`
- Any other `StringID` → `ResourcePassFlag{0}` (all bits clear)

This helper bridges the scene-layer pass identity (`StringID`) and the resource-layer pass flag (`ResourcePassFlag`). Core-layer code SHALL use this helper rather than hard-coded string comparisons when translating a pass `StringID` to a flag.

#### Scenario: Known pass constants map to the matching flag bit
- **WHEN** `passFlagFromStringID(Pass_Forward)` is called
- **THEN** the returned `ResourcePassFlag` equals `ResourcePassFlag::Forward`, and similarly for `Pass_Deferred` and `Pass_Shadow`

#### Scenario: Unknown StringID yields zero flag
- **WHEN** `passFlagFromStringID(Intern("CustomPass"))` is called for a pass identifier that is none of the three known constants
- **THEN** the returned `ResourcePassFlag` equals `ResourcePassFlag{0}` (no bits set)
