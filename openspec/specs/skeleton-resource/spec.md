## ADDED Requirements

### Requirement: Skeleton types live under core resources

`Bone`, `SkeletonUBO`, and `Skeleton` SHALL be declared in `src/core/resources/skeleton.hpp` (with implementation in `skeleton.cpp` if non-inline definitions are required). The previous files `src/core/scene/components/skeleton.hpp` and `skeleton.cpp` SHALL be removed after migration.

#### Scenario: Include path points to resources

- **WHEN** code references `Skeleton` or `Bone`
- **THEN** it SHALL include the resources-layer header path, not `scene/components/`

### Requirement: Skeleton does not use IComponent

`Skeleton` SHALL NOT inherit `IComponent` or any replacement scene-component base. The method `getRenderResources()` SHALL NOT exist on `Skeleton`.

#### Scenario: UBO access is explicit

- **WHEN** a caller needs the skeleton GPU uniform data
- **THEN** it SHALL obtain it via `getUBO()` returning `SkeletonUboPtr`

### Requirement: Skeleton public factory and bone API

`Skeleton` SHALL provide `static SkeletonPtr create(const std::vector<Bone>& bones, ResourcePassFlag passFlag)`, `bool addBone(const Bone& bone)`, and `void updateUBO()` with behavior equivalent to the pre-migration implementation.

#### Scenario: Create and mutate bones

- **WHEN** `create` is called with a bone list and optional pass flag
- **THEN** the instance SHALL hold those bones and a valid `SkeletonUBO` for the same pass flag
- **WHEN** `addBone` succeeds within `MAX_BONE_COUNT`
- **THEN** the new bone SHALL be stored and reflected in the UBO as before

### Requirement: Skeleton exposes skinning presence for pipeline identity

`Skeleton` SHALL provide `bool hasSkeleton() const` returning true for instances that participate in skinned rendering (per project convention for pipeline switches). It SHALL provide `size_t getPipelineHash() const` derived from that boolean (and any other pipeline-affecting state required by REQ-001).

#### Scenario: Hash is stable for equivalent skinning flag

- **WHEN** two `Skeleton` instances have the same pipeline-relevant skinning state
- **THEN** `getPipelineHash()` SHALL return the same value for both

### Requirement: IComponent and base header are removed

The type `IComponent` and the file `src/core/scene/components/base.hpp` SHALL be deleted. The directory `src/core/scene/components/` SHALL be removed if it contains no remaining translation units.

#### Scenario: No remaining IComponent symbols

- **WHEN** the codebase is built after the change
- **THEN** no type SHALL inherit `IComponent` and no translation unit SHALL include `components/base.hpp`

### Requirement: Camera and DirectionalLight expose UBOs without IComponent

`Camera` and `DirectionalLight` SHALL NOT inherit `IComponent`. Each SHALL expose its uniform data through a direct accessor (e.g. `getUBO()` returning the existing UBO smart pointer type) or equivalent typed access consistent with the pre-change `ubo` member visibility.

#### Scenario: Render path obtains camera and light UBOs

- **WHEN** the Vulkan renderer or tests need camera or directional light GPU resources
- **THEN** they SHALL use the new typed UBO accessors without calling `getRenderResources()`
