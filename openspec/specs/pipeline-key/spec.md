## Purpose

Define `PipelineKey` as the core-layer identity for Vulkan (or other) graphics pipelines, built from structured pipeline signatures composed through `GlobalStringTable`, and carried on `RenderingItem` for cache lookup.

## Requirements

### Requirement: PipelineKey wraps a stable StringID for pipeline identity

The system SHALL provide `LX_core::PipelineKey` in core holding a `StringID` that uniquely identifies a graphics pipeline configuration. `PipelineKey` SHALL provide `operator==`, `operator!=`, and a nested `Hash` type suitable for `std::unordered_map`. The underlying `StringID` SHALL be a structured id produced by `GlobalStringTable::compose(TypeTag::PipelineKey, ...)`, so that `GlobalStringTable::toDebugString(id)` fully renders the participating object and material signatures.

#### Scenario: Equal keys compare by StringID

- **WHEN** two `PipelineKey` values were built from the same canonical compose result
- **THEN** their `StringID` members SHALL be equal and `operator==` SHALL return true

#### Scenario: toDebugString renders the full pipeline tree
- **WHEN** `GlobalStringTable::get().toDebugString(key.id)` is called on a `PipelineKey` built from structured object, material, and target signatures
- **THEN** the returned string starts with `"PipelineKey("` and recursively contains the children's tag names (`ObjectRender(...)`, `MaterialRender(...)`, `TargetRender(...)`, etc.)

### Requirement: PipelineKey build composes object, material, and target signatures

`PipelineKey::build(StringID objectSig, StringID materialSig, StringID targetSig)` SHALL produce a `PipelineKey` whose `id` equals `GlobalStringTable::get().compose(TypeTag::PipelineKey, {objectSig, materialSig, targetRender})`, where `targetRender` is `GlobalStringTable::get().compose(TypeTag::TargetRender, {targetSig})`.

Callers assembling pipeline identity SHALL first resolve `objectSig` via `IRenderable::getPipelineSignature(pass)`, `materialSig` via `MaterialInstance::getPipelineSignature(pass)`, and `targetSig` via `RenderTargetDesc::getPipelineSignature()`. This makes depth-only, swapchain color/depth, and future MRT/HDR targets distinct pipeline-cache identities even when object and material signatures match.

A two-argument `PipelineKey::build(objectSig, materialSig)` MAY remain available for legacy unit tests or code that intentionally omits target identity, but `RenderQueue` and backend-facing render paths SHALL use the three-argument target-aware form.

#### Scenario: Different material signatures yield different keys

- **WHEN** two builds share `objectSig` but differ in `materialSig`
- **THEN** the resulting `PipelineKey::id` values SHALL differ

#### Scenario: Same signatures yield same key

- **WHEN** two builds pass the same `objectSig`, `materialSig`, and `targetSig`
- **THEN** the resulting `PipelineKey::id` values SHALL be equal

#### Scenario: Different target signatures yield different keys

- **WHEN** two builds share `objectSig` and `materialSig` but one uses a swapchain target signature and the other uses an offscreen depth-only target signature
- **THEN** the resulting `PipelineKey::id` values SHALL differ and `toDebugString` SHALL include `TargetRender(...)`

### Requirement: RenderingItem carries PipelineKey and Pass

`RenderingItem` SHALL contain a `PipelineKey pipelineKey`, a `StringID pass`, and a `RenderTargetDesc target`, all supplied when the item is built for rendering.

#### Scenario: Scene fills pipeline key and pass

- **WHEN** `RenderQueue::buildFromScene(scene, Pass_Forward, target)` constructs a `RenderingItem` from a renderable with valid mesh and material
- **THEN** `item.pipelineKey` SHALL be set to `PipelineKey::build(objectSig, materialSig, target.toDesc().getPipelineSignature())` for `Pass_Forward`, `item.pass` SHALL equal `Pass_Forward`, and `item.target` SHALL equal `target.toDesc()`
