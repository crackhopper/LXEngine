## Why

REQ-041-a needs to converge prior editor primitives (transform hierarchy, path lookup, camera component, picking, debug draw, command bus, and ImGui bootstrap) into the first minimal in-engine editor. Current `lxe_editor` still exposes only stats/camera/light debug panels and cannot select or edit scene nodes through a unified command workflow.

## What Changes

- Add the `req-041-a-imgui-editor-mvp` OpenSpec change and implement the MVP editor surfaces in bounded increments.
- Introduce dedicated editor panels/overlay pieces: scene tree, inspector, viewport overlay/gizmo, and demo wiring around the existing console panel.
- Extend camera/render-queue behavior for preview camera toggling via active-camera filtering.
- Add targeted headless/editor smoke coverage where feasible, then finish with the required demo build/test matrix.

## Capabilities

### New Capabilities
- `imgui-editor-mvp`: minimal in-engine editor with scene selection, inspection, gizmo manipulation, overlay visualization, and preview camera toggling.

## Impact

- Adds new editor UI code under `src/core/editor/`.
- Updates `lxe_editor` demo integration and camera/render-queue behavior.
- Reuses the REQ-040 command bus as the only mutation entrypoint.
