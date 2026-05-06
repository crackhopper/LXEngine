## 1. Camera Component Core

- [x] 1.1 Add `CameraComponent` under `src/core/scene/components/` with camera UBO ownership, projection parameters, target/culling-mask state, and active-state accessors.
- [x] 1.2 Remove the standalone `Camera` class while preserving shared GPU-facing camera resource types needed by the renderer.
- [x] 1.3 Implement owner-transform-derived view/projection refresh for `CameraComponent`, including explicit scale stripping from the owner world transform.
- [x] 1.4 Add pose writeback APIs on `CameraComponent` (`setPosition`, `lookAt`, and related helpers) that update the owning `SceneNode` transform.

## 2. Scene and Queue Integration

- [x] 2.1 Update `Scene` camera registration to work with camera-bearing `SceneNode` instances instead of standalone camera objects.
- [x] 2.2 Ensure registered camera nodes remain compatible with root attachment, path lookup, and hierarchy semantics.
- [x] 2.3 Update scene-level camera resource collection and combined culling-mask aggregation to enumerate `CameraComponent` entries and ignore inactive cameras.
- [x] 2.4 Update render-queue / backend-facing camera call sites to consume the new camera-component registration path without changing target-matching behavior.

## 3. Controllers and Demo Migration

- [x] 3.1 Change `ICameraController`, `OrbitCameraController`, and `FreeFlyCameraController` to operate on `CameraComponent`.
- [x] 3.2 Preserve orbit/free-fly controller behavior while switching pose writeback to `CameraComponent` methods instead of direct camera field mutation.
- [x] 3.3 Update `scene_viewer` camera construction, camera rig switching, and per-frame camera refresh to use `SceneNode` + `CameraComponent`.
- [x] 3.4 Update demo/editor-facing camera UI code to edit the active camera node transform plus camera-component properties.

## 4. Verification and Documentation

- [x] 4.1 Add or migrate integration tests for parented camera motion, scale-invariant view matrices, camera path lookup, and inactive-camera filtering.
- [x] 4.2 Update orbit/free-fly controller tests to exercise `CameraComponent` attached to `SceneNode`.
- [x] 4.3 Update any affected notes/source-analysis pages for camera and scene behavior after the component migration.
- [x] 4.4 Run targeted build/test coverage for camera controllers, scene/path behavior, and demo-adjacent camera flows.
