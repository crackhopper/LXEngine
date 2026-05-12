# Observable Camera And Light Properties Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make direct camera/light property writes emit runtime scene events by moving `CameraComponent` and `DirectionalLight` to observable setter-based mutation APIs and migrating existing callers off raw field writes.

**Architecture:** Keep the scope bounded to scene-facing camera/light state, but implement it in a reusable observer-backed way instead of command-only patches. `CameraComponent` emits through its owning `SceneNode`; `DirectionalLight` tracks its attached `Scene` + `SceneNode` through the existing `Scene::attachLight(...)` relationship and emits `SceneNodeChanged` with `CameraProperties` / `LightProperties` aspects from the real write points.

**Tech Stack:** C++20, existing `Scene` / `SceneNode` / `IComponent` ownership model, current runtime scene event hub, CMake/Ninja integration tests under `src/test/integration/`

---

## File Structure

### New files

- None required if the implementation stays inside the current scene/component files.

### Modified files

- `src/core/scene/component.hpp`
  - Add a protected helper for non-structural runtime property notifications from components.
- `src/core/scene/component.cpp`
  - Implement the helper by routing through the attached owner node and current scene event hub.
- `src/core/scene/components/camera_component.hpp`
  - Replace public mutable camera fields with private state plus getters/setters for observable writes.
- `src/core/scene/components/camera_component.cpp`
  - Implement the new setter surface, matrix updates, and event emission for attached nodes.
- `src/core/scene/light.hpp`
  - Replace direct mutable directional-light payload access with explicit getters/setters and scene/node attachment hooks.
- `src/core/scene/light.cpp`
  - Implement attachment bookkeeping and event-emitting setters.
- `src/core/scene/scene.hpp`
  - Update light attachment APIs to register/unregister the `DirectionalLight` scene/node observer link.
- `src/core/scene/scene.cpp`
  - Wire the new light attachment bookkeeping into `attachLight`, `detachLight`, `removeLight`, and subtree removal.
- `src/core/editor/commands/builtin_commands.cpp`
  - Stop direct field writes for camera/light properties and remove now-redundant command-local manual event emission.
- `src/core/editor/inspector_panel.cpp`
  - Update any camera/light reads to the new getter API.
- `src/infra/gui/debug_ui.cpp`
  - Stop mutating `camera.fovY`, `camera.nearPlane`, `light.ubo->param`, etc. directly.
- `src/demos/lxe_editor/scene_runtime.cpp`
  - Convert camera/light document application to use setter-based APIs.
- `src/demos/lxe_editor/editor_camera_state.cpp`
  - Convert capture/apply helpers to the new camera getter/setter surface.
- `src/demos/lxe_editor/lxe_editor_api_service.cpp`
  - Keep existing runtime event mirroring; only touch if serialization needs new aspect strings.
- `src/test/integration/test_scene_events.cpp`
  - Add direct runtime camera/light mutation event tests.
- `src/test/integration/test_inspector_panel.cpp`
  - Add regressions for out-of-band camera/light edits refreshing inspector snapshots/drafts.
- `src/test/integration/test_lxe_editor_api_service.cpp`
  - Add regressions for out-of-band camera/light edits mirroring into API `scene_node.changed`.
- `src/test/integration/test_scene_runtime.cpp`
  - Update direct field writes in fixtures/helpers to use the new camera/light setter APIs.
- `src/test/integration/test_command_bus.cpp`
  - Update light/camera fixture setup to the new APIs and keep command-side event regressions passing.

### Existing files to read before editing

- `src/core/scene/component.hpp`
- `src/core/scene/component.cpp`
- `src/core/scene/components/camera_component.hpp`
- `src/core/scene/components/camera_component.cpp`
- `src/core/scene/light.hpp`
- `src/core/scene/light.cpp`
- `src/core/scene/scene.hpp`
- `src/core/scene/scene.cpp`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/infra/gui/debug_ui.cpp`
- `src/demos/lxe_editor/scene_runtime.cpp`
- `src/demos/lxe_editor/editor_camera_state.cpp`
- `src/test/integration/test_scene_events.cpp`
- `src/test/integration/test_inspector_panel.cpp`
- `src/test/integration/test_lxe_editor_api_service.cpp`
- `docs/superpowers/specs/2026-05-11-scene-runtime-editor-event-design.md`

## Task 1: Add Component/Light Runtime Property Notification Hooks

**Files:**
- Modify: `src/core/scene/component.hpp`
- Modify: `src/core/scene/component.cpp`
- Modify: `src/core/scene/light.hpp`
- Modify: `src/core/scene/light.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Test: `src/test/integration/test_scene_events.cpp`

- [ ] **Step 1: Write the failing runtime-property tests**

Add these tests to `src/test/integration/test_scene_events.cpp` near the existing direct mutation coverage:

```cpp
void testCameraComponentPropertySettersEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent& event) {
        events.push_back(captureEvent(event));
      });

  auto cameraNode = LX_core::SceneNode::create("camera_node");
  cameraNode->setName("camera");
  auto camera = cameraNode->addComponent<LX_core::CameraComponent>();
  EXPECT(camera.has_value(), "camera component should exist");
  if (!camera.has_value()) {
    return;
  }

  scene->addCamera(cameraNode);
  events.clear();

  camera->get().setFovY(75.0f);
  camera->get().setNearPlane(0.5f);
  camera->get().setFarPlane(250.0f);
  camera->get().setProjectionType(LX_core::CameraType::Orthographic);
  camera->get().setCullingMask(0x0Fu);

  EXPECT(countChangedEventsWithAspect(events,
                                      LX_core::SceneNodeAspect::CameraProperties) == 5,
         "camera property setters should each emit CameraProperties events");
}

void testDirectionalLightPropertySettersEmitRuntimeEvents() {
  auto scene = LX_core::Scene::create(nullptr);
  std::vector<CapturedEvent> events;
  auto subscription =
      scene->events().subscribe([&](const LX_core::SceneEvent& event) {
        events.push_back(captureEvent(event));
      });

  auto lightNode = LX_core::SceneNode::create("light_node");
  lightNode->setName("sun");
  scene->addRenderable(lightNode);

  auto light = std::make_shared<LX_core::DirectionalLight>();
  scene->attachLight(lightNode, light);
  events.clear();

  light->setDirection({0.0f, -1.0f, 0.0f});
  light->setColor({0.2f, 0.4f, 0.6f});
  light->setIntensity(3.5f);

  EXPECT(countChangedEventsWithAspect(events,
                                      LX_core::SceneNodeAspect::LightProperties) == 3,
         "light property setters should each emit LightProperties events");
}
```

- [ ] **Step 2: Run the focused test target and verify the new tests fail**

Run: `cmake --build build --target test_scene_events -j4 && ./build/src/test/test_scene_events`

Expected: FAIL with compile errors for missing `setFovY` / `setNearPlane` / `setDirection` / `setIntensity`, or runtime failures because no direct camera/light property events are emitted yet.

- [ ] **Step 3: Add a reusable component-side runtime event helper**

Modify `src/core/scene/component.hpp`:

```cpp
#include "core/scene/scene_events.hpp"

class IComponent {
public:
  ...

protected:
  IComponent() = default;

  void notifyOwnerStructuralChange() const;
  void notifyOwnerRuntimeAspectChange(SceneNodeAspect aspect) const;
  virtual void onAttached() {}
  virtual void onDetaching() {}
```

Modify `src/core/scene/component.cpp`:

```cpp
void IComponent::notifyOwnerRuntimeAspectChange(const SceneNodeAspect aspect) const {
  const auto ownerNode = owner();
  if (!ownerNode.has_value()) {
    return;
  }

  if (const auto scene = ownerNode->get().getAttachedScene()) {
    scene->events().emit(SceneEvent{
        .domain = SceneEventDomain::Runtime,
        .type = SceneEventType::SceneNodeChanged,
        .path = ownerNode->get().getPath(),
        .stableNodeName = ownerNode->get().getNodeName(),
        .aspects = {aspect},
    });
  }
}
```

- [ ] **Step 4: Add directional-light attachment bookkeeping hooks**

Modify `src/core/scene/light.hpp` to give `DirectionalLight` an attachment-aware observer surface:

```cpp
class DirectionalLight : public LightBase {
public:
  DirectionalLight();

  [[nodiscard]] Vec3f getDirection() const;
  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  void setDirection(const Vec3f& direction);
  void setColor(const Vec3f& color);
  void setIntensity(float intensity);

  void attachToSceneNode(const std::weak_ptr<Scene>& scene,
                         const std::weak_ptr<SceneNode>& node);
  void detachFromSceneNode();

  IGpuResourceSharedPtr getUBO() const override { return m_ubo; }
  bool supportsPass(StringID pass) const override;
  void setSupportedPasses(std::initializer_list<StringID> passes);
  void setSupportedPasses(const std::vector<StringID>& passes);

private:
  void emitLightPropertyChanged() const;

  DirectionalLightDataSharedPtr m_ubo;
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_node;
};
```

Modify `src/core/scene/light.cpp` to implement the setters:

```cpp
void DirectionalLight::setDirection(const Vec3f& direction) {
  m_ubo->param.dir = Vec4f{direction.x, direction.y, direction.z, 0.0f};
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setColor(const Vec3f& color) {
  m_ubo->param.color = Vec4f{color.x, color.y, color.z, m_ubo->param.color.w};
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::setIntensity(const float intensity) {
  m_ubo->param.color.w = intensity;
  m_ubo->setDirty();
  emitLightPropertyChanged();
}

void DirectionalLight::emitLightPropertyChanged() const {
  const auto scene = m_scene.lock();
  const auto node = m_node.lock();
  if (!scene || !node) {
    return;
  }

  scene->events().emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = SceneEventType::SceneNodeChanged,
      .path = node->getPath(),
      .stableNodeName = node->getNodeName(),
      .aspects = {SceneNodeAspect::LightProperties},
  });
}
```

- [ ] **Step 5: Wire scene light attach/detach to the new light hooks**

Modify `src/core/scene/scene.cpp` inside `attachLight`, `detachLight`, and `removeLight`:

```cpp
void Scene::attachLight(const SceneNodeSharedPtr& node,
                        const LightBaseSharedPtr& light) {
  ...
  if (const auto directional = std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directional->attachToSceneNode(shared_from_this(), node);
  }
  m_lightsByNode[node.get()] = light;
  node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
}

LightBaseSharedPtr Scene::detachLight(const SceneNodeSharedPtr& node) {
  ...
  if (const auto directional = std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directional->detachFromSceneNode();
  }
  removeLight(light);
  ...
}

void Scene::removeLight(const LightBaseSharedPtr& light) {
  if (const auto directional = std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directional->detachFromSceneNode();
  }
  ...
}
```

- [ ] **Step 6: Run the scene event tests and commit**

Run:

```bash
cmake --build build --target test_scene_events -j4
./build/src/test/test_scene_events
```

Expected: PASS

Commit:

```bash
git add src/core/scene/component.hpp src/core/scene/component.cpp \
        src/core/scene/light.hpp src/core/scene/light.cpp \
        src/core/scene/scene.hpp src/core/scene/scene.cpp \
        src/test/integration/test_scene_events.cpp
git commit -m "feat: add runtime property event hooks"
```

## Task 2: Convert CameraComponent To Observable Setters

**Files:**
- Modify: `src/core/scene/components/camera_component.hpp`
- Modify: `src/core/scene/components/camera_component.cpp`
- Modify: `src/demos/lxe_editor/editor_camera_state.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/core/editor/inspector_panel.cpp`
- Test: `src/test/integration/test_scene_events.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Expand the failing tests to require the getter/setter camera API everywhere**

Update `src/test/integration/test_scene_runtime.cpp` fixture writes such as:

```cpp
gameCamera->get().fovY = 60.0f;
gameCamera->get().nearPlane = 0.5f;
gameCamera->get().farPlane = 250.0f;
```

to the intended API:

```cpp
gameCamera->get().setFovY(60.0f);
gameCamera->get().setNearPlane(0.5f);
gameCamera->get().setFarPlane(250.0f);
```

and update direct reads such as:

```cpp
EXPECT(loaded.editorCamera().fovY == 35.0f, ...);
```

to:

```cpp
EXPECT(loaded.editorCamera().getFovY() == 35.0f, ...);
```

- [ ] **Step 2: Run the camera-related targets and verify compile-time failure**

Run:

```bash
cmake --build build --target test_scene_runtime test_scene_events -j4
```

Expected: FAIL with missing `getFovY`, `setFovY`, `getNearPlane`, `setProjectionType`, etc.

- [ ] **Step 3: Replace public camera fields with private state plus explicit accessors**

Modify `src/core/scene/components/camera_component.hpp`:

```cpp
class CameraComponent final : public IComponent {
public:
  ...
  [[nodiscard]] CameraType getProjectionType() const { return m_type; }
  void setProjectionType(CameraType type);

  [[nodiscard]] float getFovY() const { return m_fovY; }
  void setFovY(float fovY);

  [[nodiscard]] float getAspect() const { return m_aspect; }
  void setAspect(float aspect);

  [[nodiscard]] float getNearPlane() const { return m_nearPlane; }
  void setNearPlane(float nearPlane);

  [[nodiscard]] float getFarPlane() const { return m_farPlane; }
  void setFarPlane(float farPlane);

  [[nodiscard]] float getLeft() const { return m_left; }
  [[nodiscard]] float getRight() const { return m_right; }
  [[nodiscard]] float getBottom() const { return m_bottom; }
  [[nodiscard]] float getTop() const { return m_top; }
  void setOrthographicBounds(float left, float right, float bottom, float top);
  void applyProjectionState(CameraType type, float fovY, float aspect,
                            float nearPlane, float farPlane,
                            float left, float right, float bottom, float top);
  ...
private:
  CameraType m_type = CameraType::Perspective;
  float m_fovY = 45.0f;
  float m_aspect = 16.0f / 9.0f;
  float m_nearPlane = 0.1f;
  float m_farPlane = 1000.0f;
  float m_left = -1.0f;
  float m_right = 1.0f;
  float m_bottom = -1.0f;
  float m_top = 1.0f;
```

- [ ] **Step 4: Implement event-emitting camera setters**

Modify `src/core/scene/components/camera_component.cpp`:

```cpp
void CameraComponent::setFovY(const float fovY) {
  m_fovY = fovY;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::setNearPlane(const float nearPlane) {
  m_nearPlane = nearPlane;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}

void CameraComponent::applyProjectionState(const CameraType type, const float fovY,
                                           const float aspect, const float nearPlane,
                                           const float farPlane, const float left,
                                           const float right, const float bottom,
                                           const float top) {
  m_type = type;
  m_fovY = fovY;
  m_aspect = aspect;
  m_nearPlane = nearPlane;
  m_farPlane = farPlane;
  m_left = left;
  m_right = right;
  m_bottom = bottom;
  m_top = top;
  updateMatrices();
  notifyOwnerRuntimeAspectChange(SceneNodeAspect::CameraProperties);
}
```

Use the private members inside `getProjMatrix(...)` and related helpers instead of the old public fields.

- [ ] **Step 5: Migrate existing camera callers**

Update:

- `src/demos/lxe_editor/editor_camera_state.cpp`

```cpp
return EditorCameraState{
    .position = node.getTranslation(),
    .rotationEulerDeg = quatToEulerDegrees(node.getRotation()),
    .fovY = camera.getFovY(),
    .nearPlane = camera.getNearPlane(),
    .farPlane = camera.getFarPlane(),
};
...
camera.setFovY(fovY);
camera.setNearPlane(nearPlane);
camera.setFarPlane(farPlane);
```

- `src/demos/lxe_editor/scene_runtime.cpp`

```cpp
camera.applyProjectionState(state.type, state.fovY, state.aspect,
                            state.nearPlane, state.farPlane,
                            state.left, state.right,
                            state.bottom, state.top);
```

- `src/core/editor/inspector_panel.cpp`

```cpp
snapshot.cameraFov = camera->get().getFovY();
snapshot.cameraNear = camera->get().getNearPlane();
snapshot.cameraFar = camera->get().getFarPlane();
snapshot.cameraPerspective =
    camera->get().getProjectionType() == CameraType::Perspective;
```

- [ ] **Step 6: Run the camera-focused tests and commit**

Run:

```bash
cmake --build build --target test_scene_runtime test_scene_events test_inspector_panel -j4
./build/src/test/test_scene_runtime
./build/src/test/test_scene_events
./build/src/test/test_inspector_panel
```

Expected: PASS

Commit:

```bash
git add src/core/scene/components/camera_component.hpp \
        src/core/scene/components/camera_component.cpp \
        src/demos/lxe_editor/editor_camera_state.cpp \
        src/demos/lxe_editor/scene_runtime.cpp \
        src/core/editor/inspector_panel.cpp \
        src/test/integration/test_scene_runtime.cpp \
        src/test/integration/test_scene_events.cpp
git commit -m "refactor: make camera properties observable"
```

## Task 3: Convert DirectionalLight To Observable Setters

**Files:**
- Modify: `src/core/scene/light.hpp`
- Modify: `src/core/scene/light.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/core/editor/inspector_panel.cpp`
- Modify: `src/core/editor/viewport_overlay.cpp`
- Test: `src/test/integration/test_scene_events.cpp`
- Test: `src/test/integration/test_inspector_panel.cpp`

- [ ] **Step 1: Write the failing light migration reads/writes**

Update direct payload access in tests to the target API, for example in `src/test/integration/test_inspector_panel.cpp`:

```cpp
dirLight->setDirection({-0.3f, -1.0f, -0.5f});
dirLight->setColor({0.9f, 0.8f, 0.7f});
dirLight->setIntensity(2.5f);
```

and assertion reads:

```cpp
EXPECT(nearlyEqual(fillLight->getIntensity(), 9.0f),
       "fixture fill light should preserve its own intensity");
```

- [ ] **Step 2: Run the light-related test targets and verify failure**

Run:

```bash
cmake --build build --target test_scene_events test_inspector_panel -j4
```

Expected: FAIL with missing `getDirection`, `setDirection`, `getColor`, `setIntensity`, or stale callers still using `ubo->param`.

- [ ] **Step 3: Finalize the public DirectionalLight API**

Modify `src/core/scene/light.hpp` to remove the public mutable `ubo` member and replace it with:

```cpp
class DirectionalLight : public LightBase {
public:
  DirectionalLight();

  [[nodiscard]] Vec3f getDirection() const;
  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  void setDirection(const Vec3f& direction);
  void setColor(const Vec3f& color);
  void setIntensity(float intensity);

  IGpuResourceSharedPtr getUBO() const override { return m_ubo; }
  [[nodiscard]] DirectionalLightDataSharedPtr getDirectionalUBO() const {
    return m_ubo;
  }
  ...
private:
  DirectionalLightDataSharedPtr m_ubo;
```

This keeps backend/resource access available without leaving the semantic light state publicly mutable.

- [ ] **Step 4: Migrate light callers to the new API**

Update:

- `src/demos/lxe_editor/scene_runtime.cpp`

```cpp
light.setDirection(state.direction);
light.setColor(state.color);
light.setIntensity(state.intensity);
```

- `src/core/editor/inspector_panel.cpp`

```cpp
snapshot.lightDirection = light->getDirection();
snapshot.lightColor = light->getColor();
snapshot.lightIntensity = light->getIntensity();
```

- `src/core/editor/viewport_overlay.cpp`

```cpp
Vec3f direction = directionalLight->getDirection();
```

- [ ] **Step 5: Run the light-focused tests and commit**

Run:

```bash
cmake --build build --target test_scene_events test_inspector_panel test_scene_runtime -j4
./build/src/test/test_scene_events
./build/src/test/test_inspector_panel
./build/src/test/test_scene_runtime
```

Expected: PASS

Commit:

```bash
git add src/core/scene/light.hpp src/core/scene/light.cpp \
        src/demos/lxe_editor/scene_runtime.cpp \
        src/core/editor/inspector_panel.cpp \
        src/core/editor/viewport_overlay.cpp \
        src/test/integration/test_scene_events.cpp \
        src/test/integration/test_inspector_panel.cpp
git commit -m "refactor: make directional light properties observable"
```

## Task 4: Migrate Command/UI Callers And Remove Redundant Manual Emission

**Files:**
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/infra/gui/debug_ui.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/test/integration/test_inspector_panel.cpp`
- Test: `src/test/integration/test_command_bus.cpp`
- Test: `src/test/integration/test_inspector_panel.cpp`

- [ ] **Step 1: Update the failing command/debug UI call sites to the new APIs**

Replace direct camera/light writes in `src/core/editor/commands/builtin_commands.cpp`:

```cpp
camera->get().setFovY(*value);
camera->get().setNearPlane(*value);
camera->get().setFarPlane(*value);
camera->get().setProjectionType(CameraType::Orthographic);
light->setDirection(*value);
light->setColor(*value);
light->setIntensity(*value);
```

Replace debug UI writes in `src/infra/gui/debug_ui.cpp`:

```cpp
float fovY = camera.getFovY();
if (sliderFloat("fovY", fovY, 1.0f, 179.0f)) {
  camera.setFovY(fovY);
}

auto direction = light.getDirection();
if (dragVec3("dir", direction, 0.01f)) {
  light.setDirection(direction);
}
```

- [ ] **Step 2: Remove the command-local event patching that the new setters supersede**

Delete or inline-away the manual helper from `src/core/editor/commands/builtin_commands.cpp`:

```cpp
void emitRuntimeNodeAspectChanged(SceneNode& node, const SceneNodeAspect aspect);
```

and remove all of its call sites once the underlying setter APIs emit the same runtime events directly.

- [ ] **Step 3: Keep the command regressions explicit**

Preserve and update the existing `testBuiltinAddRemoveSetCommands()` assertions in `src/test/integration/test_command_bus.cpp`:

```cpp
EXPECT(runtimeEvents.back().aspects.front() ==
           LX_core::SceneNodeAspect::CameraProperties,
       "set fov should emit a camera-properties scene node change");
...
EXPECT(runtimeEvents.back().aspects.front() ==
           LX_core::SceneNodeAspect::LightProperties,
       "renamed light set should emit a light-properties scene node change");
```

- [ ] **Step 4: Run command/UI regression tests and commit**

Run:

```bash
cmake --build build --target test_command_bus test_inspector_panel -j4
./build/src/test/test_command_bus
./build/src/test/test_inspector_panel
```

Expected: PASS

Commit:

```bash
git add src/core/editor/commands/builtin_commands.cpp \
        src/infra/gui/debug_ui.cpp \
        src/test/integration/test_command_bus.cpp \
        src/test/integration/test_inspector_panel.cpp
git commit -m "refactor: migrate editor camera light callers"
```

## Task 5: Verify API/Inspector Out-Of-Band Refresh Semantics End-To-End

**Files:**
- Modify: `src/test/integration/test_inspector_panel.cpp`
- Modify: `src/test/integration/test_lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp` only if aspect serialization/filtering needs adjustment

- [ ] **Step 1: Add the failing inspector regressions for direct camera/light writes**

Add to `src/test/integration/test_inspector_panel.cpp`:

```cpp
void testDrawResyncsInspectorAfterExternalCameraMutation() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.cameraNode});

  fixture.cameraNode->getComponent<LX_core::CameraComponent>()->get().setFovY(95.0f);

  const auto snapshot = panel.makeSnapshot();
  EXPECT(nearlyEqual(snapshot.cameraFov, 95.0f),
         "external camera mutation should refresh inspector snapshot");
}

void testDrawResyncsInspectorAfterExternalLightMutation() {
  Fixture fixture;
  LX_core::InspectorPanel panel(fixture.bus, fixture.editorState);
  fixture.editorState.select({fixture.lightNode});

  const auto light = fixture.scene->getDirectionalLight(*fixture.lightNode);
  EXPECT(light != nullptr, "fixture light should resolve");
  if (!light) {
    return;
  }
  light->setIntensity(6.0f);

  const auto snapshot = panel.makeSnapshot();
  EXPECT(nearlyEqual(snapshot.lightIntensity, 6.0f),
         "external light mutation should refresh inspector snapshot");
}
```

- [ ] **Step 2: Add the failing API-service regressions for direct camera/light writes**

Add to `src/test/integration/test_lxe_editor_api_service.cpp`:

```cpp
void testRuntimeCameraPropertyMutationEmitsApiSceneNodeChangedEvent() {
  Fixture fixture;
  const auto cameraNode = SceneNode::create("camera_node");
  cameraNode->setName("camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  fixture.scene->addCamera(cameraNode);

  const ApiEventCursor cursor = fixture.service->currentCursor();
  camera->get().setFovY(88.0f);
  fixture.service->refresh();

  const ApiEventBatch batch = fixture.service->collectEventsSince(cursor);
  ...
  EXPECT(event.sceneNode->aspects.front() == "cameraProperties",
         "API event should serialize camera property aspect");
}
```

and the same shape for `DirectionalLight::setIntensity(...)` expecting `"lightProperties"`.

- [ ] **Step 3: Run the full focused verification set**

Run:

```bash
cmake --build build --target \
  test_scene_events test_command_bus test_inspector_panel \
  test_scene_runtime test_lxe_editor_api_service -j4
./build/src/test/test_scene_events
./build/src/test/test_command_bus
./build/src/test/test_inspector_panel
./build/src/test/test_scene_runtime
./build/src/test/test_lxe_editor_api_service
ctest --output-on-failure -R "test_scene_events|test_command_bus|test_inspector_panel|test_scene_runtime|test_lxe_editor_api_service"
```

Expected: all listed binaries PASS, and `ctest` reports `100% tests passed`.

- [ ] **Step 4: Commit the end-to-end verification changes**

```bash
git add src/test/integration/test_inspector_panel.cpp \
        src/test/integration/test_lxe_editor_api_service.cpp \
        src/demos/lxe_editor/lxe_editor_api_service.cpp
git commit -m "test: cover runtime camera and light property events"
```

## Notes For The Implementer

- Do not keep public mutable camera fields as compatibility aliases. That would leave the original bug surface in place.
- Do not leave `DirectionalLight::ubo` publicly mutable after the migration. Rendering code may still read `getUBO()`, but semantic light state writes must go through setters.
- The existing `SceneNodeAspect::CameraProperties` and `SceneNodeAspect::LightProperties` enum values are already present in the current branch. Reuse them; do not invent new aspect strings.
- During scene load, many setter calls happen before nodes/lights are attached. That is acceptable: detached objects should remain silent, and once attached they should start emitting.
- Preserve current subtree-removal semantics from commit `7b4dc9b` and current node->light association semantics from commit `0e61b34`.

## Self-Review

- Spec coverage:
  - direct write-point event emission for camera/light state is covered by Tasks 1-3
  - existing command/debug UI/runtime callers are migrated in Tasks 2-4
  - inspector/API end-to-end refresh behavior is covered in Task 5
- Placeholder scan:
  - no `TODO` / `TBD` placeholders remain
  - each task includes concrete files, code examples, commands, and expected outcomes
- Type consistency:
  - planned public API uses `getFovY` / `setFovY`, `getProjectionType` / `setProjectionType`, `getDirection` / `setDirection`, `getIntensity` / `setIntensity`
  - planned aspect names consistently use `CameraProperties` / `LightProperties`
