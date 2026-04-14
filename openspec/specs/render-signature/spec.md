## ADDED Requirements

### Requirement: Pass system constants
The system SHALL provide a header `src/core/scene/pass.hpp` exposing `StringID` constants `Pass_Forward`, `Pass_Deferred`, and `Pass_Shadow` in namespace `LX_core`. These constants MUST be declared `inline const` (not `constexpr`) because `StringID` construction interns the underlying name into `GlobalStringTable`.

#### Scenario: Pass constants are stable across translation units
- **WHEN** `Pass_Forward` is referenced from two different translation units in the same process
- **THEN** both references resolve to the same `StringID.id` value (the `"Forward"` leaf id)

#### Scenario: Distinct pass constants produce distinct ids
- **WHEN** `Pass_Forward`, `Pass_Deferred`, and `Pass_Shadow` are compared pairwise
- **THEN** all three `StringID.id` values MUST be distinct

### Requirement: Leaf resources provide parameterless getRenderSignature
Every leaf resource participating in pipeline identity SHALL provide a `StringID getRenderSignature() const` method that returns a structurally-interned `StringID` computed via `GlobalStringTable::compose(...)` or `GlobalStringTable::Intern(...)`. This applies to `VertexLayoutItem`, `VertexLayout`, `RenderState`, `ShaderProgramSet`, `Skeleton`, and `RenderPassEntry`.

`PrimitiveTopology` SHALL be handled by a free function `StringID topologySignature(PrimitiveTopology)` instead of a member, since enums cannot carry methods.

#### Scenario: Same VertexLayout produces same signature
- **WHEN** two `VertexLayout` instances have identical items and stride
- **THEN** `getRenderSignature()` returns the same `StringID` id on both

#### Scenario: Different layout item order produces different signature
- **WHEN** two `VertexLayout` instances share the same items but in different declaration order
- **THEN** `getRenderSignature()` returns different `StringID` ids (compose is order sensitive)

#### Scenario: RenderState signature reflects all seven render-state fields
- **WHEN** any of `cullMode`, `depthTestEnable`, `depthWriteEnable`, `depthOp`, `blendEnable`, `srcBlend`, `dstBlend` changes
- **THEN** `RenderState::getRenderSignature()` returns a different `StringID` id

#### Scenario: ShaderProgramSet signature ignores variant order
- **WHEN** two `ShaderProgramSet` instances enable the same set of variant macros in different insertion order
- **THEN** `getRenderSignature()` returns the same id (variants MUST be sorted before compose)

#### Scenario: Topology signature distinguishes primitive types
- **WHEN** `topologySignature(PrimitiveTopology::TriangleList)` and `topologySignature(PrimitiveTopology::LineList)` are compared
- **THEN** the returned `StringID` ids are different

### Requirement: Mesh provides pass-aware getRenderSignature
`Mesh::getRenderSignature(StringID pass) const` SHALL return a `StringID` composed as `compose(TypeTag::MeshRender, {vertexLayoutSig, topologySig})`. The `pass` parameter is part of the signature contract for uniformity with other pass-aware types, but the current implementation MAY ignore it. Future revisions MAY consume `pass` to drop unused attributes per render pass.

#### Scenario: Mesh signature is stable across pass arguments today
- **WHEN** `mesh->getRenderSignature(Pass_Forward)` and `mesh->getRenderSignature(Pass_Shadow)` are called with identical mesh configuration
- **THEN** both return the same `StringID` id under the current implementation

### Requirement: MaterialTemplate stores passes by StringID and composes per-pass signature
`MaterialTemplate` SHALL store pass entries in `std::unordered_map<StringID, RenderPassEntry, StringID::Hash>`. Its `setPass` and `getEntry` methods SHALL accept `StringID` pass keys. It SHALL provide `StringID getRenderPassSignature(StringID pass) const` that looks up the entry for `pass` and returns `entry.getRenderSignature()` if present. If `pass` is not configured, the method MUST return `StringID{}` (the default / id-0 sentinel).

#### Scenario: Pass lookup by StringID returns the configured entry
- **WHEN** `tmpl->setPass(Pass_Forward, entry)` is called, then `tmpl->getRenderPassSignature(Pass_Forward)` is queried
- **THEN** the returned `StringID` equals `entry.getRenderSignature()`

#### Scenario: Missing pass returns default StringID
- **WHEN** `tmpl->getRenderPassSignature(Pass_Shadow)` is called but `Pass_Shadow` was never set
- **THEN** the returned `StringID.id` equals `0`

### Requirement: IMaterial exposes pass-aware getRenderSignature
`IMaterial` SHALL declare `virtual StringID getRenderSignature(StringID pass) const = 0`. `MaterialInstance` (the concrete implementation from REQ-005) SHALL override it, returning `compose(TypeTag::MaterialRender, {templatePassSig})` where `templatePassSig` is `m_template->getRenderPassSignature(pass)`.

#### Scenario: Two MaterialInstances sharing the same template and pass produce the same signature
- **WHEN** two `MaterialInstance` objects built from the same `MaterialTemplate` are queried with `getRenderSignature(Pass_Forward)`
- **THEN** both return the same `StringID` id

#### Scenario: Per-instance UBO writes do not affect signature
- **WHEN** a `MaterialInstance::setVec4` call changes per-instance UBO state but does not touch the template
- **THEN** `getRenderSignature(Pass_Forward)` continues to return the same `StringID` id

### Requirement: IRenderable exposes pass-aware getRenderSignature
`IRenderable` SHALL declare `virtual StringID getRenderSignature(StringID pass) const = 0`. `RenderableSubMesh` SHALL override it as `compose(TypeTag::ObjectRender, {meshSig, skelSig})` where `meshSig = mesh->getRenderSignature(pass)` and `skelSig = skeleton.has_value() ? skeleton.value()->getRenderSignature() : StringID{}`.

#### Scenario: Renderable without skeleton uses default skel signature
- **WHEN** `RenderableSubMesh::getRenderSignature(Pass_Forward)` is called on an instance with `skeleton == std::nullopt`
- **THEN** the returned compose contains `StringID{}` (id 0) as the second field

#### Scenario: Adding a skeleton changes the signature
- **WHEN** the same `RenderableSubMesh` is queried before and after `skeleton` is assigned
- **THEN** the two `StringID` ids differ

### Requirement: passFlagFromStringID helper translates pass identity to pass flag
The header `src/core/scene/pass.hpp` SHALL declare `LX_core::ResourcePassFlag passFlagFromStringID(StringID pass)` and the implementation SHALL live in a new `src/core/scene/pass.cpp` translation unit. The function SHALL map:
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
