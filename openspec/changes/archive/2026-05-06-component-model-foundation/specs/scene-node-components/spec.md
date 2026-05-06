## ADDED Requirements

### Requirement: SceneNode exposes typed node-local component composition
The engine SHALL provide a node-local component contract for `SceneNode` through a lightweight `IComponent` base and typed component lookup. `SceneNode` SHALL own components through `std::unique_ptr<IComponent>` and SHALL expose typed APIs equivalent to:

- `addComponent<T>(...)`
- `getComponent<T>()`
- `removeComponent<T>()`
- `listComponents()`

Each concrete component type SHALL have a stable process-local type identifier suitable for typed lookup without using `typeid(T).hash_code()`. Component ownership SHALL remain exclusive to the owning `SceneNode`, and component-to-owner access SHALL use a defined non-owning owner contract.

#### Scenario: typed add and lookup succeed
- **WHEN** a caller adds a `MeshComponent` to a `SceneNode`
- **THEN** a later `getComponent<MeshComponent>()` on that node returns the same logical component instance

#### Scenario: component list preserves insertion order
- **WHEN** a caller adds multiple distinct component types to one `SceneNode`
- **THEN** `listComponents()` returns them in the same order they were attached

### Requirement: SceneNode allows at most one component per concrete type
For this capability version, a `SceneNode` SHALL hold at most one instance of any concrete component type. Adding a second component of the same concrete type to the same node MUST be treated as a programmer error rather than silently replacing or coexisting with the first instance.

#### Scenario: duplicate type attachment is rejected
- **WHEN** a caller attempts to add a second `MaterialComponent` to the same `SceneNode`
- **THEN** the operation fails as a programmer error and the original component remains attached

### Requirement: Mesh, material, and skeleton structural payloads are represented by typed components
Renderable structural payloads on `SceneNode` SHALL be carried by typed components rather than dedicated node fields:

- `MeshComponent` owns `MeshSharedPtr`
- `MaterialComponent` owns `MaterialInstanceSharedPtr`
- `SkeletonComponent` owns `SkeletonSharedPtr`

These components SHALL preserve the existing underlying resource APIs and ownership types. The absence of a skeleton on a node SHALL be represented by the absence of `SkeletonComponent`, not by an optional wrapper stored inside `SceneNode`.

#### Scenario: skeleton absence is modeled by missing component
- **WHEN** a node has no `SkeletonComponent`
- **THEN** renderable validation treats the node as having no skeleton resource attached

#### Scenario: component mutation preserves resource API shape
- **WHEN** a caller reads the mesh or material from the corresponding structural component
- **THEN** the returned handle types remain `MeshSharedPtr` and `MaterialInstanceSharedPtr`

### Requirement: Material component owns material pass-state revalidation hookup
The active material binding on a `SceneNode` SHALL continue to trigger structural revalidation when its enabled-pass set changes, but the listener lifecycle SHALL be owned by `MaterialComponent` rather than by dedicated `SceneNode` material fields.

Replacing or removing the material component MUST update the listener lifecycle so that no stale callback remains bound to the previous material instance.

#### Scenario: shared material pass change still revalidates node
- **WHEN** a `MaterialInstance` attached through `MaterialComponent` enables or disables a pass
- **THEN** the referencing `SceneNode` rebuilds its validated pass state before further pass queries are trusted

#### Scenario: removing material component clears listener ownership
- **WHEN** a `MaterialComponent` is removed or destroyed
- **THEN** its pass-state callback is detached from the previously bound material instance before the component lifetime ends
