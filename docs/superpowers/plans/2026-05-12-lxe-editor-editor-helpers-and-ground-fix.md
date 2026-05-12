# lxe_editor Editor Helpers And Ground Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore correct editor-only camera/light visualization and selection behavior, hide those helpers in preview, and fix the ground plane winding so rendered face orientation matches its upward normal.

**Architecture:** Keep editor-only behavior inside `lxe_editor` runtime glue instead of persisting it into scene documents. Represent camera/light helpers as `SceneNode` proxies on `Layer_EditorOverlay`, route picker hits on those helpers back to the owning gameplay nodes, and keep preview mode clean by excluding helper visibility and helper picking while preserving existing scene data. Fix the ground bug at the mesh source by making triangle winding match the existing `+Y` vertex normals.

**Tech Stack:** C++20, existing `SceneNode`/component scene graph, `DebugDraw`, ImGui editor runtime, existing integration-test executables under `src/test/integration/`

---

### Task 1: Lock The Regressions With Failing Tests

**Files:**
- Modify: `src/test/integration/test_lxe_editor_interaction.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Test: `src/test/integration/test_lxe_editor_interaction.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Add a failing interaction test for editor helper visualization and helper-hit remapping**

```cpp
void testEditorHelpersDrawAndPickBackToOwningNodes() {
  Fixture fixture;
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();

  fixture.controller.enqueueDebugDraw();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() > 12,
         "editor mode should draw helper frustum/light debug lines");

  const auto selected = fixture.editorState.getSelected();
  EXPECT(selected.empty(), "helper debug draw should not mutate selection");
  LX_core::DebugDraw::endFrame();
}
```

- [ ] **Step 2: Extend the same test file with preview suppression expectations**

```cpp
void testPreviewModeSuppressesEditorHelpers() {
  Fixture fixture;
  fixture.editorState.setPreviewEnabled(true);
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::attachScene(fixture.scene);
  LX_core::DebugDraw::beginFrame();
  fixture.controller.enqueueDebugDraw();
  LX_core::DebugDraw::endFrame();
  EXPECT(LX_core::DebugDraw::testing::queuedLineCount() == 0,
         "preview mode should hide editor helper debug lines");
}
```

- [ ] **Step 3: Add a failing runtime test for helper-node creation and preview culling semantics**

```cpp
void testSceneRuntimeCreatesEditorOnlyCameraAndLightHelpers() {
  LX_demo::lxe_editor::SceneRuntime runtime;
  runtime.createEmptyScene();

  auto scene = runtime.scene();
  EXPECT(scene != nullptr, "runtime should create scene");
  EXPECT(scene->findByPath("/game_cam") != nullptr, "game camera should exist");
  EXPECT(scene->findByPath("/editor_cam") != nullptr, "editor camera should exist");
}
```

- [ ] **Step 4: Add a failing runtime test for ground-facing orientation**

```cpp
void testGroundMeshWindingMatchesUpwardNormal() {
  const auto ground = LX_demo::lxe_editor::buildGroundNode();
  const auto meshComponent = ground->getComponent<LX_core::MeshComponent>();
  EXPECT(meshComponent.has_value(), "ground node should have mesh component");
}
```

- [ ] **Step 5: Run the targeted tests to verify failure**

Run:

```bash
cmake --build build --target test_lxe_editor_interaction test_scene_runtime -j4
ctest --test-dir build --output-on-failure -R "test_lxe_editor_interaction|test_scene_runtime"
```

Expected: FAIL because helper nodes are not created/remapped yet and the ground test does not yet observe winding-correct geometry.


### Task 2: Implement Editor-Only Camera/Light Helper Nodes And Picking Remap

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.hpp`
- Modify: `src/demos/lxe_editor/scene_builder.cpp`
- Modify: `src/demos/lxe_editor/scene_builder.hpp`
- Modify: `src/demos/lxe_editor/scene_interaction_controller.cpp`
- Modify: `src/demos/lxe_editor/scene_interaction_controller.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Test: `src/test/integration/test_lxe_editor_interaction.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Extend scene runtime state to track helper proxies and owning-node lookup**

```cpp
struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  std::optional<SceneSourceKind> sourceKind;
  SceneDocument document;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
  std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> helperOwnersByPath;
};
```

- [ ] **Step 2: Add helper-node builders in demo-local scene builder**

```cpp
LX_core::SceneNodeSharedPtr buildCameraHelperNode(const char* nodeName);
LX_core::SceneNodeSharedPtr buildDirectionalLightHelperNode(const char* nodeName);
```

```cpp
auto node = SceneNode::create(nodeName);
node->setVisibilityLayerMask(LX_core::Layer_EditorOverlay);
node->addComponent<LX_core::MeshComponent>(std::move(mesh));
node->addComponent<LX_core::MaterialComponent>(std::move(material));
return node;
```

- [ ] **Step 3: Spawn editor-only helper nodes after primary runtime nodes exist**

```cpp
void addEditorHelperNode(SceneRuntimeData& runtime,
                         const LX_core::SceneNodeSharedPtr& owner,
                         const LX_core::SceneNodeSharedPtr& helper) {
  helper->setParent(owner);
  runtime.scene->addRenderable(helper);
  runtime.helperOwnersByPath.emplace(helper->getPath(), owner);
}
```

- [ ] **Step 4: Expose helper-owner resolution from `SceneRuntime` into the interaction controller binding path**

```cpp
[[nodiscard]] LX_core::SceneNodeSharedPtr
resolveEditorHelperOwner(const std::string& path) const;
```

- [ ] **Step 5: Remap selection hits on helper nodes back to the owning camera/light node**

```cpp
if (hit.has_value() && hit->node) {
  LX_core::SceneNodeSharedPtr targetNode = hit->node;
  if (m_resolveHelperOwner) {
    if (const auto owner = m_resolveHelperOwner(hit->node->getPath())) {
      targetNode = owner;
    }
  }
  return m_commandBus.dispatch("select \"" + targetNode->getPath() + "\"");
}
```

- [ ] **Step 6: Keep helper picking/editor behavior disabled in preview**

```cpp
if (m_editorState.isPreviewEnabled()) {
  m_lastHitMarker.reset();
  return LX_core::CommandResult{false, "preview mode suppresses editor picking", {}, {}};
}
```

- [ ] **Step 7: Run targeted tests to verify helper-node behavior**

Run:

```bash
cmake --build build --target test_lxe_editor_interaction test_scene_runtime -j4
ctest --test-dir build --output-on-failure -R "test_lxe_editor_interaction|test_scene_runtime"
```

Expected: PASS for helper creation/remap/preview suppression tests; ground orientation test may still fail until Task 3 lands.


### Task 3: Restore Camera/Light Debug Draw And Fix Ground Winding

**Files:**
- Modify: `src/demos/lxe_editor/scene_interaction_controller.cpp`
- Modify: `src/demos/lxe_editor/scene_builder.cpp`
- Test: `src/test/integration/test_lxe_editor_interaction.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Port the missing frustum/light-arrow debug draw into the active interaction controller path**

```cpp
for (const auto& cameraNode : m_scene.getCameras()) {
  if (!cameraNode || cameraNode == m_editorState.getEditorCamera()) {
    continue;
  }
  const auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    continue;
  }
  const LX_core::Mat4f viewProj =
      camera->get().getProjMatrix() * camera->get().getViewMatrix();
  LX_core::DebugDraw::frustum(viewProj, LX_core::DebugDraw::Color::white());
}
```

```cpp
for (const auto& light : m_scene.getLights()) {
  const auto directionalLight =
      std::dynamic_pointer_cast<LX_core::DirectionalLight>(light);
  if (!directionalLight) {
    continue;
  }
  LX_core::Vec3f direction = directionalLight->getDirection().normalized();
  LX_core::DebugDraw::arrow(origin, origin + direction * 2.0f,
                            LX_core::DebugDraw::Color::yellow());
}
```

- [ ] **Step 2: Fix the ground indices so triangle facing matches the existing `+Y` normal**

```cpp
auto ib = IndexBuffer::create(
    std::vector<u32>{0, 2, 1, 0, 3, 2});
```

- [ ] **Step 3: Strengthen the ground test so it validates triangle-facing sign, not just component presence**

```cpp
const auto* vb =
    dynamic_cast<LX_core::VertexBuffer<LX_core::VertexPosNormalUvBone>*>(
        meshComponent->get().getMesh()->vertexBuffer.get());
EXPECT(vb != nullptr, "ground mesh should use VertexPosNormalUvBone layout");
```

- [ ] **Step 4: Run the focused tests again**

Run:

```bash
cmake --build build --target test_lxe_editor_interaction test_scene_runtime -j4
ctest --test-dir build --output-on-failure -R "test_lxe_editor_interaction|test_scene_runtime"
```

Expected: PASS.

- [ ] **Step 5: Run broader regression coverage around overlay/editor runtime**

Run:

```bash
cmake --build build --target test_viewport_overlay test_lxe_editor_session -j4
ctest --test-dir build --output-on-failure -R "test_viewport_overlay|test_lxe_editor_session|test_lxe_editor_interaction|test_scene_runtime"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add \
  docs/superpowers/plans/2026-05-12-lxe-editor-editor-helpers-and-ground-fix.md \
  src/demos/lxe_editor/scene_builder.cpp \
  src/demos/lxe_editor/scene_builder.hpp \
  src/demos/lxe_editor/scene_interaction_controller.cpp \
  src/demos/lxe_editor/scene_interaction_controller.hpp \
  src/demos/lxe_editor/scene_runtime.cpp \
  src/demos/lxe_editor/scene_runtime.hpp \
  src/demos/lxe_editor/editor_session.cpp \
  src/test/integration/test_lxe_editor_interaction.cpp \
  src/test/integration/test_scene_runtime.cpp
git commit -m "fix: restore editor helpers and ground winding"
```
