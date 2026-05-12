# lxe_editor Windowless Gizmo Toolbar Design

## Context

`lxe_editor` currently mixes editor mode and camera control mode in
`UiOverlay::EditMode`: `Selection`, `Orbit`, and `FreeFly` are mutually
exclusive. This conflicts with the desired behavior: selection/editing mode and
camera control type are orthogonal.

The current ImGuizmo integration is also tied to `ViewportOverlay::draw()`,
which creates an ImGui window named `Viewport`. The gizmo is visible there, but
that window is not the real rendered scene viewport. It creates a misleading
second interaction surface and interferes with the current editor model.

## Goals

- Remove the fake `Viewport` ImGui window from the maintained editor path.
- Keep ImGuizmo, but draw it as a windowless overlay over the real scene area.
- Make editor mode and camera control mode separate state.
- Preserve left mouse for mode/gizmo/selection interaction.
- Preserve right mouse for camera interaction.
- Clean code, tests, and current docs so no maintained path still describes the
  old `Selection` / `Orbit` / `FreeFly` mutually exclusive mode model.

## Non-Goals

- Do not implement a real docked viewport panel in this change.
- Do not turn gizmo axes into engine scene renderables.
- Do not remove ImGuizmo or `GizmoAdapter`.
- Do not change Vulkan viewport/scissor behavior; scene rendering remains
  full-window swapchain rendering.
- Do not preserve command compatibility for `mode orbit` or `mode freefly`.

## Architecture

### Scene Rect Source

Keep using `SceneViewRect` as the editor's scene interaction rectangle type.
For this change, the scene rect resolves to the full window:

```text
x = 0
y = 0
width = window width
height = window height
```

This keeps the implementation aligned with the renderer, which currently draws
the scene to the whole swapchain. The API shape remains replaceable so a future
real viewport panel can provide a narrower rect without rewriting selection or
gizmo behavior.

### Windowless Gizmo Overlay

Convert `ViewportOverlay` from a window-owning UI panel into a scene overlay
interaction component:

- Stop creating `ImGui::Begin("Viewport")`.
- Stop creating `InvisibleButton("##viewport_canvas")`.
- Add a draw entry that accepts a `SceneViewRect`.
- Use the accepted rect for `ImGuizmo::SetRect(...)`.
- Draw ImGuizmo into an ImGui overlay draw list for the current frame.
- Continue using the editor camera view/projection matrices and primary
  selected node transform.
- Continue dispatching transform commits through `CommandBus`.

The class name stays `ViewportOverlay` for this change to limit churn, but
current documentation must describe it as a windowless scene overlay rather
than a viewport window.

### Toolbar State

Split the toolbar into three visible groups.

1. Editor mode group
   - Current mode set contains only `Selection`.
   - It controls left-click scene editing behavior.
   - It does not control camera behavior.

2. Camera control group
   - Contains `Orbit` and `FreeFly`.
   - Exactly one option is selected at all times.
   - Switching calls `CameraRig::setMode(...)`.
   - Switching keeps the current view continuous by using existing camera-rig
     synchronization behavior.

3. Functional button group
   - `reset editor cam`
   - `preview`
   - `debug`
   - `preference`

`preview` suppresses editor interactions while enabled, but it does not mutate
the selected editor mode or camera control mode.

## Input Rules

### Left Mouse

Left mouse belongs to the active editor mode and to gizmo interaction.

Priority:

1. If preview is enabled, no editor interaction runs.
2. If ImGui captures mouse because the cursor is over a panel, scene left-click
   interaction does not run.
3. If the cursor is inside the scene rect and ImGuizmo is hovered or active,
   left mouse is consumed by ImGuizmo.
4. Otherwise, left mouse is routed to the current editor mode.
5. In the current `Selection` mode, left click selects a hit node and empty
   space deselects.

Gizmo hover/use must suppress selection and box selection for that frame.

### Right Mouse

Right mouse belongs to camera controls.

- Orbit uses right-drag for rotate and wheel for zoom in the editor's selection
  mode.
- FreeFly uses right-hold mouse look plus movement keys.
- Right mouse does not pick, box select, or start gizmo manipulation.

### Keyboard Capture

Keyboard focus inside another ImGui panel does not block mouse scene
interaction by itself. Mouse capture blocks scene mouse interaction. Preview
blocks all editor interaction.

## Commands And API

The command/state surface must stop representing camera type as editor mode.

### Mode Command

`mode` only manages editor mode:

- `mode`
- `mode status`
- `mode selection`

`mode orbit` and `mode freefly` fail with a clear error that tells users to use
the camera-control command instead.

### Camera Control Command

Add a command for camera control mode under the existing `cam` command
namespace:

```text
cam control orbit
cam control freefly
cam control status
```

This keeps camera-control mode separate from editor mode while staying
consistent with existing commands such as `cam reset-editor-to-game`.

### Structured State

Toolbar and summary JSON must expose both fields:

```json
{
  "mode": "selection",
  "camera": "freefly",
  "previewEnabled": false,
  "debugEnabled": false
}
```

API and MCP toolbar snapshots must carry the same split state. Existing
single-field `editMode` protocol naming must be replaced or extended so callers
can no longer confuse `freefly` with an editor mode.

## Cleanup Scope

Remove maintained references to the fake viewport window:

- Remove `UiOverlay` dependence on `ViewportOverlay::getPanelRect()` as the
  primary scene rect source.
- Remove `UiOverlay::drawFrame()` calls that create or sync the `Viewport`
  window.
- Remove default `"Viewport"` layout generation and syncing.
- Existing persisted `"Viewport"` layout entries may be ignored rather than
  migrated.
- Update tests that currently assert a `Viewport` ImGui window exists.
- Update current specs/docs that describe `Selection`, `Orbit`, and `FreeFly`
  as mutually exclusive editor modes.

Do not edit archived historical requirement text unless it is part of a current
index or active guidance page. Historical documents can remain historical.

## Testing Plan

Focused C++ integration tests should cover:

- Toolbar state starts as `mode=selection` and `camera=orbit` or the chosen
  default camera mode.
- `mode orbit` and `mode freefly` return errors.
- Camera control command switches only the camera control mode.
- `state toolbar` and API toolbar snapshots include both mode and camera fields.
- No `Viewport` ImGui window is created by `UiOverlay::drawFrame()`.
- `sceneViewRect(windowSize)` returns a valid full-window rect without a
  viewport window.
- Gizmo hover/use suppresses selection click dispatch.
- Non-gizmo left click still performs selection.
- Right mouse routes to camera input and does not perform picking.
- Preview suppresses selection, gizmo, and camera interaction.

Build verification should include at least:

```bash
cmake --build build --target lxe_editor test_lxe_editor_layout test_lxe_editor_interaction test_viewport_overlay test_command_bus test_lxe_editor_api_service -j4
ctest --test-dir build --output-on-failure -R 'test_(lxe_editor_layout|lxe_editor_interaction|viewport_overlay|command_bus|lxe_editor_api_service)$'
```

If the build tree lacks some targets, configure first with the repository's
standard CMake/Ninja workflow.

## Documentation Updates

Update current docs/specs that define maintained behavior:

- `openspec/specs/lxe-editor/spec.md`
- `src/demos/lxe_editor/README.md`
- `notes/subsystems/scene.md`
- Any current command/API docs that mention toolbar mode or viewport overlay
  semantics.

Required documentation facts after this change:

- Editor mode and camera control mode are orthogonal.
- Current editor mode set contains only `Selection`.
- Camera control mode is `Orbit` or `FreeFly`.
- The gizmo is drawn as a windowless overlay on the scene rect.
- There is no maintained `Viewport` ImGui window in the editor.

## Open Decisions Resolved

- Use ImGuizmo overlay, not engine-rendered gizmo geometry.
- Keep scene rect abstraction, but return full window for now.
- Give gizmo left-click priority over scene selection.
- Do not preserve `mode orbit/freefly` compatibility.
- Clean current code/docs/tests rather than only hiding the viewport window.
