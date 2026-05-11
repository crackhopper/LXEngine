# Editor Preview Camera Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add explicit scene serialize/deserialize for `lxe_editor`, persist `game_cam` as runtime scene state plus `editor_cam` as editor-only metadata, expose `scene load/save` through the command bus, and keep preview toggling as a pure active-camera switch.

**Architecture:** Keep the first implementation scoped to `lxe_editor` instead of pretending the repo already has a generic scene asset system. Introduce a small YAML scene document, a runtime owner object that tracks the current scene path and can rebuild/persist the demo scene, and command-bus handlers that call the same load/save path used by startup and shutdown. `editor_cam` restores from `editor.editorCamera` metadata when present, otherwise falls back to a one-time copy from `game_cam`.

**Tech Stack:** C++20, yaml-cpp, CMake/Ninja, existing `Scene` / `SceneNode` / `CameraComponent` / `EditorState` / `CommandBus` integration tests.

---

## File Structure

- Create: `src/demos/lxe_editor/editor_camera_state.hpp`
  Responsibility: capture/apply editor camera pose and projection fields.
- Create: `src/demos/lxe_editor/editor_camera_state.cpp`
  Responsibility: conversions between `SceneNode` / `CameraComponent` and persisted editor state.
- Create: `src/demos/lxe_editor/scene_document.hpp`
  Responsibility: YAML document contract for `scene`, `gameCamera`, and `editor.editorCamera`.
- Create: `src/demos/lxe_editor/scene_document.cpp`
  Responsibility: yaml-cpp load/save implementation.
- Create: `src/demos/lxe_editor/scene_runtime.hpp`
  Responsibility: own current scene path, current `SceneSharedPtr`, camera nodes, and rebuild/save helpers.
- Create: `src/demos/lxe_editor/scene_runtime.cpp`
  Responsibility: compose existing demo builders with scene document I/O and camera semantics.
- Modify: `src/core/editor/commands/builtin_commands.hpp`
  Responsibility: allow builtin registration to receive scene I/O callbacks/context.
- Modify: `src/core/editor/commands/builtin_commands.cpp`
  Responsibility: add `scene load`, `scene save`, `scene save <path>` handlers.
- Modify: `src/demos/lxe_editor/main.cpp`
  Responsibility: replace ad-hoc camera bootstrap with `SceneRuntime`, wire runtime callbacks into command bus, persist on shutdown.
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
  Responsibility: build the new runtime/document units and tests.
- Modify: `src/demos/lxe_editor/README.md`
  Responsibility: document scene file, camera semantics, and scene commands.
- Create: `assets/scenes/lxe_editor.scene.yaml`
  Responsibility: default scene document for the demo.
- Create: `src/test/integration/test_scene_document.cpp`
  Responsibility: serializer/deserializer round-trip tests.
- Create: `src/test/integration/test_scene_runtime.cpp`
  Responsibility: fallback-copy and metadata-restore runtime tests.
- Modify: `src/test/integration/test_command_bus.cpp`
  Responsibility: scene command coverage plus preview-toggle-no-mutation checks.
- Modify: `src/test/integration/test_viewport_overlay.cpp`
  Responsibility: overlay-level preview invariants stay locked.

### Task 1: Add red tests for scene document, runtime fallback, and scene commands

**Files:**
- Create: `src/test/integration/test_scene_document.cpp`
- Create: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`

- [ ] **Step 1: Add failing scene document tests**

```cpp
#include "demos/lxe_editor/scene_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace demo = LX_demo::lxe_editor;

namespace {
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testLoadSceneDocumentReadsGameAndEditorCamera() {
  const auto path =
      std::filesystem::temp_directory_path() / "lx_scene_document_test.yaml";
  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "gameCamera:\n"
         "  eye: [0.0, 2.0, 6.0]\n"
         "  target: [0.0, 0.0, 0.0]\n"
         "  up: [0.0, 1.0, 0.0]\n"
         "  fovY: 45.0\n"
         "  nearPlane: 0.1\n"
         "  farPlane: 1000.0\n"
         "editor:\n"
         "  editorCamera:\n"
         "    position: [5.0, 6.0, 7.0]\n"
         "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
         "    fovY: 35.0\n"
         "    nearPlane: 0.2\n"
         "    farPlane: 400.0\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.sceneName() == "lxe_editor", "scene name should load");
  EXPECT(doc.hasEditorCamera(), "editor camera metadata should load");
  EXPECT(doc.editorCamera().position.x == 5.0f, "editor camera x should load");
  EXPECT(doc.gameCamera().eye.y == 2.0f, "game camera eye should load");
}
} // namespace
```

- [ ] **Step 2: Add failing runtime fallback tests**

```cpp
#include "demos/lxe_editor/scene_runtime.hpp"

#include <iostream>

namespace demo = LX_demo::lxe_editor;

namespace {
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testRuntimeFallsBackEditorCameraFromGameCamera() {
  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath("assets/scenes/lxe_editor.scene.yaml");
  EXPECT(runtime.editorCameraNode(), "editor camera node should exist");
  EXPECT(runtime.gameCameraNode(), "game camera node should exist");
  EXPECT(runtime.editorCameraNode()->getLocalTransform().translation ==
             runtime.gameCameraNode()->getLocalTransform().translation,
         "missing editor metadata should copy game camera pose");
}
} // namespace
```

- [ ] **Step 3: Extend command bus tests with failing scene command expectations**

```cpp
void testSceneCommandsRequireRegisteredSceneIoCallbacks() {
  CommandFixture fixture;
  const CommandResult saveResult = fixture.bus.dispatch("scene save");
  EXPECT(!saveResult.ok, "scene save should fail before scene io is wired");
  EXPECT(saveResult.message.find("scene io unavailable") != std::string::npos,
         "scene save failure should explain missing io");
}
```

- [ ] **Step 4: Register new test targets**

```cmake
add_executable(test_scene_document
  ${CMAKE_SOURCE_DIR}/src/test/integration/test_scene_document.cpp
  scene_document.cpp
  editor_camera_state.cpp
)
add_executable(test_scene_runtime
  ${CMAKE_SOURCE_DIR}/src/test/integration/test_scene_runtime.cpp
  scene_runtime.cpp
  scene_document.cpp
  editor_camera_state.cpp
  scene_builder.cpp
)
add_test(NAME test_scene_document COMMAND test_scene_document)
add_test(NAME test_scene_runtime COMMAND test_scene_runtime)
```

- [ ] **Step 5: Run the red tests**

Run: `cmake --build build --target test_scene_document test_scene_runtime test_command_bus -j4`

Expected: FAIL because `SceneDocument`, `SceneRuntime`, and scene command plumbing do not exist yet.

- [ ] **Step 6: Commit the red test harness**

```bash
git add src/test/integration/test_scene_document.cpp src/test/integration/test_scene_runtime.cpp src/test/integration/test_command_bus.cpp src/demos/lxe_editor/CMakeLists.txt
git commit -m "test: add scene io red coverage"
```

### Task 2: Implement `EditorCameraState`

**Files:**
- Create: `src/demos/lxe_editor/editor_camera_state.hpp`
- Create: `src/demos/lxe_editor/editor_camera_state.cpp`
- Test: `src/test/integration/test_scene_document.cpp`

- [ ] **Step 1: Add the value type header**

```cpp
#pragma once

#include "core/math/vec3.hpp"
#include "core/scene/camera.hpp"

namespace LX_core {
class CameraComponent;
class SceneNode;
}

namespace LX_demo::lxe_editor {

struct EditorCameraState final {
  LX_core::Vec3f position{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f rotationEulerDeg{0.0f, 0.0f, 0.0f};
  LX_core::CameraType projectionType = LX_core::CameraType::Perspective;
  float fovY = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float aspect = 16.0f / 9.0f;

  static EditorCameraState fromSceneCamera(const LX_core::SceneNode& node,
                                           const LX_core::CameraComponent& camera);
  void applyTo(LX_core::SceneNode& node, LX_core::CameraComponent& camera) const;
};

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement capture/apply helpers**

```cpp
#include "demos/lxe_editor/editor_camera_state.hpp"

#include "core/math/quat.hpp"
#include "core/math/transform.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"

namespace LX_demo::lxe_editor {

EditorCameraState EditorCameraState::fromSceneCamera(const LX_core::SceneNode& node,
                                                     const LX_core::CameraComponent& camera) {
  const LX_core::Transform transform = node.getLocalTransform();
  EditorCameraState state;
  state.position = transform.translation;
  state.rotationEulerDeg = transform.rotation.toEulerDegrees();
  state.projectionType = camera.type;
  state.fovY = camera.fovY;
  state.nearPlane = camera.nearPlane;
  state.farPlane = camera.farPlane;
  state.aspect = camera.aspect;
  return state;
}

void EditorCameraState::applyTo(LX_core::SceneNode& node,
                                LX_core::CameraComponent& camera) const {
  LX_core::Transform transform = node.getLocalTransform();
  transform.translation = position;
  transform.rotation = LX_core::Quat::fromEulerDegrees(rotationEulerDeg);
  node.setLocalTransform(transform);
  camera.type = projectionType;
  camera.fovY = fovY;
  camera.nearPlane = nearPlane;
  camera.farPlane = farPlane;
  camera.aspect = aspect;
  camera.updateMatrices();
}

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 3: Re-run document test**

Run: `cmake --build build --target test_scene_document -j4 && ./build/src/test/test_scene_document`

Expected: still FAIL, but only on missing `SceneDocument` symbols.

- [ ] **Step 4: Commit**

```bash
git add src/demos/lxe_editor/editor_camera_state.hpp src/demos/lxe_editor/editor_camera_state.cpp
git commit -m "feat: add editor camera state value type"
```

### Task 3: Implement `SceneDocument` YAML load/save

**Files:**
- Create: `src/demos/lxe_editor/scene_document.hpp`
- Create: `src/demos/lxe_editor/scene_document.cpp`
- Create: `assets/scenes/lxe_editor.scene.yaml`
- Test: `src/test/integration/test_scene_document.cpp`

- [ ] **Step 1: Add the document interface**

```cpp
#pragma once

#include "demos/lxe_editor/editor_camera_state.hpp"

#include "core/math/vec3.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct GameplayCameraState final {
  LX_core::Vec3f eye{0.0f, 2.0f, 6.0f};
  LX_core::Vec3f target{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f up{0.0f, 1.0f, 0.0f};
  float fovY = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
};

class SceneDocument final {
public:
  const std::string& sceneName() const { return m_sceneName; }
  void setSceneName(std::string name) { m_sceneName = std::move(name); }
  GameplayCameraState& gameCamera() { return m_gameCamera; }
  const GameplayCameraState& gameCamera() const { return m_gameCamera; }
  bool hasEditorCamera() const { return m_editorCamera.has_value(); }
  const EditorCameraState& editorCamera() const { return *m_editorCamera; }
  void setEditorCamera(EditorCameraState state) { m_editorCamera = std::move(state); }

private:
  std::string m_sceneName = "lxe_editor";
  GameplayCameraState m_gameCamera;
  std::optional<EditorCameraState> m_editorCamera;

  friend SceneDocument loadSceneDocument(const std::filesystem::path& path);
  friend void saveSceneDocument(const std::filesystem::path& path,
                                const SceneDocument& document);
};

SceneDocument loadSceneDocument(const std::filesystem::path& path);
void saveSceneDocument(const std::filesystem::path& path, const SceneDocument& document);

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement YAML load/save**

```cpp
#include "demos/lxe_editor/scene_document.hpp"

#include <yaml-cpp/yaml.h>

namespace LX_demo::lxe_editor {

SceneDocument loadSceneDocument(const std::filesystem::path& path) {
  const YAML::Node root = YAML::LoadFile(path.string());
  SceneDocument doc;
  doc.m_sceneName = root["scene"]["name"].as<std::string>();
  doc.m_gameCamera.eye = root["gameCamera"]["eye"].as<LX_core::Vec3f>();
  doc.m_gameCamera.target = root["gameCamera"]["target"].as<LX_core::Vec3f>();
  doc.m_gameCamera.up = root["gameCamera"]["up"].as<LX_core::Vec3f>();
  doc.m_gameCamera.fovY = root["gameCamera"]["fovY"].as<float>();
  doc.m_gameCamera.nearPlane = root["gameCamera"]["nearPlane"].as<float>();
  doc.m_gameCamera.farPlane = root["gameCamera"]["farPlane"].as<float>();
  if (const YAML::Node editorCamera = root["editor"]["editorCamera"]) {
    EditorCameraState state;
    state.position = editorCamera["position"].as<LX_core::Vec3f>();
    state.rotationEulerDeg = editorCamera["rotationEulerDeg"].as<LX_core::Vec3f>();
    state.fovY = editorCamera["fovY"].as<float>();
    state.nearPlane = editorCamera["nearPlane"].as<float>();
    state.farPlane = editorCamera["farPlane"].as<float>();
    doc.m_editorCamera = state;
  }
  return doc;
}

void saveSceneDocument(const std::filesystem::path& path, const SceneDocument& document) {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "scene" << YAML::Value << YAML::BeginMap
      << YAML::Key << "name" << YAML::Value << document.m_sceneName
      << YAML::EndMap;
  out << YAML::Key << "gameCamera" << YAML::Value << YAML::BeginMap
      << YAML::Key << "eye" << YAML::Value << document.m_gameCamera.eye
      << YAML::Key << "target" << YAML::Value << document.m_gameCamera.target
      << YAML::Key << "up" << YAML::Value << document.m_gameCamera.up
      << YAML::Key << "fovY" << YAML::Value << document.m_gameCamera.fovY
      << YAML::Key << "nearPlane" << YAML::Value << document.m_gameCamera.nearPlane
      << YAML::Key << "farPlane" << YAML::Value << document.m_gameCamera.farPlane
      << YAML::EndMap;
  out << YAML::Key << "editor" << YAML::Value << YAML::BeginMap;
  if (document.m_editorCamera.has_value()) {
    out << YAML::Key << "editorCamera" << YAML::Value << YAML::BeginMap
        << YAML::Key << "position" << YAML::Value << document.m_editorCamera->position
        << YAML::Key << "rotationEulerDeg" << YAML::Value << document.m_editorCamera->rotationEulerDeg
        << YAML::Key << "fovY" << YAML::Value << document.m_editorCamera->fovY
        << YAML::Key << "nearPlane" << YAML::Value << document.m_editorCamera->nearPlane
        << YAML::Key << "farPlane" << YAML::Value << document.m_editorCamera->farPlane
        << YAML::EndMap;
  }
  out << YAML::EndMap << YAML::EndMap;
  std::ofstream stream(path);
  stream << out.c_str();
}

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 3: Seed the default scene file**

```yaml
scene:
  name: lxe_editor
gameCamera:
  eye: [0.0, 2.0, 6.0]
  target: [0.0, 0.0, 0.0]
  up: [0.0, 1.0, 0.0]
  fovY: 45.0
  nearPlane: 0.1
  farPlane: 1000.0
editor: {}
```

- [ ] **Step 4: Run the document test**

Run: `cmake --build build --target test_scene_document -j4 && ./build/src/test/test_scene_document`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/scene_document.hpp src/demos/lxe_editor/scene_document.cpp assets/scenes/lxe_editor.scene.yaml
git commit -m "feat: add scene viewer scene document"
```

### Task 4: Implement `SceneRuntime` and bootstrap camera semantics

**Files:**
- Create: `src/demos/lxe_editor/scene_runtime.hpp`
- Create: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Define the runtime owner**

```cpp
#pragma once

#include "demos/lxe_editor/scene_document.hpp"

#include "core/scene/scene.hpp"

#include <filesystem>

namespace LX_demo::lxe_editor {

class SceneRuntime final {
public:
  void loadFromDocumentPath(const std::filesystem::path& path);
  void save();
  void saveAs(const std::filesystem::path& path);

  LX_core::SceneSharedPtr scene() const { return m_scene; }
  LX_core::SceneNodeSharedPtr gameCameraNode() const { return m_gameCameraNode; }
  LX_core::SceneNodeSharedPtr editorCameraNode() const { return m_editorCameraNode; }
  const std::filesystem::path& currentPath() const { return m_currentPath; }

private:
  void rebuildSceneFromDocument();

  std::filesystem::path m_currentPath;
  SceneDocument m_document;
  LX_core::SceneSharedPtr m_scene;
  LX_core::SceneNodeSharedPtr m_gameCameraNode;
  LX_core::SceneNodeSharedPtr m_editorCameraNode;
};

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement rebuild, fallback copy, and save-back**

```cpp
void SceneRuntime::loadFromDocumentPath(const std::filesystem::path& path) {
  m_currentPath = path;
  m_document = loadSceneDocument(path);
  rebuildSceneFromDocument();
}

void SceneRuntime::save() {
  m_document.setEditorCamera(
      EditorCameraState::fromSceneCamera(*m_editorCameraNode,
                                         m_editorCameraNode->getComponent<LX_core::CameraComponent>()->get()));
  saveSceneDocument(m_currentPath, m_document);
}

void SceneRuntime::rebuildSceneFromDocument() {
  auto helmet = buildHelmetNode(resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf"));
  auto ground = buildGroundNode();
  helmet->setName("helmet");
  ground->setName("ground");
  helmet->setParent(ground);
  m_scene = LX_core::Scene::create(m_document.sceneName(), helmet);
  m_scene->addRenderable(ground);

  m_gameCameraNode = LX_core::SceneNode::create("game_camera");
  m_gameCameraNode->setName("game_cam");
  auto gameCamera = m_gameCameraNode->addComponent<LX_core::CameraComponent>();
  gameCamera->get().lookAt(m_document.gameCamera().eye,
                           m_document.gameCamera().target,
                           m_document.gameCamera().up);
  gameCamera->get().fovY = m_document.gameCamera().fovY;
  gameCamera->get().nearPlane = m_document.gameCamera().nearPlane;
  gameCamera->get().farPlane = m_document.gameCamera().farPlane;
  m_scene->addCamera(m_gameCameraNode);

  m_editorCameraNode = LX_core::SceneNode::create("editor_camera");
  m_editorCameraNode->setName("editor_cam");
  auto editorCamera = m_editorCameraNode->addComponent<LX_core::CameraComponent>();
  if (m_document.hasEditorCamera()) {
    m_document.editorCamera().applyTo(*m_editorCameraNode, editorCamera->get());
  } else {
    EditorCameraState::fromSceneCamera(*m_gameCameraNode, gameCamera->get())
        .applyTo(*m_editorCameraNode, editorCamera->get());
  }
  editorCamera->get().setCullingMask(LX_core::Layer_All);
  m_scene->addCamera(m_editorCameraNode);
}
```

- [ ] **Step 3: Refactor `main.cpp` to use `SceneRuntime`**

```cpp
demo::SceneRuntime sceneRuntime;
sceneRuntime.loadFromDocumentPath(resolveRuntimePath("assets/scenes/lxe_editor.scene.yaml"));
auto scene = sceneRuntime.scene();
auto editorCameraNode = sceneRuntime.editorCameraNode();
auto gameCameraNode = sceneRuntime.gameCameraNode();
auto editorCamera = editorCameraNode->getComponent<LX_core::CameraComponent>();
auto gameCamera = gameCameraNode->getComponent<LX_core::CameraComponent>();
```

- [ ] **Step 4: Run runtime test**

Run: `cmake --build build --target test_scene_runtime -j4 && ./build/src/test/test_scene_runtime`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/scene_runtime.hpp src/demos/lxe_editor/scene_runtime.cpp src/demos/lxe_editor/main.cpp
git commit -m "feat: add scene viewer runtime scene io"
```

### Task 5: Add `scene load/save` command handlers

**Files:**
- Modify: `src/core/editor/commands/builtin_commands.hpp`
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Extend builtin registration API with scene I/O callbacks**

```cpp
struct SceneIoContext final {
  std::function<CommandResult(const std::string& path)> loadScene;
  std::function<CommandResult(std::optional<std::string> path)> saveScene;
};

void registerBuiltinCommands(CommandBus& bus, EditorState& editorState, Scene& scene,
                             SceneIoContext sceneIo = {});
```

- [ ] **Step 2: Implement the `scene` verb**

```cpp
bus.registerHandler(
    "scene",
    "scene load <path> | scene save [path]",
    [&sceneIo](std::vector<std::string> args) {
      if (args.empty()) {
        return makeError("usage: scene load <path> | scene save [path]");
      }
      if (args[0] == "load") {
        if (!sceneIo.loadScene) {
          return makeError("scene io unavailable: load");
        }
        if (args.size() != 2) {
          return makeError("usage: scene load <path>");
        }
        return sceneIo.loadScene(args[1]);
      }
      if (args[0] == "save") {
        if (!sceneIo.saveScene) {
          return makeError("scene io unavailable: save");
        }
        if (args.size() == 1) {
          return sceneIo.saveScene(std::nullopt);
        }
        if (args.size() == 2) {
          return sceneIo.saveScene(args[1]);
        }
        return makeError("usage: scene save [path]");
      }
      return makeError("unknown scene action: " + args[0]);
    });
```

- [ ] **Step 3: Add passing command tests**

```cpp
void testSceneCommandsRouteThroughCallbacks() {
  CommandFixture fixture;
  registerBuiltinCommands(
      fixture.bus, fixture.editorState, *fixture.scene,
      SceneIoContext{
          .loadScene = [](const std::string& path) {
            return CommandResult{true, "loaded " + path,
                                 "{\"action\":\"load\",\"path\":\"" + path + "\"}"};
          },
          .saveScene = [](std::optional<std::string> path) {
            const std::string resolved = path.value_or("current.scene.yaml");
            return CommandResult{true, "saved " + resolved,
                                 "{\"action\":\"save\",\"path\":\"" + resolved + "\"}"};
          }});

  EXPECT(fixture.bus.dispatch("scene load demo.scene.yaml").ok, "scene load should succeed");
  EXPECT(fixture.bus.dispatch("scene save").ok, "scene save should succeed");
  EXPECT(fixture.bus.dispatch("scene save other.scene.yaml").ok, "scene save path should succeed");
}
```

- [ ] **Step 4: Run command bus tests**

Run: `cmake --build build --target test_command_bus -j4 && ./build/src/test/test_command_bus`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/editor/commands/builtin_commands.hpp src/core/editor/commands/builtin_commands.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: add scene load save commands"
```

### Task 6: Wire main-session scene commands to `SceneRuntime`

**Files:**
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/test/integration/test_viewport_overlay.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Pass runtime callbacks into builtin command registration**

```cpp
LX_core::registerBuiltinCommands(
    commandBus, editorState, *scene,
    LX_core::SceneIoContext{
        .loadScene = [&](const std::string& path) {
          sceneRuntime.loadFromDocumentPath(path);
          scene = sceneRuntime.scene();
          editorState.setEditorCamera(sceneRuntime.editorCameraNode());
          editorState.setPreviewCamera(sceneRuntime.gameCameraNode());
          editorState.setPreviewEnabled(false);
          editorState.syncActiveCamera(*scene);
          return LX_core::CommandResult{true, "scene loaded: " + path,
                                        "{\"action\":\"load\",\"path\":\"" + path + "\"}"};
        },
        .saveScene = [&](std::optional<std::string> path) {
          if (path.has_value()) {
            sceneRuntime.saveAs(*path);
          } else {
            sceneRuntime.save();
          }
          const std::string resolved = path.value_or(sceneRuntime.currentPath().string());
          return LX_core::CommandResult{true, "scene saved: " + resolved,
                                        "{\"action\":\"save\",\"path\":\"" + resolved + "\"}"};
        }});
```

- [ ] **Step 2: Add regression check that preview toggle still does not mutate camera poses**

```cpp
void testPreviewToggleDoesNotMutateCameraState() {
  CommandFixture fixture;
  // after wiring scene io, keep the existing preview invariant
  const auto beforeEditor = fixture.cameraNode->getLocalTransform();
  const CommandResult on = fixture.bus.dispatch("preview on");
  EXPECT(on.ok, "preview on succeeds");
  EXPECT(fixture.cameraNode->getLocalTransform().translation ==
             beforeEditor.translation,
         "preview should not mutate editor camera pose");
}
```

- [ ] **Step 3: Run focused tests**

Run: `cmake --build build --target test_command_bus test_viewport_overlay test_scene_runtime -j4`

Run: `ctest --test-dir build --output-on-failure -R 'test_(command_bus|viewport_overlay|scene_runtime)$'`

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/demos/lxe_editor/main.cpp src/test/integration/test_viewport_overlay.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: wire scene runtime into editor commands"
```

### Task 7: Update demo docs and verify the full targeted matrix

**Files:**
- Modify: `src/demos/lxe_editor/README.md`

- [ ] **Step 1: Document scene file and commands**

```md
## Scene file

`lxe_editor` now loads from `assets/scenes/lxe_editor.scene.yaml`.

- `gameCamera` stores the authored gameplay camera.
- `editor.editorCamera` stores editor-only view state.

## Scene commands

- `scene load <path>`
- `scene save`
- `scene save <path>`

`F` only toggles active rendering between `editor_cam` and `game_cam`; it does not copy transforms.
```

- [ ] **Step 2: Run the full matrix**

Run: `cmake --build build --target lxe_editor test_scene_document test_scene_runtime test_command_bus test_viewport_overlay -j4`

Run: `ctest --test-dir build --output-on-failure -R 'test_(scene_document|scene_runtime|command_bus|viewport_overlay)$'`

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/demos/lxe_editor/README.md
git commit -m "docs: describe scene viewer scene io"
```

## Self-Review

- Spec coverage:
  - scene serialize/deserialize contract is covered by Tasks 1 and 3.
  - `editor_cam` metadata restore and fallback-from-`game_cam` are covered by Tasks 2 and 4.
  - pure preview toggle semantics are covered by Tasks 5 and 6.
  - `scene load/save` command-bus entrypoints are covered by Tasks 5 and 6.
  - user-facing docs are covered by Task 7.
- Placeholder scan:
  - No `TBD`, `TODO`, or “implement later” placeholders remain.
  - The scope is intentionally `lxe_editor`-local and that limitation is explicit in the architecture.
- Type consistency:
  - `EditorCameraState`, `SceneDocument`, `SceneRuntime`, and `SceneIoContext` are defined once and reused consistently across tasks.
