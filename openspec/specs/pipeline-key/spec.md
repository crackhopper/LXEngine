## Purpose

Define `PipelineKey` as the core-layer identity for Vulkan (or other) graphics pipelines, built from structured pipeline signatures composed through `GlobalStringTable`, and carried on `RenderingItem` for cache lookup.

## Requirements

### Requirement: PipelineKey wraps a stable StringID for pipeline identity

The system SHALL provide `LX_core::PipelineKey` in core holding a `StringID` that uniquely identifies a graphics pipeline configuration. `PipelineKey` SHALL provide `operator==`, `operator!=`, and a nested `Hash` type suitable for `std::unordered_map`. The underlying `StringID` SHALL be a structured id produced by `GlobalStringTable::compose(TypeTag::PipelineKey, ...)`, so that `GlobalStringTable::toDebugString(id)` fully renders the participating object and material signatures.

#### Scenario: Equal keys compare by StringID

- **WHEN** two `PipelineKey` values were built from the same canonical compose result
- **THEN** their `StringID` members SHALL be equal and `operator==` SHALL return true

#### Scenario: toDebugString renders the full pipeline tree
- **WHEN** `GlobalStringTable::get().toDebugString(key.id)` is called on a `PipelineKey` built from a structured object and material signature
- **THEN** the returned string starts with `"PipelineKey("` and recursively contains the children's tag names (`ObjectRender(...)`, `MaterialRender(...)`, etc.)

### Requirement: PipelineKey build composes object and material signatures

`PipelineKey::build` SHALL accept exactly two arguments, `StringID objectSig` and `StringID materialSig`, and SHALL produce a `PipelineKey` whose `id` equals `GlobalStringTable::get().compose(TypeTag::PipelineKey, {objectSig, materialSig})`. Callers assembling pipeline identity SHALL first resolve `objectSig` via `IRenderable::getPipelineSignature(pass)` and `materialSig` via `MaterialInstance::getPipelineSignature(pass)`.

#### Scenario: Different material signatures yield different keys

- **WHEN** two builds share `objectSig` but differ in `materialSig`
- **THEN** the resulting `PipelineKey::id` values SHALL differ

#### Scenario: Same signatures yield same key

- **WHEN** two builds pass the same `objectSig` and `materialSig`
- **THEN** the resulting `PipelineKey::id` values SHALL be equal

### Requirement: RenderingItem carries PipelineKey and Pass

`RenderingItem` SHALL contain a `PipelineKey pipelineKey` member AND a `StringID pass` member, both supplied when the item is built for rendering.

#### Scenario: Scene fills pipeline key and pass

- **WHEN** `RenderQueue::buildFromScene(scene, Pass_Forward)` constructs a `RenderingItem` from a renderable with valid mesh and material
- **THEN** `item.pipelineKey` SHALL be set to `PipelineKey::build(objectSig, materialSig)` for `Pass_Forward` and `item.pass` SHALL equal `Pass_Forward`
