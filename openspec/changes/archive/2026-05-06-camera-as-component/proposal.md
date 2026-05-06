## Why

`Camera` is still outside the `SceneNode` component model, so cameras cannot participate in transform hierarchy, path-based selection, or the same inspector/command flows as renderable nodes. After `REQ-037-a` landed, this is the next blocking step for editor work because camera movement, gizmo editing, and node-local composition are still split across two scene contracts.

## What Changes

- Introduce `CameraComponent` as a node-local `IComponent` that owns camera projection, culling-mask, target, and active-state data.
- **BREAKING** Replace direct `Camera` scene usage with `SceneNode` + `CameraComponent` registration in `Scene::m_cameras`.
- **BREAKING** Update camera-controller interfaces from `Camera&` / `Camera*` to `CameraComponent&` / `CameraComponent*` while preserving controller behavior.
- Derive camera view state from the owning `SceneNode` transform hierarchy, with node scale explicitly ignored for view-matrix construction.
- Keep scene-level camera filtering semantics (`matchesTarget`, `cullingMask`) intact while changing the owning object model.
- Move demo/editor-facing camera construction to `SceneNode` + `addComponent<CameraComponent>(...)`.

## Capabilities

### New Capabilities
- `scene-camera-components`: Define `CameraComponent`, owner-transform-derived view semantics, and Scene camera registration through `SceneNode`.

### Modified Capabilities
- `scene-node-components`: Extend the node-local component contract to cover `CameraComponent` as a supported non-renderable component type.
- `camera-controller`: Change controller contracts to write through `CameraComponent` rather than a standalone `Camera` object.
- `freefly-camera-controller`: Keep free-fly behavior but retarget writeback semantics to `CameraComponent`.
- `camera-visibility-mask`: Preserve culling-mask and scene-level resource behavior after cameras become node-local components.
- `demo-scene-viewer`: Update the demo camera rig and startup contract to construct cameras through `SceneNode` + `CameraComponent`.

## Impact

- Affected code: `src/core/scene/camera*`, `src/core/scene/components/`, `src/core/scene/scene*`, `src/core/frame_graph/render_queue.cpp`, controller implementations, `scene_viewer`, and integration tests.
- Affected APIs: camera creation/registration, controller update signatures, and any call sites that currently expect a standalone `Camera`.
- Dependencies: builds directly on `scene-node-components`, `scene-transform-hierarchy`, `scene-node-path-lookup`, and existing camera culling-mask behavior.
