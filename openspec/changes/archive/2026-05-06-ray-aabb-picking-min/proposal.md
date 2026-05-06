## Why

`REQ-041` needs viewport click-selection before gizmo editing can work, but the engine still has no ray type, no mesh-local bounds contract, and no scene-level picking path. `REQ-038-a` intentionally takes the minimum useful subset now: mesh-local `BoundingBox` data, slab-tested ray-box intersection, and brute-force nearest-hit picking through the existing `SceneNode` graph.

## What Changes

- Introduce a minimal ray-picking contract based on existing `BoundingBox` rather than a new `AABB` type.
- Add a `Ray` value type plus `intersectRayBox(...) -> std::optional<float>` slab-test semantics in `src/core/math/`.
- **BREAKING** Extend `Mesh` creation/loading contracts so ingested meshes carry a local-space `BoundingBox`.
- Extend `SceneNode` with `getLocalBounds()` / `getWorldBounds()` derived through `MeshComponent` and the current world transform.
- Add `Scene::pick(const Ray&, VisibilityLayerMask)` returning the nearest `SceneNodeSharedPtr` hit under brute-force traversal.
- Extend `CameraComponent` with a screen-pixel to world-ray helper for editor-facing picking flows.
- Add integration coverage for ray-box intersection, transformed bounds behavior, visibility-mask filtering, and nearest-hit selection.

## Capabilities

### New Capabilities
- `scene-ray-picking`: Define the minimal ray/AABB picking contract, including ray-box intersection, node/world bounds lookup, and nearest-hit scene picking.

### Modified Capabilities
- `mesh-loading`: Mesh ingestion now computes and preserves local-space `BoundingBox` data for loaded meshes.
- `scene-node-components`: `SceneNode` gains bounds-query behavior derived from `MeshComponent` while preserving component-based ownership.
- `scene-camera-components`: `CameraComponent` gains the screen-to-world `pickRay(...)` helper used by editor picking flows.

## Impact

- Affected code: `src/core/math/bounds.hpp`, new `src/core/math/ray.hpp`, `src/core/asset/mesh.hpp`, GLTF/OBJ mesh loaders, `src/core/scene/object.*`, `src/core/scene/scene.*`, `src/core/scene/components/camera_component.*`, and new picking tests.
- Affected APIs: `Mesh::create(...)`, `SceneNode` bounds queries, `Scene::pick(...)`, and `CameraComponent::pickRay(...)`.
- Dependencies: builds on `REQ-035` transform hierarchy, `REQ-037-a` component composition, and `REQ-037-b` camera-as-component.
