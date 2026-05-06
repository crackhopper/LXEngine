## Purpose

Define the current skeleton resource contract, including core-layer types, UBO exposure, and pipeline-signature behavior.

## Requirements

### Requirement: Skeleton types live under core resources

`Bone`, `SkeletonUBO`, and `Skeleton` SHALL be declared in `src/core/asset/skeleton.hpp` (with implementation in `skeleton.cpp` if non-inline definitions are required). The previous files `src/core/scene/components/skeleton.hpp` and `skeleton.cpp` SHALL be removed after migration.

#### Scenario: Include path points to resources

- **WHEN** code references `Skeleton` or `Bone`
- **THEN** it SHALL include the resources-layer header path, not `scene/components/`

### Requirement: Skeleton does not use IComponent

`Skeleton` SHALL NOT inherit `IComponent` or any replacement scene-component base. The method `getRenderResources()` SHALL NOT exist on `Skeleton`.

#### Scenario: UBO access is explicit

- **WHEN** a caller needs the skeleton GPU uniform data
- **THEN** it SHALL obtain it via `getUBO()` returning `SkeletonUboPtr`

### Requirement: Skeleton public factory and bone API

`Skeleton` SHALL provide `static SkeletonSharedPtr create(const std::vector<Bone>& bones, ResourcePassFlag passFlag)`, `bool addBone(const Bone& bone)`, and `void updateUBO()` with behavior equivalent to the pre-migration implementation.

#### Scenario: Create and mutate bones

- **WHEN** `create` is called with a bone list and optional pass flag
- **THEN** the instance SHALL hold those bones and a valid `SkeletonUBO` for the same pass flag
- **WHEN** `addBone` succeeds within `MAX_BONE_COUNT`
- **THEN** the new bone SHALL be stored and reflected in the UBO as before

### Requirement: Skeleton exposes skinning presence for pipeline identity
`Skeleton` SHALL NOT contribute a pipeline-signature leaf or any other direct pipeline-identity token. The previous `StringID getPipelineSignature() const`, `size_t getPipelineHash() const`, and `kSkeletonPipelineHashTag` constant SHALL NOT exist. `Skeleton` remains a runtime resource provider through its Bones UBO and a legality dependency for passes whose material variants require skinning.

#### Scenario: Skeleton no longer exposes identity helpers
- **WHEN** a caller attempts to invoke `skeleton->getPipelineSignature()` or `skeleton->getPipelineHash()`
- **THEN** the call SHALL fail to compile because those identity helpers no longer exist

#### Scenario: Skeleton still provides runtime UBO data
- **WHEN** a skinned pass is structurally valid and needs Bones data at draw time
- **THEN** the render path obtains that data from the skeleton's UBO resource rather than from any pipeline-identity API

### Requirement: IComponent and base header are removed
The legacy scene-component base contract at `src/core/scene/components/base.hpp` SHALL remain removed. However, the codebase MAY introduce new scene-component infrastructure under current paths such as `src/core/scene/component.hpp` and `src/core/scene/components/` for node-local composition, provided that this infrastructure does not move `Skeleton` out of `src/core/asset/` and does not make `Skeleton` inherit `IComponent`.

#### Scenario: scene-component infrastructure returns without changing skeleton ownership

- **WHEN** the codebase introduces new node-local component files
- **THEN** `Skeleton` still lives under `src/core/asset/skeleton.hpp` and does not inherit `IComponent`

### Requirement: Camera and DirectionalLight expose UBOs without IComponent

`Camera` and `DirectionalLight` SHALL NOT inherit `IComponent`. Each SHALL expose its uniform data through a direct accessor (e.g. `getUBO()` returning the existing UBO smart pointer type) or equivalent typed access consistent with the pre-change `ubo` member visibility.

#### Scenario: Render path obtains camera and light UBOs

- **WHEN** the Vulkan renderer or tests need camera or directional light GPU resources
- **THEN** they SHALL use the new typed UBO accessors without calling `getRenderResources()`
