## ADDED Requirements

### Requirement: PipelineKey wraps a stable StringID for pipeline identity

The system SHALL provide `LX_core::PipelineKey` in core holding a `StringID` that uniquely identifies a graphics pipeline configuration. `PipelineKey` SHALL provide `operator==`, `operator!=`, and a nested `Hash` type suitable for `std::unordered_map`.

#### Scenario: Equal keys compare by StringID

- **WHEN** two `PipelineKey` values were built from the same canonical interned string
- **THEN** their `StringID` members SHALL be equal and `operator==` SHALL return true

### Requirement: PipelineKey build composes factors per canonical format

`PipelineKey::build` SHALL accept `ShaderProgramSet`, `VertexLayout`, `RenderState`, `PrimitiveTopology`, and `bool hasSkeleton`, and SHALL produce a `PipelineKey` by concatenating factors into a single canonical string and interning it via `StringID`. The implementation SHALL obtain numeric/hash segments using `getPipelineHash()` on each participating resource type as specified in the resource-pipeline-hash capability, not ad hoc hash methods at the key boundary. Factors SHALL appear in the order: shader name and variant segment, vertex layout and topology segment, render state segment, optional skeleton suffix as required by the project REQ-002 document.

#### Scenario: Different shader variants yield different keys

- **WHEN** two builds differ only by shader variant macros
- **THEN** the resulting `PipelineKey::id` values SHALL differ

### Requirement: RenderingItem carries PipelineKey

`RenderingItem` SHALL contain a `PipelineKey pipelineKey` member supplied when the item is built for rendering.

#### Scenario: Scene fills pipeline key

- **WHEN** `Scene::buildRenderingItem()` constructs a `RenderingItem` from a renderable with consistent shader, mesh layout, render state, topology, and skeleton presence
- **THEN** `item.pipelineKey` SHALL be set to `PipelineKey::build(...)` for those inputs
