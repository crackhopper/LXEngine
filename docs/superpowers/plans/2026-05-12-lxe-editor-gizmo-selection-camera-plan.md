# lxe_editor Gizmo / Selection / Camera Coexistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore visible, interactive viewport gizmos in `lxe_editor`, and make selection-mode left-click selection coexist with right-button Orbit/FreeFly camera navigation.

**Architecture:** The current repo already contains a working `ViewportOverlay` implementation plus tests, but the runtime UI path never instantiates or draws it. The fix is to wire `ViewportOverlay` into `LxeEditorSession` and `UiOverlay`, route gizmo hotkeys through the active UI overlay, and replace the current mutually-exclusive selection-vs-camera update rule with a selection-mode input adapter that preserves left-button selection while forwarding right-button navigation to the existing camera rig.

**Tech Stack:** C++20, Dear ImGui, ImGuizmo, existing `CommandBus`, existing `CameraRig`, existing integration-test binaries under `src/test/integration/`

---

### Task 1: Reproduce the missing viewport overlay at the UI level

**Files:**
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`
- Inspect: `src/demos/lxe_editor/ui_overlay.hpp`
- Inspect: `src/demos/lxe_editor/editor_session.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void testUiOverlayCreatesViewportWindowAndSceneRect() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] viewport window layout test\n";
    ImGui::DestroyContext();
    return;
  }

  UiHarness harness;
  ImGui::NewFrame();
  harness.ui.drawFrame();

  ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");
  EXPECT(viewport != nullptr, "viewport window should exist");

  const auto rect =
      harness.ui.sceneViewRect(LX_core::Vec2f{1280.0f, 720.0f});
  EXPECT(rect.isValid(), "scene view rect should be valid");
  EXPECT(rect.width > 0.0f && rect.height > 0.0f,
         "scene view rect should have positive extent");

  ImGui::EndFrame();
  ImGui::DestroyContext();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_lxe_editor_layout -j4 && ./build/src/test/test_lxe_editor_layout`
Expected: FAIL because `Viewport` window is never created by `UiOverlay::drawFrame()`

- [ ] **Step 3: Write minimal implementation**

```cpp
// UiOverlay gains a ViewportOverlay binding and drawFrame() places it in the
// center region before computing m_sceneViewRect.
if (m_viewportOverlay) {
  ImGui::SetNextWindowPos(...);
  ImGui::SetNextWindowSize(...);
  m_viewportOverlay->get().draw();
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/src/test/test_lxe_editor_layout`
Expected: PASS and `Viewport` exists

- [ ] **Step 5: Commit**

```bash
git add src/test/integration/test_lxe_editor_layout.cpp src/demos/lxe_editor/ui_overlay.hpp src/demos/lxe_editor/ui_overlay.cpp src/demos/lxe_editor/editor_session.hpp src/demos/lxe_editor/editor_session.cpp
git commit -m "fix: wire viewport overlay into lxe editor ui"
```

### Task 2: Restore selection-mode camera coexistence with right-button navigation

**Files:**
- Create: `src/demos/lxe_editor/selection_camera_input.hpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/demos/lxe_editor/ui_overlay.hpp`
- Modify: `src/demos/lxe_editor/ui_overlay.cpp`
- Modify: `src/test/integration/test_lxe_editor_interaction.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void testSelectionModeStillAllowsRightButtonCameraRigRouting() {
  EXPECT(LX_demo::lxe_editor::shouldProcessSelectionMode(
             false, false, LX_demo::lxe_editor::SceneInputEditMode::Selection),
         "selection mode should still process selection");
  EXPECT(LX_demo::lxe_editor::shouldProcessCameraRig(
             false, false, false,
             LX_demo::lxe_editor::SceneInputEditMode::Selection),
         "selection mode should still allow camera rig updates");
}
```

```cpp
void testSelectionCameraInputMapsRightMouseToOrbitRotate() {
  LX_core::MockInputState input;
  input.setMouseButtonDown(LX_core::MouseButton::Right, true);
  input.setMouseDelta({15.0f, -8.0f});

  LX_demo::lxe_editor::SelectionCameraInput orbitInput(
      input, LX_demo::lxe_editor::SelectionNavigationMode::Orbit);
  EXPECT(orbitInput.isMouseButtonDown(LX_core::MouseButton::Left),
         "selection orbit input should map right mouse to orbit rotate");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_lxe_editor_interaction -j4 && ./build/src/test/test_lxe_editor_interaction`
Expected: FAIL because selection mode currently blocks camera rig routing and no selection-camera adapter exists

- [ ] **Step 3: Write minimal implementation**

```cpp
class SelectionCameraInput final : public LX_core::IInputState {
public:
  SelectionCameraInput(const LX_core::IInputState& base,
                       SelectionNavigationMode mode);
  bool isMouseButtonDown(LX_core::MouseButton button) const override;
  bool isKeyDown(LX_core::KeyCode code) const override;
  // forward mouse pos/delta/wheel, suppress left-button selection collisions
};
```

```cpp
if (inputMode == demo::SceneInputEditMode::Selection) {
  demo::SelectionCameraInput selectionNavInput(
      *input, ui.selectionNavigationMode());
  rig.update(selectionNavInput, clock.deltaTime());
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/src/test/test_lxe_editor_interaction`
Expected: PASS and selection mode no longer excludes camera rig routing

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/selection_camera_input.hpp src/demos/lxe_editor/main.cpp src/demos/lxe_editor/ui_overlay.hpp src/demos/lxe_editor/ui_overlay.cpp src/test/integration/test_lxe_editor_interaction.cpp
git commit -m "fix: allow right-button camera navigation in selection mode"
```

### Task 3: Hook gizmo hotkeys and runtime viewport interaction through the same overlay

**Files:**
- Modify: `src/demos/lxe_editor/ui_overlay.hpp`
- Modify: `src/demos/lxe_editor/ui_overlay.cpp`
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/test/integration/test_viewport_overlay.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void testUiOverlayRoutesGizmoHotkeysToViewportOverlay() {
  if (!setupMinimalImGui()) {
    std::cout << "[SKIP] gizmo hotkey ui test\n";
    ImGui::DestroyContext();
    return;
  }

  Fixture fixture;
  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState, *fixture.scene);
  LX_demo::lxe_editor::UiOverlay ui;
  // attach overlay, then press E
  LX_core::MockInputState input;
  input.setKeyDown(LX_core::KeyCode::E, true);
  ui.handleHotkeys(input);
  EXPECT(overlay.getGizmoOperation() ==
             LX_core::ViewportOverlay::GizmoOperation::Rotate,
         "E should switch gizmo mode to rotate through UiOverlay");
  ImGui::DestroyContext();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_viewport_overlay test_lxe_editor_layout -j4 && ./build/src/test/test_viewport_overlay`
Expected: FAIL because `UiOverlay` does not own or forward hotkeys to `ViewportOverlay`

- [ ] **Step 3: Write minimal implementation**

```cpp
if (m_viewportOverlay && wDown && !m_prevWDown) {
  (void)m_viewportOverlay->get().handleGizmoHotkeys('W');
}
if (m_viewportOverlay && eDown && !m_prevEDown) {
  (void)m_viewportOverlay->get().handleGizmoHotkeys('E');
}
if (m_viewportOverlay && rDown && !m_prevRDown) {
  (void)m_viewportOverlay->get().handleGizmoHotkeys('R');
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/src/test/test_viewport_overlay && ./build/src/test/test_lxe_editor_layout`
Expected: PASS and gizmo mode switches through the live UI path

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/ui_overlay.hpp src/demos/lxe_editor/ui_overlay.cpp src/demos/lxe_editor/editor_session.hpp src/demos/lxe_editor/editor_session.cpp src/test/integration/test_viewport_overlay.cpp src/test/integration/test_lxe_editor_layout.cpp
git commit -m "fix: route gizmo viewport interactions through ui overlay"
```

### Task 4: Full verification

**Files:**
- Verify only

- [ ] **Step 1: Build focused targets**

Run: `cmake --build build --target lxe_editor test_viewport_overlay test_lxe_editor_layout test_lxe_editor_interaction -j4`
Expected: build succeeds

- [ ] **Step 2: Run focused tests**

Run: `./build/src/test/test_viewport_overlay`
Expected: PASS

Run: `./build/src/test/test_lxe_editor_layout`
Expected: PASS

Run: `./build/src/test/test_lxe_editor_interaction`
Expected: PASS

- [ ] **Step 3: Run aggregated ctest slice**

Run: `ctest --test-dir build --output-on-failure -R 'test_(viewport_overlay|lxe_editor_layout|lxe_editor_interaction)$'`
Expected: all selected tests pass

- [ ] **Step 4: Manual runtime smoke**

Run: `./build/src/demos/lxe_editor/lxe_editor`
Expected:
- `Viewport` window visible
- left-click mesh selects node and shows gizmo
- left-drag gizmo manipulates selected node
- `W/E/R` switch translate/rotate/scale
- right-drag navigates camera while in selection mode
- `F` preview still suppresses editor overlay

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix: restore lxe editor gizmo viewport interaction"
```
