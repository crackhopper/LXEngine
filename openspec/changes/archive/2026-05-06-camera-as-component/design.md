## Context

`REQ-037-a` moved mesh/material/skeleton ownership into `SceneNode` components, but cameras still live as standalone `Camera` objects in `Scene::m_cameras`. That leaves three mismatched paths in the engine:

- transform hierarchy applies to renderable nodes, not cameras
- path lookup / inspector / command routing work on `SceneNode`, not cameras
- controller writeback and scene-level camera resource collection still depend on a separate object model

This change needs to preserve current rendering semantics while removing that split. Existing behavior that must survive intact:

- camera target matching and culling-mask filtering
- scene-level camera UBO collection before queue building
- orbit/freefly controller feel and F2 switching in `lxe_editor`
- path lookup and transform hierarchy contracts already implemented by `SceneNode`

The repository also has two style constraints that shape the design:

- no raw pointers for stored ownership or object references
- keep narrow, explicit contracts instead of compatibility shims

## Goals / Non-Goals

**Goals:**

- Make cameras first-class `SceneNode` components so camera nodes can participate in hierarchy, path lookup, and inspector flows.
- Preserve scene-level rendering behavior by keeping target matching, culling-mask filtering, and camera UBO production stable.
- Retarget orbit/freefly controllers to `CameraComponent` without changing their input semantics.
- Keep the migration localized: camera registration, controllers, render-queue camera enumeration, demo bootstrapping, and tests.

**Non-Goals:**

- Introduce a full ECS, camera subtype hierarchy, or generalized component dependency system.
- Change `RenderTarget` matching semantics or the `REQ-042` target-shape migration plan.
- Make camera-node scale affect the view matrix.
- Add editor UI polish beyond what is required for camera nodes to exist and be controllable.

## Decisions

### 1. Introduce `CameraComponent` and retire standalone `Camera`

`CameraComponent` becomes the single high-level camera object. It owns:

- GPU-facing `CameraData` / UBO resource
- projection parameters
- target binding state
- culling-mask state
- active/inactive state

The old standalone `Camera` class is removed instead of wrapped.

Why:

- A wrapper would keep two public camera identities alive and prolong the migration.
- `SceneNode` is already the engine’s stable owner for node-local composition.
- Scene path lookup, transform hierarchy, and future editor work all need one camera identity, not a node-plus-sidecar pair.

Alternatives considered:

- Keep `Camera` and attach it through a component wrapper.
  Rejected: duplicates API surface and leaves scene registration ambiguous.
- Make `CameraComponent` inherit from `Camera`.
  Rejected: preserves the old data model and keeps transform state separate from the owner node.

### 2. Use `SceneNode` ownership in the camera registry, not raw node pointers

The requirement draft says `Scene::addCamera(node)` takes `SceneNode*`, but the implementation contract will use node references / shared ownership consistent with the existing scene layer. `Scene` will register camera nodes through `SceneNodeSharedPtr`-compatible APIs and store camera entries as node references rather than as standalone camera objects.

Why:

- The repo’s style guide forbids raw pointers as stored object references.
- Camera nodes must remain path-addressable and hierarchy-aware after registration.
- `Scene` already uses `shared_ptr` ownership for renderables and lights.

Alternatives considered:

- Store raw `SceneNode*` in `m_cameras`.
  Rejected: violates repo ownership rules and weakens lifetime clarity.
- Keep `m_cameras` as `CameraComponent*` / `shared_ptr<CameraComponent>`.
  Rejected: loses direct access to the owning node needed for hierarchy and path operations.

### 3. Derive view state from the owner transform and strip scale

`CameraComponent` will derive its view matrix from the owning node’s world transform. Translation and rotation come from the node. Scale is explicitly removed before computing the view matrix.

Why:

- Cameras need to move with hierarchy and reparenting.
- Editor TRS tooling should work on camera nodes.
- View matrices polluted by node scale are almost always authoring mistakes and break controller expectations.

Alternatives considered:

- Continue storing explicit camera `position/target/up` fields on the camera object.
  Rejected: keeps transform duplication and breaks hierarchy composition.
- Use the full world matrix including scale.
  Rejected: introduces non-rigid camera transforms and inconsistent projection behavior.

### 4. Keep controller state internal; write pose through `CameraComponent`

Orbit and free-fly controllers keep their current internal state models (`target/distance/yaw/pitch` and `position/yaw/pitch`). They stop mutating camera fields directly and instead write pose through `CameraComponent` methods such as `setPosition(...)` and `lookAt(...)`.

Why:

- Controller feel stays intact.
- The owner `SceneNode` remains the source of truth for local/world transforms.
- Controllers do not need to know about parent nodes or scene hierarchy internals.

Alternatives considered:

- Move controller state into `CameraComponent`.
  Rejected: couples editor/runtime controller policies to the camera data model.
- Let controllers mutate `SceneNode` directly.
  Rejected: leaks scene ownership concerns into controller code and weakens reusable controller APIs.

### 5. Preserve scene-level render filtering semantics, add explicit active-state filtering

`matchesTarget(...)` and culling-mask behavior remain camera concerns even after cameras become components. `Scene::getSceneLevelResources(...)` and `Scene::getCombinedCameraCullingMask(...)` will enumerate registered camera nodes, extract `CameraComponent`, skip inactive cameras, and otherwise preserve today’s filtering semantics.

Why:

- Render-target selection and culling masks are already working and must not regress.
- `REQ-041` needs a lightweight way to disable editor or gameplay cameras without removing nodes.

Alternatives considered:

- Push camera filtering into `RenderQueue`.
  Rejected: duplicates scene-level policy and expands queue responsibilities.
- Model inactive cameras by unregistering them from `Scene`.
  Rejected: loses stable path identity and creates unnecessary registry churn.

## Risks / Trade-offs

- `Controller migration touches multiple call sites` → Mitigation: keep controller input semantics unchanged and constrain API churn to type signatures plus pose writeback.
- `Camera-node registration could drift from renderable registration rules` → Mitigation: keep registration explicit in `Scene`, define ownership and root-attachment behavior in the new spec, and reuse `SceneNode` naming/path contracts.
- `Scale-stripping from owner transforms can be implemented inconsistently` → Mitigation: define the behavior normatively in the camera-component spec and test parented/scaled camera scenarios directly.
- `Deleting Camera removes a widely visible type` → Mitigation: land complete spec + design + tasks up front, then migrate controllers/demo/tests in one implementation pass instead of adding compatibility shims.
