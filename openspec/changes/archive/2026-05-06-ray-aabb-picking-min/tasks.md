## 1. Math and Mesh Bounds Foundations

- [x] 1.1 Add `src/core/math/ray.hpp` with `Ray` and `intersectRayBox(...)` slab-test semantics using `BoundingBox`.
- [x] 1.2 Extend `Mesh` to store local-space `BoundingBox` data and update `Mesh::create(...)` call signatures accordingly.
- [x] 1.3 Update OBJ and GLTF mesh-loading paths to compute bounds during position ingestion and populate `Mesh::bounds`.
- [x] 1.4 Update procedural/demo/test mesh construction call sites to provide valid bounds through the unified `Mesh::create(...)` path.

## 2. Scene and Camera Picking APIs

- [x] 2.1 Add `SceneNode::getLocalBounds()` and `getWorldBounds()` derived through `MeshComponent` and the current world transform.
- [x] 2.2 Add `Scene::PickHit` and `Scene::pick(const Ray&, VisibilityLayerMask)` with brute-force nearest-hit traversal and visibility-mask filtering.
- [x] 2.3 Add `CameraComponent::pickRay(screenPixel, viewportSize)` for perspective and orthographic cameras with normalized world-space direction.

## 3. Verification and Integration Coverage

- [x] 3.1 Add integration coverage for `intersectRayBox(...)` hit, miss, tangent, and origin-inside cases.
- [x] 3.2 Add integration coverage for transformed bounds, node-local/world bounds lookup, and nearest-hit scene picking with layer-mask filtering.
- [x] 3.3 Run targeted build/test coverage for mesh-loading-adjacent call sites and the new picking test suite.
