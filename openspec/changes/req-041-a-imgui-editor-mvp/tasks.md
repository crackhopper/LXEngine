## 1. OpenSpec And Scene Tree Foundation

- [x] 1.1 Create the `req-041-a-imgui-editor-mvp` OpenSpec change skeleton (proposal/design/tasks).
- [x] 1.2 Add `src/core/editor/scene_tree_panel.hpp/.cpp` with path jump input, recursive scene tree rendering, and command-bus selection/remove dispatch helpers.
- [x] 1.3 Add headless integration coverage for `SceneTreePanel` path jump / selection command behavior.

## 2. Inspector And Viewport Overlay

- [x] 2.1 Add `inspector_panel` with selected-node field display and command-bus-backed edit submission.
- [x] 2.2 Add ImGuizmo adapter + `viewport_overlay` skeleton with gizmo operation mode state and debug-draw integration points.
- [x] 2.3 Extend camera active-state / preview toggling path shared by F-key and `preview` command.

## 3. Demo Wiring And Verification

- [x] 3.1 Wire scene tree / inspector / console / viewport overlay into `lxe_editor` with initial editor/game cameras.
- [x] 3.2 Add or extend smoke coverage for editor MVP flows where practical.
- [x] 3.3 Run required configure/build/ctest matrix, then commit `Implement REQ-041-a: imgui-editor-mvp`.
