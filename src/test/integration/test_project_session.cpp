#include "demos/lxe_editor/project_session.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace demo = LX_demo::lxe_editor;

namespace {
int failures = 0;
#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __LINE__ << " " << msg << "\n";                \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] std::filesystem::path makeTempRoot(const std::string &name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

[[nodiscard]] demo::ProjectSession
makeSession(const std::filesystem::path &root) {
  return demo::ProjectSession("assets/project_templates", root / "projects");
}

void testSaveFailsWithoutOpenProject() {
  const auto root = makeTempRoot("lx_project_session_save_none");
  demo::ProjectSession session = makeSession(root);

  EXPECT(!session.hasProject(), "new session should not have project");
  EXPECT(!session.currentProject().has_value(),
         "new session should not expose a project document");
  EXPECT(!session.projectRoot().has_value(),
         "new session should not expose project root");
  EXPECT(!session.activeScenePath().has_value(),
         "new session should not expose active scene");
  EXPECT(!session.dirty(), "new session should start clean");
  EXPECT(!session.saveProject().ok, "save should fail without project");
}

void testInitCopiesTemplateAndOpensDefaultScene() {
  const auto root = makeTempRoot("lx_project_session_init");
  demo::ProjectSession session = makeSession(root);

  const auto result = session.initProject("empty", "My Project");

  EXPECT(result.ok, "project init should succeed");
  EXPECT(session.hasProject(), "project init should open a project");
  EXPECT(session.currentProject().has_value(),
         "project init should expose project document");
  EXPECT(session.projectRoot().has_value(),
         "project init should expose project root");
  const auto projectRoot = *session.projectRoot();
  EXPECT(projectRoot.filename() == "my_project",
         "project init should allocate project path from name");
  EXPECT(std::filesystem::exists(projectRoot / "project.yaml"),
         "project init should create project.yaml");
  EXPECT(std::filesystem::exists(projectRoot / "scenes/main.scene.yaml"),
         "project init should copy default scene");
  EXPECT(std::filesystem::exists(projectRoot / "assets"),
         "project init should copy assets root");
  EXPECT(session.activeScenePath().has_value(),
         "project init should expose active scene path");
  EXPECT(session.activeScenePath()->filename() == "main.scene.yaml",
         "template default scene should open");
  EXPECT(!session.dirty(), "freshly initialized project should be clean");

  const auto &document = *session.currentProject();
  EXPECT(document.id == "my_project", "project id should use slugged name");
  EXPECT(document.displayName == "My Project",
         "project display name should use requested name");
  EXPECT(document.activeScene == std::filesystem::path("scenes/main.scene.yaml"),
         "project active scene should be template default");
  EXPECT(document.scenes.size() == 1, "template default scene should be listed");
  EXPECT(document.scenes[0].id == "main", "default scene id should be main");
  EXPECT(document.assetRoots.size() == 1, "template asset root should be listed");
  EXPECT(document.createdFromTemplate == std::optional<std::string>("empty"),
         "project should record source template");
}

void testInitEmptyProjectNameUsesTemplateIdForAllocation() {
  const auto root = makeTempRoot("lx_project_session_init_empty_name");
  demo::ProjectSession session = makeSession(root);

  const auto result =
      session.initProject("empty", std::optional<std::string>{""});

  EXPECT(result.ok, "project init with empty name should succeed");
  EXPECT(session.hasProject(), "project init should open a project");
  EXPECT(session.projectRoot().has_value(),
         "project init should expose project root");
  const auto projectRoot = *session.projectRoot();
  EXPECT(projectRoot.filename() == "empty",
         "empty project name should allocate from template id");
  EXPECT(std::filesystem::exists(root / "projects/empty/project.yaml"),
         "empty project name should create empty project directory");
  EXPECT(!std::filesystem::exists(root / "projects/project/project.yaml"),
         "empty project name should not allocate generic project directory");

  const auto &document = *session.currentProject();
  EXPECT(document.id == "empty",
         "empty project name should use template id for project id");
  EXPECT(document.displayName == "Empty",
         "empty project name should use template display name");
}

void testSceneNewAddsProjectSceneEntry() {
  const auto root = makeTempRoot("lx_project_session_new_scene");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Scene Test").ok,
         "project init should succeed");

  const auto result = session.newScene("lighting");

  EXPECT(result.ok, "scene new should succeed");
  EXPECT(session.currentProject().has_value(),
         "session should still expose project document");
  const auto &document = *session.currentProject();
  EXPECT(document.scenes.size() == 2, "scene new should add second scene");
  EXPECT(document.scenes[1].id == "lighting",
         "scene new should use requested scene id");
  EXPECT(document.scenes[1].path ==
             std::filesystem::path("scenes/lighting.scene.yaml"),
         "scene new should use scenes directory");
  EXPECT(document.activeScene == document.scenes[1].path,
         "scene new should make the new scene active");
  EXPECT(session.activeScenePath()->filename() == "lighting.scene.yaml",
         "scene new should expose new active scene path");
  EXPECT(std::filesystem::exists(*session.projectRoot() /
                                 "scenes/lighting.scene.yaml"),
         "scene new should write scene file");
  EXPECT(session.dirty(), "scene new should mark project dirty");
}

void testSceneNewRejectsPathTraversalSceneId() {
  const auto root = makeTempRoot("lx_project_session_new_scene_escape");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Traversal Test").ok,
         "project init should succeed");
  const auto outsideScene = *session.projectRoot() / "../outside.scene.yaml";

  const auto result = session.newScene("../../outside");

  EXPECT(!result.ok, "scene new should reject path traversal scene id");
  EXPECT(!std::filesystem::exists(outsideScene.lexically_normal()),
         "scene new should not write outside project root");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "rejected scene new should not add scene entry");
}

void testSceneDuplicateRejectsPathTraversalSceneId() {
  const auto root = makeTempRoot("lx_project_session_duplicate_scene_escape");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Duplicate Traversal Test").ok,
         "project init should succeed");
  const auto outsideScene = *session.projectRoot() / "../outside.scene.yaml";

  const auto result = session.duplicateScene("main", "../../outside");

  EXPECT(!result.ok, "scene duplicate should reject path traversal scene id");
  EXPECT(!std::filesystem::exists(outsideScene.lexically_normal()),
         "scene duplicate should not write outside project root");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "rejected scene duplicate should not add scene entry");
}

void testSceneOpenRejectsPathOutsideProjectRoot() {
  const auto root = makeTempRoot("lx_project_session_scene_escape");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Escape Test").ok,
         "project init should succeed");
  const auto originalScene = session.activeScenePath();

  const auto result = session.openScene("../outside.scene.yaml");

  EXPECT(!result.ok, "scene path should not escape project root");
  EXPECT(session.activeScenePath() == originalScene,
         "rejected scene open should keep active scene");
}

void testProjectCloseReturnsToNoProjectState() {
  const auto root = makeTempRoot("lx_project_session_close");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Close Test").ok,
         "project init should succeed");
  EXPECT(session.newScene("scratch").ok, "scene new should succeed");
  EXPECT(session.dirty(), "scene new should mark project dirty");

  const auto result = session.closeProject();

  EXPECT(result.ok, "project close should succeed");
  EXPECT(!session.hasProject(), "close should clear open project");
  EXPECT(!session.currentProject().has_value(),
         "close should clear project document");
  EXPECT(!session.projectRoot().has_value(), "close should clear project root");
  EXPECT(!session.activeScenePath().has_value(),
         "close should clear active scene path");
  EXPECT(!session.dirty(), "close should clear dirty state");
}
} // namespace

int main() {
  testSaveFailsWithoutOpenProject();
  testInitCopiesTemplateAndOpensDefaultScene();
  testInitEmptyProjectNameUsesTemplateIdForAllocation();
  testSceneNewAddsProjectSceneEntry();
  testSceneNewRejectsPathTraversalSceneId();
  testSceneDuplicateRejectsPathTraversalSceneId();
  testSceneOpenRejectsPathOutsideProjectRoot();
  testProjectCloseReturnsToNoProjectState();
  return failures == 0 ? 0 : 1;
}
