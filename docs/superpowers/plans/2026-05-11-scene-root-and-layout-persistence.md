# Scene Root And Layout Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a real explicit scene root across runtime and scene serialization, show that root in the Scene Tree, and persist `lxe_editor` window/layout state under local `data/` storage.

**Architecture:** Split the work into three seams. First, convert `Scene` from a synthetic-root path helper to a real root-node model and lock that with unit-style integration tests. Second, upgrade scene document load/save to serialize the explicit root while still normalizing older flat documents on load. Third, add a narrow editor-shell persistence layer that writes ImGui layout plus native window geometry to local files under `data/`, without mixing that state into scene assets.

**Tech Stack:** C++20, yaml-cpp, ImGui, SDL/GLFW window backends, existing `Scene`, `SceneTreePanel`, `lxe_editor`, and `src/test/integration` coverage.

---

## File Structure

- Modify: `src/core/scene/object.hpp`
  Responsibility: declare explicit-root helpers or root-role queries if the scene model needs them.
- Modify: `src/core/scene/object.cpp`
  Responsibility: implement root-node behavior and any node-level invariants tied to the explicit root.
- Modify: `src/core/scene/scene.hpp`
  Responsibility: expose real root-node access and update traversal/path APIs.
- Modify: `src/core/scene/scene.cpp`
  Responsibility: own the explicit root, update `findByPath("/")`, `getRootNodes()`, `dumpTree()`, and hierarchy traversal.
- Modify: `src/core/editor/scene_tree_panel.cpp`
  Responsibility: render the explicit root row instead of flattening top-level authored nodes.
- Modify: `src/demos/lxe_editor/scene_document.hpp`
  Responsibility: extend the full-scene schema contract to include a serialized root node.
- Modify: `src/demos/lxe_editor/scene_document.cpp`
  Responsibility: load legacy flat documents, save canonical explicit-root documents.
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
  Responsibility: adapt runtime scene bootstrap and root-dependent traversal to the new model.
- Modify: `src/demos/lxe_editor/README.md`
  Responsibility: document explicit-root scene files and local layout persistence behavior.
- Modify: `notes/subsystems/scene.md`
  Responsibility: replace stale synthetic-root descriptions with the new explicit-root model after implementation lands.
- Modify: `src/core/platform/window.hpp`
  Responsibility: add native window state query/apply APIs for position, size, and maximized state.
- Modify: `src/infra/window/window.hpp`
  Responsibility: mirror the core window API in the concrete infra wrapper.
- Modify: `src/infra/window/sdl_window.cpp`
  Responsibility: implement native window state restore/save on the SDL path.
- Modify: `src/infra/window/glfw_window.cpp`
  Responsibility: keep the GLFW backend aligned with the window API contract if this backend is still built.
- Modify: `src/infra/gui/imgui_gui.cpp`
  Responsibility: move ImGui ini persistence from disabled mode to a local `data/` path.
- Modify: `src/demos/lxe_editor/main.cpp`
  Responsibility: restore layout/window state on startup and save it on shutdown.
- Create: `src/demos/lxe_editor/window_layout_state.hpp`
  Responsibility: small value types and helpers for local window/layout persistence.
- Create: `src/demos/lxe_editor/window_layout_state.cpp`
  Responsibility: YAML or equivalent structured load/save for window geometry state.
- Modify: `src/test/integration/test_scene_document.cpp`
  Responsibility: cover explicit-root save/load and legacy flat-document normalization.
- Modify: `src/test/integration/test_scene_runtime.cpp`
  Responsibility: cover empty-scene/root bootstrap and root-aware runtime behavior.
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`
  Responsibility: cover local layout-file paths and non-fatal missing/corrupt-state behavior.
- Modify: `src/test/integration/test_scene_node_validation.cpp`
  Responsibility: lock root-node structural invariants if existing validation tests already cover node hierarchy.
- Modify: `src/test/integration/test_command_bus.cpp`
  Responsibility: keep scene commands green with explicit-root scene documents.

## Task 1: Add failing coverage for explicit-root scene behavior

**Files:**
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_node_validation.cpp`

- [ ] **Step 1: Add a failing scene-document test for the new canonical root shape**

Add a test near the existing scene-document coverage with assertions like:

```cpp
void testSceneDocumentLoadsExplicitRootHierarchy() {
  const auto path = std::filesystem::temp_directory_path() /
                    "lx_scene_explicit_root.yaml";
  std::ofstream out(path);
  out << "scene:\n"
         "  name: explicit_root\n"
         "root:\n"
         "  name: root\n"
         "  children:\n"
         "    - name: world\n"
         "      children:\n"
         "        - name: helmet\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.rootNode().name == "root", "root node should load");
  EXPECT(doc.rootNode().children.size() == 1, "root should own top-level nodes");
  EXPECT(doc.rootNode().children[0].name == "world", "world should be root child");
}
```

- [ ] **Step 2: Add a failing legacy-compatibility test**

Add a second scene-document test that still writes the current legacy flat shape:

```cpp
void testSceneDocumentNormalizesLegacyTopLevelNodesIntoRoot() {
  const auto path = std::filesystem::temp_directory_path() /
                    "lx_scene_legacy_flat.yaml";
  std::ofstream out(path);
  out << "scene:\n"
         "  name: legacy\n"
         "nodes:\n"
         "  - name: world\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.rootNode().name == "root", "legacy docs should synthesize explicit root");
  EXPECT(doc.rootNode().children.size() == 1, "legacy top-level nodes should move under root");
}
```

- [ ] **Step 3: Add a failing runtime/root-path test**

Extend `test_scene_runtime.cpp` with a focused assertion:

```cpp
void testRuntimeSceneRootResolvesSlashPath() {
  demo::SceneRuntime runtime;
  runtime.resetToEmptyScene();
  LX_core::Scene& scene = runtime.scene();
  LX_core::SceneNode* root = scene.findByPath("/");
  EXPECT(root != nullptr, "root path should resolve");
  EXPECT(root->getName() == "root", "root node should have canonical name");
  EXPECT(root->getParent() == nullptr, "root node should be parentless");
}
```

- [ ] **Step 4: Add a failing hierarchy invariant test**

Extend `test_scene_node_validation.cpp` with an invariant for the explicit root:

```cpp
void testSceneAlwaysKeepsSingleExplicitRoot() {
  auto scene = std::make_shared<LX_core::Scene>("test");
  LX_core::SceneNode* root = scene->findByPath("/");
  EXPECT(root != nullptr, "scene should always create root");
  EXPECT(scene->getRootNodes().size() == 1, "scene should expose one real root node");
  EXPECT(scene->getRootNodes()[0].get() == root, "root list should contain the explicit root");
}
```

- [ ] **Step 5: Run the focused red tests**

Run:

```bash
cmake --build build --target test_scene_document test_scene_runtime test_scene_node_validation
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
./build/src/test/test_scene_node_validation
```

Expected: FAIL because the current scene model still uses flat top-level nodes plus a synthetic path root.

- [ ] **Step 6: Commit the red coverage**

```bash
git add src/test/integration/test_scene_document.cpp src/test/integration/test_scene_runtime.cpp src/test/integration/test_scene_node_validation.cpp
git commit -m "test: add explicit scene root coverage"
```

## Task 2: Convert `Scene` to a real explicit-root model

**Files:**
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`
- Test: `src/test/integration/test_scene_node_validation.cpp`

- [ ] **Step 1: Add a root-node accessor to `Scene`**

Update `src/core/scene/scene.hpp` with an explicit accessor and any helper needed by callers:

```cpp
class Scene : public std::enable_shared_from_this<Scene> {
public:
  [[nodiscard]] SceneNodeSharedPtr getRootNode() const;
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getTopLevelAuthoredNodes() const;
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getRootNodes() const;
  SceneNode* findByPath(const std::string& path) const;
};
```

Keep `getRootNodes()` temporarily if other code depends on that name, but redefine it to return a single-element vector containing the explicit root. Use `getTopLevelAuthoredNodes()` for callers that truly mean root children.

- [ ] **Step 2: Make `Scene` own one real root node**

In `src/core/scene/scene.cpp`, replace synthetic-root logic with explicit node ownership along these lines:

```cpp
Scene::Scene(std::string name)
    : m_name(std::move(name)),
      m_rootNode(std::make_shared<SceneNode>("root")) {
  m_rootNode->attachToScene(*this);
}

SceneNodeSharedPtr Scene::getRootNode() const {
  return m_rootNode;
}

std::vector<SceneNodeSharedPtr> Scene::getRootNodes() const {
  return {m_rootNode};
}

std::vector<SceneNodeSharedPtr> Scene::getTopLevelAuthoredNodes() const {
  return m_rootNode ? m_rootNode->getChildren() : std::vector<SceneNodeSharedPtr>{};
}
```

Adapt existing code paths that currently scan `m_nodes` for `parent == nullptr` so they instead traverse from `m_rootNode`.

- [ ] **Step 3: Update path and tree traversal**

Refactor `findByPath("/")`, `dumpTree()`, and any helper that starts from flat top-level nodes so they begin at `m_rootNode`:

```cpp
SceneNode* Scene::findByPath(const std::string& path) const {
  if (path == "/" || path.empty()) {
    return m_rootNode.get();
  }
  std::vector<SceneNodeSharedPtr> candidates = m_rootNode->getChildren();
  // existing segment matching continues from here
}
```

For tree dumps, include the explicit root as the first line and recurse through its children.

- [ ] **Step 4: Make root restrictions explicit**

If any removal or rename flows currently assume "node without parent means top-level", convert them to an explicit root-role check. A narrow helper is acceptable:

```cpp
[[nodiscard]] bool SceneNode::isSceneRoot() const;
```

Use it in destructive command guards rather than relying on `getParent() == nullptr`.

- [ ] **Step 5: Rebuild and rerun the root-focused tests**

Run:

```bash
cmake --build build --target test_scene_runtime test_scene_node_validation
./build/src/test/test_scene_runtime
./build/src/test/test_scene_node_validation
```

Expected: PASS for the explicit-root runtime and invariant tests; scene-document tests may still fail until serialization is upgraded.

- [ ] **Step 6: Commit the scene-core conversion**

```bash
git add src/core/scene/scene.hpp src/core/scene/scene.cpp src/core/scene/object.hpp src/core/scene/object.cpp src/test/integration/test_scene_runtime.cpp src/test/integration/test_scene_node_validation.cpp
git commit -m "feat: add explicit scene root model"
```

## Task 3: Upgrade scene document load/save to the explicit-root schema

**Files:**
- Modify: `src/demos/lxe_editor/scene_document.hpp`
- Modify: `src/demos/lxe_editor/scene_document.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Extend the document model with a serialized root node**

Update `scene_document.hpp` so the document owns one hierarchy root instead of a flat top-level node array:

```cpp
struct SceneDocumentNode final {
  std::string name;
  LX_core::Transform localTransform{};
  std::optional<MeshComponentState> mesh;
  std::optional<MaterialComponentState> material;
  std::optional<CameraComponentState> camera;
  std::vector<SceneDocumentNode> children;
};

class SceneDocument final {
public:
  [[nodiscard]] const SceneDocumentNode& rootNode() const;
  void setRootNode(SceneDocumentNode root);
private:
  SceneDocumentNode m_rootNode{};
};
```

- [ ] **Step 2: Load new-format `root:` and normalize old-format `nodes:`**

In `scene_document.cpp`, make load accept both:

```cpp
if (const YAML::Node rootNode = root["root"]; rootNode) {
  doc.setRootNode(loadSceneDocumentNode(rootNode));
} else if (const YAML::Node nodesNode = root["nodes"]; nodesNode && nodesNode.IsSequence()) {
  SceneDocumentNode explicitRoot;
  explicitRoot.name = "root";
  for (const YAML::Node& child : nodesNode) {
    explicitRoot.children.push_back(loadSceneDocumentNode(child));
  }
  doc.setRootNode(std::move(explicitRoot));
}
```

Treat missing both `root` and `nodes` as an invalid scene file.

- [ ] **Step 3: Save only the canonical explicit-root format**

Update save to emit:

```yaml
scene:
  name: sample
root:
  name: root
  children:
    - name: world
```

Do not keep writing `nodes:` in new saves.

- [ ] **Step 4: Adapt runtime scene build/apply helpers**

In `scene_runtime.cpp`, update document-to-runtime and runtime-to-document paths so they:

- build authored content by iterating `document.rootNode().children`
- populate the runtime `Scene` under `scene.getRootNode()`
- serialize from the runtime root node back into `document.rootNode()`

Keep camera/editor metadata semantics unchanged apart from the hierarchy anchor.

- [ ] **Step 5: Rebuild and rerun the scene-document and command tests**

Run:

```bash
cmake --build build --target test_scene_document test_scene_runtime test_command_bus
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
./build/src/test/test_command_bus
```

Expected: PASS, including legacy-load normalization and unchanged command-bus save/load behavior.

- [ ] **Step 6: Commit the document migration**

```bash
git add src/demos/lxe_editor/scene_document.hpp src/demos/lxe_editor/scene_document.cpp src/demos/lxe_editor/scene_runtime.cpp src/test/integration/test_scene_document.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: serialize explicit scene root"
```

## Task 4: Show the real root in the Scene Tree

**Files:**
- Modify: `src/core/editor/scene_tree_panel.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Make the panel start from `Scene::getRootNode()`**

Replace the current flat render loop:

```cpp
for (const auto& root : m_scene.getRootNodes()) {
  if (!root) {
    continue;
  }
  drawNode(*root);
}
```

with a single-root flow:

```cpp
if (const auto root = m_scene.getRootNode(); root) {
  drawNode(*root);
}
```

Any selection-range helper that previously treated `m_scene.getRootNodes()` as authored siblings should instead use `m_scene.getTopLevelAuthoredNodes()` when the parent is the root.

- [ ] **Step 2: Add a focused panel-behavior assertion if existing tests can cover it**

If `test_scene_runtime.cpp` or an existing layout test already inspects Scene Tree output, add a simple assertion that the first visible/serialized tree line is the root, e.g. by using `Scene::dumpTree()`:

```cpp
void testDumpTreeStartsWithRoot() {
  auto scene = std::make_shared<LX_core::Scene>("tree");
  EXPECT(scene->dumpTree().find("root") == 0, "tree dump should start with root");
}
```

- [ ] **Step 3: Rebuild and rerun the affected tests**

Run:

```bash
cmake --build build --target test_scene_runtime
./build/src/test/test_scene_runtime
```

Expected: PASS, with no regression in selection/path behavior.

- [ ] **Step 4: Commit the panel update**

```bash
git add src/core/editor/scene_tree_panel.cpp src/test/integration/test_scene_runtime.cpp
git commit -m "feat: show explicit root in scene tree"
```

## Task 5: Add failing local layout/window-state coverage

**Files:**
- Create: `src/test/integration/test_lxe_editor_layout.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`

- [ ] **Step 1: Add a failing test for missing-layout fallback**

Create `test_lxe_editor_layout.cpp` with a small harness for the planned persistence helper:

```cpp
void testLayoutLoaderMissingFilesFallsBackToDefaults() {
  const auto dir = std::filesystem::temp_directory_path() / "lx_layout_missing";
  std::filesystem::remove_all(dir);

  const demo::WindowLayoutState state =
      demo::loadWindowLayoutState(dir / "window_state.yaml");

  EXPECT(!state.position.has_value(), "missing file should not invent position");
  EXPECT(!state.maximized, "missing file should default maximized off");
}
```

- [ ] **Step 2: Add a failing round-trip test for native window state**

In the same file, add:

```cpp
void testWindowLayoutStateRoundTripsGeometry() {
  const auto path = std::filesystem::temp_directory_path() /
                    "lx_window_state.yaml";
  demo::WindowLayoutState written;
  written.position = demo::WindowPoint{100, 120};
  written.size = demo::WindowSize{1600, 900};
  written.maximized = true;

  demo::saveWindowLayoutState(path, written);
  const demo::WindowLayoutState loaded = demo::loadWindowLayoutState(path);

  EXPECT(loaded.position.has_value(), "position should persist");
  EXPECT(loaded.position->x == 100, "x should round-trip");
  EXPECT(loaded.size.has_value(), "size should persist");
  EXPECT(loaded.maximized, "maximized should round-trip");
}
```

- [ ] **Step 3: Register the new test target**

Add to `src/demos/lxe_editor/CMakeLists.txt` or the test CMake file:

```cmake
add_executable(test_lxe_editor_layout
  ${CMAKE_SOURCE_DIR}/src/test/integration/test_lxe_editor_layout.cpp
  window_layout_state.cpp
)
add_test(NAME test_lxe_editor_layout COMMAND test_lxe_editor_layout)
```

- [ ] **Step 4: Run the new red test**

Run:

```bash
cmake --build build --target test_lxe_editor_layout
./build/src/test/test_lxe_editor_layout
```

Expected: FAIL because the local layout persistence helper does not exist yet.

- [ ] **Step 5: Commit the red layout coverage**

```bash
git add src/test/integration/test_lxe_editor_layout.cpp src/demos/lxe_editor/CMakeLists.txt
git commit -m "test: add scene viewer layout persistence coverage"
```

## Task 6: Implement local window/layout persistence helpers

**Files:**
- Create: `src/demos/lxe_editor/window_layout_state.hpp`
- Create: `src/demos/lxe_editor/window_layout_state.cpp`
- Modify: `src/infra/gui/imgui_gui.cpp`
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`

- [ ] **Step 1: Define a narrow local persistence value type**

Create `window_layout_state.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>

namespace LX_demo::lxe_editor {

struct WindowPoint final {
  int x = 0;
  int y = 0;
};

struct WindowSize final {
  int width = 1280;
  int height = 720;
};

struct WindowLayoutState final {
  std::optional<WindowPoint> position;
  std::optional<WindowSize> size;
  bool maximized = false;
};

[[nodiscard]] WindowLayoutState loadWindowLayoutState(const std::filesystem::path& path);
void saveWindowLayoutState(const std::filesystem::path& path,
                           const WindowLayoutState& state);
[[nodiscard]] std::filesystem::path defaultSceneViewerLayoutDir();
[[nodiscard]] std::filesystem::path defaultSceneViewerImGuiIniPath();
[[nodiscard]] std::filesystem::path defaultSceneViewerWindowStatePath();

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement structured YAML load/save**

Create `window_layout_state.cpp` using yaml-cpp with soft-failure behavior:

```cpp
WindowLayoutState loadWindowLayoutState(const std::filesystem::path& path) {
  WindowLayoutState state;
  if (!std::filesystem::exists(path)) {
    return state;
  }
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    if (const YAML::Node position = root["position"]; position && position.IsSequence() &&
        position.size() == 2) {
      state.position = WindowPoint{position[0].as<int>(), position[1].as<int>()};
    }
    if (const YAML::Node size = root["size"]; size && size.IsSequence() &&
        size.size() == 2) {
      state.size = WindowSize{size[0].as<int>(), size[1].as<int>()};
    }
    state.maximized = root["maximized"] ? root["maximized"].as<bool>() : false;
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] ignoring invalid window layout state '"
              << path.string() << "': " << e.what() << "\n";
  }
  return state;
}
```

Create parent directories before save.

- [ ] **Step 3: Point ImGui ini persistence at local `data/`**

In `src/infra/gui/imgui_gui.cpp`, replace:

```cpp
ImGui::GetIO().IniFilename = nullptr;
```

with a local-file path injected from the caller or a dedicated helper. If the current `Gui::InitParams` lacks an ini-path field, add one and thread it through:

```cpp
std::string m_iniFilenameStorage = params.iniFilename;
ImGui::GetIO().IniFilename = m_iniFilenameStorage.empty()
                                 ? nullptr
                                 : m_iniFilenameStorage.c_str();
```

Keep `ConfigWindowsMoveFromTitleBarOnly = true`.

- [ ] **Step 4: Rebuild and rerun the layout test**

Run:

```bash
cmake --build build --target test_lxe_editor_layout
./build/src/test/test_lxe_editor_layout
```

Expected: PASS for missing-file fallback and YAML round-trip.

- [ ] **Step 5: Commit the persistence helper**

```bash
git add src/demos/lxe_editor/window_layout_state.hpp src/demos/lxe_editor/window_layout_state.cpp src/infra/gui/imgui_gui.cpp src/test/integration/test_lxe_editor_layout.cpp
git commit -m "feat: add local scene viewer layout state"
```

## Task 7: Extend the window abstraction for geometry restore

**Files:**
- Modify: `src/core/platform/window.hpp`
- Modify: `src/infra/window/window.hpp`
- Modify: `src/infra/window/sdl_window.cpp`
- Modify: `src/infra/window/glfw_window.cpp`

- [ ] **Step 1: Add geometry query/apply APIs to the core window interface**

Extend `src/core/platform/window.hpp` with narrow value types and methods:

```cpp
struct WindowRect final {
  int x = 0;
  int y = 0;
  int width = 1280;
  int height = 720;
};

class Window {
public:
  [[nodiscard]] virtual std::optional<WindowRect> getWindowRect() const = 0;
  [[nodiscard]] virtual bool isMaximized() const = 0;
  virtual void setWindowRect(const WindowRect& rect) = 0;
  virtual void setMaximized(bool maximized) = 0;
};
```

If the repo already defines similar geometry types elsewhere, reuse them instead of inventing duplicates.

- [ ] **Step 2: Mirror the API in the infra wrapper**

Update `src/infra/window/window.hpp` to override the new methods and keep the concrete wrapper aligned with the core interface.

- [ ] **Step 3: Implement the SDL backend**

In `src/infra/window/sdl_window.cpp`, use SDL calls such as:

```cpp
SDL_GetWindowPosition(window, &x, &y);
SDL_GetWindowSize(window, &w, &h);
SDL_MaximizeWindow(window);
SDL_RestoreWindow(window);
SDL_SetWindowPosition(window, rect.x, rect.y);
SDL_SetWindowSize(window, rect.width, rect.height);
```

Persist only valid non-zero sizes.

- [ ] **Step 4: Implement or consciously match the GLFW backend**

If `src/infra/window/glfw_window.cpp` exists and is still built, implement the same contract with GLFW calls such as `glfwGetWindowPos`, `glfwSetWindowPos`, `glfwGetWindowSize`, `glfwSetWindowSize`, and `glfwMaximizeWindow`.

- [ ] **Step 5: Rebuild key targets**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_layout
```

Expected: PASS at compile time with both window backends satisfying the new interface.

- [ ] **Step 6: Commit the window API extension**

```bash
git add src/core/platform/window.hpp src/infra/window/window.hpp src/infra/window/sdl_window.cpp src/infra/window/glfw_window.cpp
git commit -m "feat: add window geometry restore api"
```

## Task 8: Wire startup/shutdown restore in `lxe_editor`

**Files:**
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/demos/lxe_editor/README.md`
- Modify: `notes/subsystems/scene.md`
- Test: `src/test/integration/test_lxe_editor_layout.cpp`

- [ ] **Step 1: Load local layout paths at startup**

In `main.cpp`, derive stable local paths:

```cpp
const std::filesystem::path layoutDir =
    lxe_editor::defaultSceneViewerLayoutDir();
const std::filesystem::path imguiIniPath =
    lxe_editor::defaultSceneViewerImGuiIniPath();
const std::filesystem::path windowStatePath =
    lxe_editor::defaultSceneViewerWindowStatePath();
const lxe_editor::WindowLayoutState windowState =
    lxe_editor::loadWindowLayoutState(windowStatePath);
```

Apply the loaded geometry before or immediately after window creation, depending on backend constraints.

- [ ] **Step 2: Thread ImGui ini path into GUI init**

If needed, extend the GUI init parameter object and pass:

```cpp
gui.init({
  .nativeWindowHandle = window->getNativeHandle(),
  .iniFilename = imguiIniPath.string(),
  // existing Vulkan fields...
});
```

This ensures panel layout and collapsed state restore through ImGui itself.

- [ ] **Step 3: Save window state on clean shutdown**

At the same shutdown point where scene save prompts and runtime teardown already happen, snapshot:

```cpp
if (const auto rect = window->getWindowRect(); rect.has_value()) {
  lxe_editor::WindowLayoutState state;
  state.position = lxe_editor::WindowPoint{rect->x, rect->y};
  state.size = lxe_editor::WindowSize{rect->width, rect->height};
  state.maximized = window->isMaximized();
  lxe_editor::saveWindowLayoutState(windowStatePath, state);
}
```

Let ImGui save its ini file through its normal shutdown path.

- [ ] **Step 4: Update human-facing docs**

Document in `src/demos/lxe_editor/README.md`:

- Scene Tree now shows an explicit `root`
- scene files now save a canonical `root:` hierarchy
- `lxe_editor` restores local layout from `data/lxe_editor/`

Update `notes/subsystems/scene.md` to remove the stale "synthetic path root" description and describe the real root-node model instead.

- [ ] **Step 5: Run full verification**

Run:

```bash
cmake --build build --target lxe_editor test_scene_document test_scene_runtime test_scene_node_validation test_lxe_editor_layout test_command_bus
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
./build/src/test/test_scene_node_validation
./build/src/test/test_lxe_editor_layout
./build/src/test/test_command_bus
timeout 8s xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor
```

Expected:

- all tests pass
- `lxe_editor` starts successfully without requiring preexisting layout files

- [ ] **Step 6: Perform the manual acceptance check**

Manual check in `lxe_editor`:

1. Confirm Scene Tree shows `root` as the top row.
2. Load a scene and confirm authored nodes appear under `root`.
3. Move panels, resize/maximize the main window, then close the app.
4. Relaunch and confirm panel layout plus window geometry restored.
5. Load an older flat-format scene, save it, and inspect the saved file for the new `root:` structure.

- [ ] **Step 7: Commit the integration wiring**

```bash
git add src/demos/lxe_editor/main.cpp src/demos/lxe_editor/README.md notes/subsystems/scene.md
git commit -m "feat: persist scene viewer layout and explicit root"
```

## Self-Review

- Spec coverage:
  - explicit runtime root: Tasks 1-4
  - explicit-root scene serialization plus legacy normalization: Task 3
  - Scene Tree root display: Task 4
  - local ImGui/native window persistence under `data/`: Tasks 5-8
  - restore size, position, maximized state only: Tasks 6-8
- Placeholder scan:
  - no `TODO`/`TBD` placeholders remain
  - each task includes concrete files, commands, and expected outcomes
- Type consistency:
  - root access uses `getRootNode()` and `getTopLevelAuthoredNodes()`
  - local layout state uses `WindowLayoutState`, `WindowPoint`, and `WindowSize` consistently across Tasks 5-8
