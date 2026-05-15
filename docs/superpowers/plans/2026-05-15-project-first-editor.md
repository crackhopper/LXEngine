# Project-First Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the editor's `asset/local` scene-source model with `project_template -> project -> scene`, where one project owns multiple scenes and no compatibility layer preserves old source-kind behavior.

**Architecture:** Add project metadata/catalog/session objects beside the existing scene document/runtime code, then move `LxeEditorSession` to use `ProjectSession` as the save/open authority. Scene commands remain user-facing, but they resolve scene ids and paths only within the current project. API, tests, and docs expose project state instead of source kind state.

**Tech Stack:** C++20, yaml-cpp, CMake/Ninja, existing `LX_core::CommandBus`, Python unittest blackbox tests, Markdown notes.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/demos/lxe_editor/project_document.hpp` | Create | Define `ProjectTemplateDocument`, `ProjectDocument`, scene entries, and project dirty fields. |
| `src/demos/lxe_editor/project_document.cpp` | Create | Load/save `project_template.yaml` and `project.yaml` with yaml-cpp. |
| `src/demos/lxe_editor/project_catalog.hpp` | Create | Enumerate `assets/project_templates/` and `data/projects/`. |
| `src/demos/lxe_editor/project_catalog.cpp` | Create | Resolve template ids, project ids, project paths, and project-relative scene paths. |
| `src/demos/lxe_editor/project_session.hpp` | Create | Hold current project, active scene, dirty state, and project/scene save decisions. |
| `src/demos/lxe_editor/project_session.cpp` | Create | Implement project init/open/save/close and scene list/open/new/duplicate/remove decisions. |
| `src/demos/lxe_editor/scene_catalog.*` | Delete after replacement | Remove old asset/local catalog implementation. |
| `src/demos/lxe_editor/scene_session.*` | Delete after replacement | Remove old asset/local save redirection implementation. |
| `src/demos/lxe_editor/scene_runtime.*` | Modify | Remove `SceneSourceKind`; keep document path only. |
| `src/demos/lxe_editor/editor_data_state.*` | Modify | Persist `lastProject` in `data/lxe_editor/editor_data.yaml`. |
| `src/demos/lxe_editor/editor_session.*` | Modify | Wire `ProjectSession` into startup, commands, deferred scene opening, save, and dirty tracking. |
| `src/demos/lxe_editor/lxe_editor_commands.*` | Modify | Add project commands and change scene commands to project-scoped semantics. |
| `src/demos/lxe_editor/lxe_editor_api_protocol.*` | Modify | Replace `ApiSceneSourceKind/currentDocumentPath` with project state. |
| `src/demos/lxe_editor/lxe_editor_api_service.*` | Modify | Emit project-aware snapshots/events and detect `project` / `scene open` / `scene save`. |
| `src/demos/lxe_editor/lxe_editor_api_server.cpp` | Modify | Serialize project state instead of source kind fields. |
| `src/demos/lxe_editor/main.cpp` | Modify | Build API state from `ProjectSession` accessors. |
| `src/demos/lxe_editor/CMakeLists.txt` | Modify | Add project files and remove scene catalog/session files. |
| `src/test/integration/test_project_document.cpp` | Create | Unit coverage for project/template YAML parsing and writing. |
| `src/test/integration/test_project_catalog.cpp` | Create | Unit coverage for template/project enumeration and scene path resolution. |
| `src/test/integration/test_project_session.cpp` | Create | Unit coverage for init/open/save/scene operations and no-open-project failures. |
| `src/test/integration/test_scene_catalog.cpp` | Delete or replace target | Remove tests for asset/local source classification. |
| `src/test/integration/test_scene_session.cpp` | Delete or replace target | Remove tests for asset-to-local save redirection. |
| `src/test/integration/test_command_bus.cpp` | Modify | Update command registration behavior for project and scene commands. |
| `src/test/integration/test_lxe_editor_session.cpp` | Modify | Update integration tests from `scene load` to project init/open and `scene open`. |
| `src/test/integration/test_lxe_editor_api_service.cpp` | Modify | Assert project snapshots/events. |
| `src/test/integration/test_lxe_editor_api_server.cpp` | Modify | Assert JSON project payload. |
| `src/test/CMakeLists.txt` | Modify | Register new tests and remove old scene catalog/session targets. |
| `tests/lxe_editor/test_scene_io.py` | Modify | Use `project init` / project-scoped `scene save/open`; assert project JSON. |
| `assets/project_templates/empty/project_template.yaml` | Create | Minimal starter template. |
| `assets/project_templates/empty/scenes/main.scene.yaml` | Create | Empty starter scene copied into projects. |
| `notes/tutorial/start-project/03-load-and-save-scene.md` | Modify | Teach project init/open/save and project-scoped scenes. |
| `notes/design/editor-system/04-scene-runtime-and-persistence.md` | Modify | Replace asset/local persistence explanation with project/session design. |
| `notes/design/editor-system/05-api-recording-and-observation.md` | Modify | Replace source-kind API explanation with project events/state. |
| `notes/subsystems/scene.md` | Modify | Describe current scene runtime as project-owned documents. |
| `src/demos/lxe_editor/README.md` | Modify | Update command reference and remove admin/source-kind save rules. |

## Task 1: Project YAML Documents

**Files:**
- Create: `src/demos/lxe_editor/project_document.hpp`
- Create: `src/demos/lxe_editor/project_document.cpp`
- Create: `src/test/integration/test_project_document.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write the failing project document test**

Create `src/test/integration/test_project_document.cpp` with tests for template load and project save/load:

```cpp
#include "demos/lxe_editor/project_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace demo = LX_demo::lxe_editor;

namespace {
int failures = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { std::cerr << "[FAIL] " << __LINE__ << " " << msg << "\n"; ++failures; } } while (0)

void writeFile(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

void testTemplateDocumentLoadsCopyRoots() {
  const auto root = std::filesystem::temp_directory_path() / "lx_project_doc_template";
  std::filesystem::remove_all(root);
  const auto path = root / "project_template.yaml";
  writeFile(path,
            "schema: lxe.project_template.v1\n"
            "id: basic-3d\n"
            "displayName: Basic 3D\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - scenes/\n"
            "  - assets/\n");

  const auto document = demo::loadProjectTemplateDocument(path);

  EXPECT(document.id == "basic-3d", "template id should load");
  EXPECT(document.defaultScene == std::filesystem::path("scenes/main.scene.yaml"),
         "default scene should load as a relative path");
  EXPECT(document.copyRoots.size() == 2, "copy roots should load");
}

void testProjectDocumentRoundTripsScenes() {
  const auto root = std::filesystem::temp_directory_path() / "lx_project_doc_project";
  std::filesystem::remove_all(root);
  const auto path = root / "project.yaml";
  demo::ProjectDocument document;
  document.id = "my_project";
  document.displayName = "My Project";
  document.activeScene = "scenes/main.scene.yaml";
  document.scenes.push_back({"main", "scenes/main.scene.yaml"});
  document.scenes.push_back({"lighting_test", "scenes/lighting_test.scene.yaml"});
  document.assetRoots.push_back("assets/");
  document.createdFromTemplate = "basic-3d";

  EXPECT(demo::saveProjectDocument(path, document), "project document should save");
  const auto loaded = demo::loadProjectDocument(path);

  EXPECT(loaded.id == "my_project", "project id should round trip");
  EXPECT(loaded.scenes.size() == 2, "scene entries should round trip");
  EXPECT(loaded.scenes[1].id == "lighting_test", "second scene id should round trip");
  EXPECT(loaded.assetRoots.size() == 1, "asset roots should round trip");
}
} // namespace

int main() {
  testTemplateDocumentLoadsCopyRoots();
  testProjectDocumentRoundTripsScenes();
  if (failures != 0) {
    return 1;
  }
  std::cout << "test_project_document passed\n";
  return 0;
}
```

- [ ] **Step 2: Register and run the failing test**

Modify `src/test/CMakeLists.txt` by inserting `test_project_document` in `TEST_INTEGRATION_EXE_LIST` immediately after `test_scene_document`, then add the target block below the existing lxe editor test target blocks:

```cmake
if(TARGET test_project_document)
  target_sources(test_project_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
  )
  target_include_directories(test_project_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_project_document PRIVATE yaml-cpp::yaml-cpp)
endif()
```

Run:

```bash
ninja test_project_document
```

Expected: compile fails because `project_document.hpp` does not exist.

- [ ] **Step 3: Implement document types and YAML IO**

Create `src/demos/lxe_editor/project_document.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct ProjectSceneEntry final {
  std::string id;
  std::filesystem::path path;
};

struct ProjectTemplateDocument final {
  std::string schema = "lxe.project_template.v1";
  std::string id;
  std::string displayName;
  std::filesystem::path defaultScene;
  std::vector<std::filesystem::path> copyRoots;
};

struct ProjectDocument final {
  std::string schema = "lxe.project.v1";
  std::string id;
  std::string displayName;
  std::filesystem::path activeScene;
  std::vector<ProjectSceneEntry> scenes;
  std::vector<std::filesystem::path> assetRoots;
  std::optional<std::string> createdFromTemplate;
};

[[nodiscard]] ProjectTemplateDocument
loadProjectTemplateDocument(const std::filesystem::path& path);
[[nodiscard]] ProjectDocument loadProjectDocument(const std::filesystem::path& path);
bool saveProjectDocument(const std::filesystem::path& path,
                         const ProjectDocument& document);

} // namespace LX_demo::lxe_editor
```

Create `src/demos/lxe_editor/project_document.cpp` with yaml-cpp parsing that validates exact schemas `lxe.project_template.v1` and `lxe.project.v1`, requires `id`, `displayName`, `defaultScene` or `activeScene`, and emits `scenes`, `assetRoots`, and `createdFromTemplate` in the shape from the design spec.

- [ ] **Step 4: Verify Task 1**

Run:

```bash
ninja test_project_document && ./src/test/test_project_document
```

Expected: build succeeds and output includes `test_project_document passed`.

- [ ] **Step 5: Commit Task 1**

```bash
git add src/demos/lxe_editor/project_document.* src/test/integration/test_project_document.cpp src/test/CMakeLists.txt
git commit -m "Add project document metadata IO"
```

## Task 2: Project Catalogs And Starter Template

**Files:**
- Create: `src/demos/lxe_editor/project_catalog.hpp`
- Create: `src/demos/lxe_editor/project_catalog.cpp`
- Create: `src/test/integration/test_project_catalog.cpp`
- Create: `assets/project_templates/empty/project_template.yaml`
- Create: `assets/project_templates/empty/scenes/main.scene.yaml`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write catalog tests**

Create `src/test/integration/test_project_catalog.cpp`:

```cpp
#include "demos/lxe_editor/project_catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace demo = LX_demo::lxe_editor;

namespace {
int failures = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { std::cerr << "[FAIL] " << __LINE__ << " " << msg << "\n"; ++failures; } } while (0)

void writeFile(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

void testCatalogListsTemplatesAndProjects() {
  const auto root = std::filesystem::temp_directory_path() / "lx_project_catalog";
  std::filesystem::remove_all(root);
  writeFile(root / "assets/project_templates/empty/project_template.yaml",
            "schema: lxe.project_template.v1\nid: empty\ndisplayName: Empty\ndefaultScene: scenes/main.scene.yaml\ncopy:\n  - scenes/\n");
  writeFile(root / "data/projects/demo/project.yaml",
            "schema: lxe.project.v1\nid: demo\ndisplayName: Demo\nactiveScene: scenes/main.scene.yaml\nscenes:\n  - id: main\n    path: scenes/main.scene.yaml\n");

  demo::ProjectTemplateCatalog templates(root / "assets/project_templates");
  demo::ProjectCatalog projects(root / "data/projects");
  templates.refresh();
  projects.refresh();

  EXPECT(templates.entries().size() == 1, "one template should be listed");
  EXPECT(templates.findById("empty").has_value(), "template id should resolve");
  EXPECT(projects.entries().size() == 1, "one project should be listed");
  EXPECT(projects.findById("demo").has_value(), "project id should resolve");
}

void testSceneResolutionStaysInsideProject() {
  const auto root = std::filesystem::temp_directory_path() / "lx_project_catalog_scene";
  std::filesystem::remove_all(root);
  writeFile(root / "data/projects/demo/project.yaml",
            "schema: lxe.project.v1\nid: demo\ndisplayName: Demo\nactiveScene: scenes/main.scene.yaml\nscenes:\n  - id: main\n    path: scenes/main.scene.yaml\n");
  demo::ProjectDocument document = demo::loadProjectDocument(root / "data/projects/demo/project.yaml");

  const auto resolved = demo::resolveProjectScenePath(root / "data/projects/demo", document, "main");
  EXPECT(resolved == std::filesystem::absolute(root / "data/projects/demo/scenes/main.scene.yaml").lexically_normal(),
         "scene id should resolve under project root");
}
} // namespace

int main() {
  testCatalogListsTemplatesAndProjects();
  testSceneResolutionStaysInsideProject();
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register and run the failing test**

Add `test_project_catalog` to `src/test/CMakeLists.txt` and link `project_catalog.cpp` plus `project_document.cpp`.

Run:

```bash
ninja test_project_catalog
```

Expected: compile fails because `project_catalog.hpp` does not exist.

- [ ] **Step 3: Implement catalog APIs**

Create `project_catalog.hpp` with:

```cpp
#pragma once

#include "demos/lxe_editor/project_document.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct ProjectTemplateCatalogEntry final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
};

struct ProjectCatalogEntry final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
};

class ProjectTemplateCatalog final {
public:
  explicit ProjectTemplateCatalog(std::filesystem::path root);
  void refresh();
  [[nodiscard]] const std::vector<ProjectTemplateCatalogEntry>& entries() const;
  [[nodiscard]] std::optional<ProjectTemplateCatalogEntry>
  findById(const std::string& id) const;

private:
  std::filesystem::path m_root;
  std::vector<ProjectTemplateCatalogEntry> m_entries;
};

class ProjectCatalog final {
public:
  explicit ProjectCatalog(std::filesystem::path root);
  void refresh();
  [[nodiscard]] const std::vector<ProjectCatalogEntry>& entries() const;
  [[nodiscard]] std::optional<ProjectCatalogEntry>
  findById(const std::string& id) const;
  [[nodiscard]] std::filesystem::path resolveIdOrPath(const std::string& token) const;

private:
  std::filesystem::path m_root;
  std::vector<ProjectCatalogEntry> m_entries;
};

[[nodiscard]] std::filesystem::path
resolveProjectScenePath(const std::filesystem::path& projectRoot,
                        const ProjectDocument& document,
                        const std::string& sceneIdOrPath);
[[nodiscard]] std::string makeProjectSlug(std::string name);
[[nodiscard]] std::filesystem::path
allocateProjectPath(const std::filesystem::path& projectsRoot,
                    const std::string& requestedName);

} // namespace LX_demo::lxe_editor
```

Implement `makeProjectSlug()` as lowercase, replacing non `[a-z0-9_-]` characters with `_`, defaulting to `project`, and suffixing `-2`, `-3` in `allocateProjectPath()` when a directory exists.

- [ ] **Step 4: Add the empty template asset**

Create `assets/project_templates/empty/project_template.yaml`:

```yaml
schema: lxe.project_template.v1
id: empty
displayName: Empty
defaultScene: scenes/main.scene.yaml
copy:
  - scenes/
  - assets/
```

Create `assets/project_templates/empty/scenes/main.scene.yaml`:

```yaml
scene:
  name: Empty Project
nodes: []
```

Create `assets/project_templates/empty/assets/.gitkeep` so the copy root exists.

- [ ] **Step 5: Verify Task 2**

Run:

```bash
ninja test_project_document test_project_catalog && ./src/test/test_project_catalog
```

Expected: both tests pass.

- [ ] **Step 6: Commit Task 2**

```bash
git add src/demos/lxe_editor/project_catalog.* src/test/integration/test_project_catalog.cpp src/test/CMakeLists.txt assets/project_templates/empty
git commit -m "Add project catalogs and empty template"
```

## Task 3: Project Session Save/Open Decisions

**Files:**
- Create: `src/demos/lxe_editor/project_session.hpp`
- Create: `src/demos/lxe_editor/project_session.cpp`
- Create: `src/test/integration/test_project_session.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write session tests**

Create `src/test/integration/test_project_session.cpp` with tests for:

```cpp
void testSaveFailsWithoutOpenProject();
void testInitCopiesTemplateAndOpensDefaultScene();
void testSceneNewAddsProjectSceneEntry();
void testSceneOpenRejectsPathOutsideProjectRoot();
void testProjectCloseReturnsToNoProjectState();
```

The concrete assertions are:

```cpp
EXPECT(!session.hasProject(), "new session should not have project");
EXPECT(!session.saveProject().ok, "save should fail without project");
EXPECT(std::filesystem::exists(projectRoot / "project.yaml"), "project init should create project.yaml");
EXPECT(session.activeScenePath().filename() == "main.scene.yaml", "template default scene should open");
EXPECT(document.scenes.size() == 2, "scene new should add second scene");
EXPECT(!session.openScene("../outside.scene.yaml").ok, "scene path should not escape project root");
```

- [ ] **Step 2: Register and run the failing test**

Add `test_project_session` to `src/test/CMakeLists.txt` and link `project_session.cpp`, `project_catalog.cpp`, and `project_document.cpp`.

Run:

```bash
ninja test_project_session
```

Expected: compile fails because `project_session.hpp` does not exist.

- [ ] **Step 3: Implement `ProjectSession`**

Create `project_session.hpp` with value-returning command decision methods:

```cpp
struct ProjectCommandResult final {
  bool ok = false;
  std::string message;
  std::string structuredJson;
};

class ProjectSession final {
public:
  ProjectSession(std::filesystem::path templateRoot,
                 std::filesystem::path projectsRoot);

  [[nodiscard]] bool hasProject() const;
  [[nodiscard]] bool dirty() const;
  void setDirty(bool dirty);
  [[nodiscard]] const std::optional<ProjectDocument>& currentProject() const;
  [[nodiscard]] const std::optional<std::filesystem::path>& projectRoot() const;
  [[nodiscard]] std::optional<std::filesystem::path> activeScenePath() const;

  [[nodiscard]] ProjectCommandResult initProject(const std::string& templateId,
                                                 const std::optional<std::string>& projectName);
  [[nodiscard]] ProjectCommandResult openProject(const std::string& idOrPath);
  [[nodiscard]] ProjectCommandResult saveProject();
  [[nodiscard]] ProjectCommandResult closeProject();
  [[nodiscard]] ProjectCommandResult openScene(const std::string& sceneIdOrPath);
  [[nodiscard]] ProjectCommandResult newScene(const std::string& sceneId);
  [[nodiscard]] ProjectCommandResult duplicateScene(const std::string& sourceSceneId,
                                                    const std::string& newSceneId);
  [[nodiscard]] ProjectCommandResult removeScene(const std::string& sceneId);
};
```

Implement file copying with `std::filesystem::copy(sourcePath, targetPath, copy_options::recursive | copy_options::overwrite_existing)` for each template `copyRoot`. Reject scene paths that normalize outside the current project root.

- [ ] **Step 4: Verify Task 3**

Run:

```bash
ninja test_project_session && ./src/test/test_project_session
```

Expected: `test_project_session` passes.

- [ ] **Step 5: Commit Task 3**

```bash
git add src/demos/lxe_editor/project_session.* src/test/integration/test_project_session.cpp src/test/CMakeLists.txt
git commit -m "Add project session workflow"
```

## Task 4: Remove Source Kind From Scene Runtime

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.hpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Write/update runtime expectations**

Update tests so runtime only exposes document path:

```cpp
runtime.loadFromDocumentPath(scenePath);
EXPECT(runtime.documentPath().has_value(), "runtime should remember loaded path");
```

Remove assertions that call `runtime.sourceKind()`.

- [ ] **Step 2: Remove `SceneSourceKind` parameters and storage**

Change signatures:

```cpp
void loadFromDocumentPath(const std::filesystem::path& path);
```

Remove:

```cpp
std::optional<SceneSourceKind> sourceKind() const;
std::optional<SceneSourceKind> sourceKind;
```

from `scene_runtime.hpp/.cpp`.

- [ ] **Step 3: Verify Task 4**

Run:

```bash
ninja test_scene_runtime && ./src/test/test_scene_runtime
```

Expected: scene runtime tests pass without source-kind APIs.

- [ ] **Step 4: Commit Task 4**

```bash
git add src/demos/lxe_editor/scene_runtime.* src/test/integration/test_scene_runtime.cpp
git commit -m "Remove scene source kind from runtime"
```

## Task 5: Editor Data Last Project

**Files:**
- Modify: `src/demos/lxe_editor/editor_data_state.hpp`
- Modify: `src/demos/lxe_editor/editor_data_state.cpp`
- Modify: `src/test/integration/test_lxe_editor_session.cpp` or create focused test if one exists for editor data.

- [ ] **Step 1: Add failing editor data assertions**

Add a test that writes:

```yaml
version: 1
lastProject: data/projects/demo
consoleHistory: []
```

and asserts:

```cpp
EXPECT(document.lastProject == std::filesystem::path("data/projects/demo"),
       "lastProject should load");
```

- [ ] **Step 2: Implement `lastProject`**

Change `EditorDataDocument`:

```cpp
struct EditorDataDocument final {
  int version = 1;
  std::optional<std::filesystem::path> lastProject;
  std::vector<std::string> consoleHistory;
};
```

In `load()`, read scalar `lastProject` when present. In `save()`, emit `lastProject` before `consoleHistory` when `has_value()`.

- [ ] **Step 3: Verify Task 5**

Run:

```bash
ninja test_lxe_editor_session && ./src/test/test_lxe_editor_session
```

Expected: editor session tests compile with the new field. If this task adds a focused editor-data test, run that target instead.

- [ ] **Step 4: Commit Task 5**

```bash
git add src/demos/lxe_editor/editor_data_state.* src/test/integration/test_lxe_editor_session.cpp
git commit -m "Persist last opened project"
```

## Task 6: Project And Scene Commands

**Files:**
- Modify: `src/demos/lxe_editor/lxe_editor_commands.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_commands.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Update command bus tests**

Replace old `scene load` command tests with:

```cpp
const CommandResult projectTemplates = fixture.bus.dispatch("project templates");
EXPECT(projectTemplates.ok, "project templates should route through project handler");

const CommandResult projectInit = fixture.bus.dispatch("project init empty demo");
EXPECT(projectInit.ok, "project init should route through project handler");

const CommandResult sceneOpen = fixture.bus.dispatch("scene open main");
EXPECT(sceneOpen.ok, "scene open should route through project-scoped handler");

const CommandResult sceneLoad = fixture.bus.dispatch("scene load main");
EXPECT(!sceneLoad.ok, "scene load should be removed");
EXPECT(sceneLoad.message.find("unknown command") != std::string::npos,
       "scene load should not be a compatibility alias");
```

Update undo/redo invalidation tests so `scene open` invalidates stale undo and redo history in the same places where `scene load` used to do so.

- [ ] **Step 2: Extend command context**

Replace source-kind callbacks with project callbacks:

```cpp
using ProjectCommandFn = std::function<LX_core::CommandResult(std::string_view)>;
using ProjectSummaryJsonFn = std::function<std::string()>;

ProjectCommandFn projectCommand;
ProjectCommandFn sceneCommand;
ProjectSummaryJsonFn projectSummaryJson;
```

Keep existing `dirty`, `permission`, and debug callbacks only where still used by non-project commands. Remove `CurrentSourceKindFn`.

- [ ] **Step 3: Register project and scene command handlers**

In `registerLxeEditorCommands`, register:

```cpp
bus.registerCommand("project", "Project workflow commands", [context](const auto& args) {
  return context.projectCommand(joinArgs(args));
});
bus.registerCommand("scene", "Project-scoped scene commands", [context](const auto& args) {
  return context.sceneCommand(joinArgs(args));
});
```

Inside the scene handler, support `list`, `open`, `save`, `new`, `duplicate`, `remove`, and `status`. Do not register or parse `scene load`.

- [ ] **Step 4: Verify Task 6**

Run:

```bash
ninja test_command_bus && ./src/test/test_command_bus
```

Expected: command bus tests pass and `scene load` is unknown.

- [ ] **Step 5: Commit Task 6**

```bash
git add src/demos/lxe_editor/lxe_editor_commands.* src/test/integration/test_command_bus.cpp
git commit -m "Route project scoped editor commands"
```

## Task 7: Wire `LxeEditorSession` To Project Session

**Files:**
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Delete: `src/demos/lxe_editor/scene_catalog.hpp`
- Delete: `src/demos/lxe_editor/scene_catalog.cpp`
- Delete: `src/demos/lxe_editor/scene_session.hpp`
- Delete: `src/demos/lxe_editor/scene_session.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`
- Modify: `src/test/integration/test_lxe_editor_session.cpp`
- Remove or replace: `src/test/integration/test_scene_catalog.cpp`
- Remove or replace: `src/test/integration/test_scene_session.cpp`

- [ ] **Step 1: Update editor session tests**

Change tests from:

```cpp
session.commandBus().dispatch("scene load lxe_editor.scene.yaml");
```

to:

```cpp
session.commandBus().dispatch("project init empty editor_session_test");
session.commandBus().dispatch("scene open main");
```

Add assertions:

```cpp
EXPECT(session.currentProjectId() == "editor_session_test",
       "project init should open the new project");
EXPECT(session.activeScenePath().has_value(), "project should expose active scene");
EXPECT(!session.currentSourceKind().has_value(),
       "source kind accessor should be removed from the test after code update");
```

The final assertion is temporary guidance for the edit: remove `currentSourceKind()` calls and replace them with `currentProjectId()` / `activeScenePath()`.

- [ ] **Step 2: Replace members in `editor_session.hpp`**

Use:

```cpp
ProjectSession m_projectSession;
std::optional<SceneRuntime> m_pendingRuntime;
std::optional<std::filesystem::path> m_pendingScenePath;
```

Remove:

```cpp
SceneCatalog m_catalog;
SceneSession m_session;
std::optional<SceneSourceKind> m_pendingSourceKind;
```

Expose:

```cpp
[[nodiscard]] std::optional<std::string> currentProjectId() const;
[[nodiscard]] std::optional<std::filesystem::path> currentProjectRoot() const;
[[nodiscard]] std::optional<std::filesystem::path> activeScenePath() const;
```

- [ ] **Step 3: Implement startup behavior**

In `initialize()`:

```cpp
m_editorData = m_editorDataState.load();
if (m_editorData.lastProject.has_value()) {
  const auto opened = m_projectSession.openProject(m_editorData.lastProject->string());
  if (opened.ok) {
    queueSceneOpenFromProjectActiveScene();
    flushPendingSceneLoad(loop) occurs on the next update tick as before;
  } else {
    m_runtime.createEmptyScene();
  }
} else {
  m_runtime.createEmptyScene();
}
```

If `initialize()` cannot access `EngineLoop`, keep runtime creation immediate by loading the active scene document into `m_runtime` directly after `openProject()`; preserve deferred loading only for runtime scene switches issued through commands.

- [ ] **Step 4: Implement project and scene command dispatchers**

Add private helpers:

```cpp
[[nodiscard]] LX_core::CommandResult handleProjectCommand(std::string_view args);
[[nodiscard]] LX_core::CommandResult handleSceneCommand(std::string_view args);
[[nodiscard]] LX_core::CommandResult queueSceneOpen(const std::string& sceneIdOrPath);
```

Rules:
- `project init` calls `ProjectSession::initProject`, loads the template default scene, updates `m_editorData.lastProject`, and saves editor data.
- `project open` calls `ProjectSession::openProject`, loads active scene, updates `lastProject`.
- `project save` saves project metadata, active scene via `SceneRuntime::saveToDocumentPath`, project editor state, and marks clean.
- `project close` clears current project, creates empty runtime scene, clears `lastProject`.
- `scene open` queues project-scoped scene runtime loading.
- `scene save` saves the active scene path from `ProjectSession`; without a project it fails with `project init` guidance.

- [ ] **Step 5: Delete old source-kind files and CMake entries**

Remove `scene_catalog.cpp`, `scene_catalog.hpp`, `scene_session.cpp`, and `scene_session.hpp`. In `src/demos/lxe_editor/CMakeLists.txt`, remove them and add:

```cmake
project_catalog.cpp
project_document.cpp
project_session.cpp
```

In `src/test/CMakeLists.txt`, remove `test_scene_catalog` and `test_scene_session` from `TEST_INTEGRATION_EXE_LIST` after project replacements are passing.

- [ ] **Step 6: Verify Task 7**

Run:

```bash
ninja test_lxe_editor_session test_project_session lxe_editor
./src/test/test_lxe_editor_session
```

Expected: editor session tests pass, `lxe_editor` links, and no compile errors reference `SceneCatalog`, `SceneSession`, or `SceneSourceKind`.

- [ ] **Step 7: Commit Task 7**

```bash
git add src/demos/lxe_editor src/test/CMakeLists.txt src/test/integration/test_lxe_editor_session.cpp
git rm src/demos/lxe_editor/scene_catalog.* src/demos/lxe_editor/scene_session.* src/test/integration/test_scene_catalog.cpp src/test/integration/test_scene_session.cpp
git commit -m "Move editor session to project workflow"
```

## Task 8: API Protocol, Service, And Server

**Files:**
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/test/integration/test_lxe_editor_api_service.cpp`
- Modify: `src/test/integration/test_lxe_editor_api_server.cpp`

- [ ] **Step 1: Update API tests first**

Replace snapshot setup:

```cpp
hookState.scene.currentDocumentPath = "data/scenes/test.scene.yaml";
hookState.scene.sourceKind = ApiSceneSourceKind::Local;
```

with:

```cpp
hookState.project.id = "demo";
hookState.project.displayName = "Demo";
hookState.project.path = "data/projects/demo";
hookState.project.dirty = true;
hookState.project.activeScene = "scenes/main.scene.yaml";
```

Assert serialized JSON contains:

```json
"project":{"id":"demo","displayName":"Demo","path":"data/projects/demo","dirty":true,"activeScene":"scenes/main.scene.yaml"}
```

and does not contain `sourceKind` or `currentDocumentPath`.

- [ ] **Step 2: Replace protocol structs**

In `lxe_editor_api_protocol.hpp`, delete `ApiSceneSourceKind` and replace scene summary fields with:

```cpp
struct ApiProjectSummary final {
  std::string id;
  std::string displayName;
  std::string path;
  bool dirty = false;
  std::string activeScene;
};
```

Embed it in the API state as `project`. If no project is open, serialize `project` as `null`.

- [ ] **Step 3: Update event classification**

In `lxe_editor_api_service.cpp`, replace `isSceneLoadCommand()` with `isSceneOpenCommand()`:

```cpp
return line == "scene open" || line.starts_with("scene open ");
```

Add project command event routing for `project init`, `project open`, `project save`, and `project close`. Event names are `ProjectInitialized`, `ProjectOpened`, `ProjectSaved`, `ProjectClosed`, `ActiveSceneChanged`, and `SceneSaved`.

- [ ] **Step 4: Wire main API state**

In `main.cpp`, replace the `sceneSourceKindName` helper and API state mapping with calls to:

```cpp
session.currentProjectId();
session.currentProjectRoot();
session.activeScenePath();
session.isDirty();
```

Build `ApiProjectSummary` only when `currentProjectId()` has a value.

- [ ] **Step 5: Verify Task 8**

Run:

```bash
ninja test_lxe_editor_api_service test_lxe_editor_api_server lxe_editor
./src/test/test_lxe_editor_api_service
./src/test/test_lxe_editor_api_server
```

Expected: tests pass and `rg -n "ApiSceneSourceKind|sourceKind|currentDocumentPath" src/demos/lxe_editor src/test/integration` returns no source-kind API references.

- [ ] **Step 6: Commit Task 8**

```bash
git add src/demos/lxe_editor/lxe_editor_api_* src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_editor_api_*.cpp
git commit -m "Expose project state through editor API"
```

## Task 9: Blackbox Tests And Recording Fixtures

**Files:**
- Modify: `tests/lxe_editor/test_scene_io.py`
- Modify: `src/test/integration/test_lxe_editor_recording.cpp`
- Modify: `src/test/integration/test_lxe_editor_memory_probe.cpp`
- Modify: `src/test/integration/test_lxe_editor_layout.cpp`
- Modify: `notes/use_cases/lxe_editor/record-complex-scene-edit.md`

- [ ] **Step 1: Update Python blackbox scene IO**

Replace:

```python
load_response = self.harness.client.command("scene load lxe_editor.scene.yaml")
```

with:

```python
init_response = self.harness.client.command("project init empty scene_io_test")
self.assertTrue(init_response["ok"])
open_response = self.harness.client.command("scene open main")
self.assertTrue(open_response["ok"])
```

Update scene state assertions to inspect `state["project"]["activeScene"]`, not `sourceKind`.

- [ ] **Step 2: Update recording command fixtures**

Replace recording fixture payloads:

```json
{"line":"scene load lxe_editor.scene.yaml"}
```

with:

```json
{"line":"project init empty recording_test"}
{"line":"scene open main"}
```

Update expected text assertions from `scene load` to `scene open`.

- [ ] **Step 3: Verify Task 9**

Run:

```bash
ninja test_lxe_editor_recording test_lxe_editor_memory_probe test_lxe_editor_layout
./src/test/test_lxe_editor_recording
./src/test/test_lxe_editor_layout
```

For Python blackbox tests, run when a video device is available:

```bash
ctest --output-on-failure -R test_lxe_editor_api_blackbox
```

Expected: C++ tests pass; Python test passes in the managed editor environment or reports only documented video-device availability issues.

- [ ] **Step 4: Commit Task 9**

```bash
git add tests/lxe_editor src/test/integration/test_lxe_editor_recording.cpp src/test/integration/test_lxe_editor_memory_probe.cpp src/test/integration/test_lxe_editor_layout.cpp notes/use_cases/lxe_editor/record-complex-scene-edit.md
git commit -m "Update editor tests for project scenes"
```

## Task 10: Notes And User-Facing Docs

**Files:**
- Modify: `notes/tutorial/start-project/03-load-and-save-scene.md`
- Modify: `notes/tutorial/start-project/index.md`
- Modify: `notes/tutorial/start-project/05-troubleshooting.md`
- Modify: `notes/design/editor-system/04-scene-runtime-and-persistence.md`
- Modify: `notes/design/editor-system/05-api-recording-and-observation.md`
- Modify: `notes/subsystems/scene.md`
- Modify: `src/demos/lxe_editor/README.md`
- Modify: `notes/releases/v0.1.0/CHANGELOG.md` only if it describes current completed behavior inaccurately after the code change.

- [ ] **Step 1: Rewrite the start-project save/load tutorial**

Use `writing-notes` style: explain project as the folder that owns scenes and assets, template as read-only starting material, scene as a document inside the project. Replace the old command sequence with:

```text
project templates
project init empty my_first_project
project status
scene list
scene new lighting_test
scene open lighting_test
scene save
project save
```

- [ ] **Step 2: Rewrite editor design docs in causal order**

In `04-scene-runtime-and-persistence.md`, organize sections as:

```text
project 为什么是顶层工作单元
project_template 如何变成 project
scene runtime 只关心当前 scene 文档
project save 与 scene save 的边界
启动时如何恢复 lastProject
```

In `05-api-recording-and-observation.md`, organize sections as:

```text
API 为什么暴露 project 而不是文件来源
命令事件如何从 project/scene 命令生成
录制如何保持可重放
MCP/HTTP 如何共用同一套命令语义
```

- [ ] **Step 3: Update subsystem and README command reference**

Remove user-facing mentions of `asset/local`, `admin on`, and asset-to-local redirect. Replace with project command table:

```markdown
| Command | Meaning |
|---|---|
| `project templates` | 列出只读 project_template |
| `project init <type> [name]` | 从模板创建并打开 project |
| `project open <id-or-path>` | 打开已有 project |
| `project save` | 保存 project metadata、active scene、editor_state |
| `scene list` | 列出当前 project 的 scenes |
| `scene open <id-or-path>` | 打开当前 project 内的 scene |
| `scene save` | 保存当前 project 的 active scene |
```

- [ ] **Step 4: Verify docs do not preserve old user-facing terms**

Run:

```bash
rg -n "asset/local|sourceKind|scene load|asset-to-local|redirectedFromAsset|admin on" notes src/demos/lxe_editor/README.md tests/lxe_editor src/test/integration
```

Expected: no user-facing old model references remain. Source code may still contain unrelated words `asset` for actual assets, but not `asset/local` source-kind semantics.

- [ ] **Step 5: Commit Task 10**

```bash
git add notes src/demos/lxe_editor/README.md
git commit -m "Document project-first editor workflow"
```

## Task 11: Final Cleanup And Verification

**Files:**
- Modify any remaining files reported by search.

- [ ] **Step 1: Remove old identifiers from source**

Run:

```bash
rg -n "SceneSourceKind|SceneCatalog|SceneSession|ApiSceneSourceKind|sourceKind|currentSourceKind|currentDocumentPath|scene load|redirectedFromAsset" src tests notes
```

Expected: no matches for removed editor source-kind concepts. Investigate each remaining match; either delete it or rename it to project terminology.

- [ ] **Step 2: Build focused targets**

Run:

```bash
ninja lxe_editor test_project_document test_project_catalog test_project_session test_command_bus test_lxe_editor_session test_lxe_editor_api_service test_lxe_editor_api_server
```

Expected: all targets build.

- [ ] **Step 3: Run focused tests**

Run:

```bash
./src/test/test_project_document
./src/test/test_project_catalog
./src/test/test_project_session
./src/test/test_command_bus
./src/test/test_lxe_editor_session
./src/test/test_lxe_editor_api_service
./src/test/test_lxe_editor_api_server
```

Expected: each executable exits 0 and prints its pass message.

- [ ] **Step 4: Run ctest headless set**

Run:

```bash
ctest --output-on-failure -L auto -LE requires_video_device
```

Expected: headless tests pass.

- [ ] **Step 5: Commit cleanup**

```bash
git add src tests notes assets
git commit -m "Remove legacy scene source model"
```

## Self-Review Notes

- Spec coverage: template catalog, project catalog, project documents, startup `lastProject`, transient empty startup, project commands, project-scoped scene commands, save semantics, API/events, tests, docs, and no compatibility cleanup each have an implementation task.
- No-compat requirement: `scene load` is explicitly removed, old source-kind identifiers are deleted, and search verification blocks reintroducing aliases.
- Multi-scene requirement: `ProjectDocument.scenes`, `scene list/open/new/duplicate/remove`, and tests for a second scene are included.
- Risk: the editor session task is the largest. Execute earlier document/catalog/session tasks first so the integration step mostly rewires existing responsibilities rather than inventing behavior inside `editor_session.cpp`.
