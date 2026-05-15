#include "demos/lxe_editor/project_session.hpp"

#include <filesystem>
#include <fstream>
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

[[nodiscard]] demo::ProjectSession
makeSession(const std::filesystem::path &templateRoot,
            const std::filesystem::path &projectsRoot) {
  return demo::ProjectSession(templateRoot, projectsRoot);
}

void writeFile(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
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
  EXPECT(result.structuredJson.find("\"ok\":true") != std::string::npos,
         "project init success result should include ok JSON field");
  EXPECT(result.structuredJson.find("\"projectId\":\"my_project\"") !=
             std::string::npos,
         "project init success result should include project id JSON field");
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

void testInitRejectsTemplateCopyRootTraversal() {
  const auto root = makeTempRoot("lx_project_session_template_escape");
  const auto templateRoot = root / "templates";
  writeFile(templateRoot / "bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: bad\n"
            "displayName: Bad\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - ../outside/\n");
  writeFile(templateRoot / "outside/secret.txt", "secret\n");
  demo::ProjectSession session = makeSession(templateRoot, root / "projects");

  const auto result = session.initProject("bad", std::nullopt);

  EXPECT(!result.ok, "project init should reject traversal copy root");
  EXPECT(!session.hasProject(),
         "rejected template copy root should not open a project");
  EXPECT(!std::filesystem::exists(root / "projects/outside/secret.txt"),
         "traversal copy root should not write outside project directory");
}

void testInitRejectsTemplateAbsoluteCopyRoot() {
  const auto root = makeTempRoot("lx_project_session_template_absolute");
  const auto templateRoot = root / "templates";
  const auto absoluteRoot = root / "absolute_source";
  writeFile(absoluteRoot / "secret.txt", "secret\n");
  writeFile(templateRoot / "bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: bad\n"
            "displayName: Bad\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - " +
                absoluteRoot.generic_string() + "/\n");
  demo::ProjectSession session = makeSession(templateRoot, root / "projects");

  const auto result = session.initProject("bad", std::nullopt);

  EXPECT(!result.ok, "project init should reject absolute copy root");
  EXPECT(!session.hasProject(),
         "rejected absolute copy root should not open a project");
  EXPECT(!std::filesystem::exists(root / "projects/bad/secret.txt"),
         "absolute copy root should not be copied into project");
}

void testInitRejectsTemplateSymlinkCopyRootEscape() {
  const auto root = makeTempRoot("lx_project_session_template_symlink");
  const auto templateRoot = root / "templates";
  writeFile(templateRoot / "bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: bad\n"
            "displayName: Bad\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - linked/\n");
  writeFile(root / "outside/secret.txt", "secret\n");

  std::error_code ec;
  std::filesystem::create_directory_symlink(root / "outside",
                                            templateRoot / "bad/linked", ec);
  if (ec) {
    std::cerr << "[SKIP] symlink copy root assertion: " << ec.message()
              << "\n";
    return;
  }

  demo::ProjectSession session = makeSession(templateRoot, root / "projects");
  const auto result = session.initProject("bad", std::nullopt);

  EXPECT(!result.ok, "project init should reject symlink copy root escape");
  EXPECT(!session.hasProject(),
         "rejected symlink copy root should not open a project");
  EXPECT(!std::filesystem::exists(root / "projects/bad/linked/secret.txt"),
         "symlink copy root should not copy escaped content");
}

void testInitRejectsNestedTemplateCopyRootSymlink() {
  const auto root = makeTempRoot("lx_project_session_template_nested_symlink");
  const auto templateRoot = root / "templates";
  writeFile(templateRoot / "bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: bad\n"
            "displayName: Bad\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - assets/\n");
  writeFile(templateRoot / "bad/assets/local.txt", "local\n");
  writeFile(root / "outside/secret.txt", "secret\n");

  std::error_code ec;
  std::filesystem::create_directory_symlink(root / "outside",
                                            templateRoot / "bad/assets/linked",
                                            ec);
  if (ec) {
    std::cerr << "[SKIP] nested symlink assertion: " << ec.message() << "\n";
    return;
  }

  demo::ProjectSession session = makeSession(templateRoot, root / "projects");
  const auto result = session.initProject("bad", std::nullopt);

  EXPECT(!result.ok, "project init should reject nested copy root symlink");
  EXPECT(!session.hasProject(),
         "rejected nested copy root symlink should not open a project");
  EXPECT(!std::filesystem::exists(root /
                                  "projects/bad/assets/linked/secret.txt"),
         "nested copy root symlink should not copy outside content");
}

void testInitRejectsMissingTemplateCopyRoot() {
  const auto root = makeTempRoot("lx_project_session_template_missing_root");
  const auto templateRoot = root / "templates";
  writeFile(templateRoot / "bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: bad\n"
            "displayName: Bad\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - missing/\n");
  demo::ProjectSession session = makeSession(templateRoot, root / "projects");

  const auto result = session.initProject("bad", std::nullopt);

  EXPECT(!result.ok, "project init should reject missing copy root");
  EXPECT(!session.hasProject(),
         "rejected missing copy root should not open a project");
}

void testOpenProjectLoadsSavedProject() {
  const auto root = makeTempRoot("lx_project_session_open_project");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Open Test").ok,
         "project init should succeed");

  demo::ProjectSession byId = makeSession(root);
  const auto idResult = byId.openProject("open_test");

  EXPECT(idResult.ok, "openProject should open saved project by id");
  EXPECT(byId.hasProject(), "openProject by id should set project state");
  EXPECT(byId.currentProject()->id == "open_test",
         "openProject by id should load project document");

  demo::ProjectSession byPath = makeSession(root);
  const auto pathResult = byPath.openProject((root / "projects/open_test")
                                                 .generic_string());

  EXPECT(pathResult.ok, "openProject should open saved project by path");
  EXPECT(byPath.hasProject(), "openProject by path should set project state");
  EXPECT(byPath.currentProject()->displayName == "Open Test",
         "openProject by path should load project display name");
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

void testSaveProjectPersistsNewSceneAndActiveScene() {
  const auto root = makeTempRoot("lx_project_session_save_persistence");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Save Test").ok,
         "project init should succeed");
  EXPECT(session.newScene("lighting").ok, "scene new should succeed");
  EXPECT(session.saveProject().ok, "saveProject should persist metadata");
  EXPECT(!session.dirty(), "saveProject should clear dirty state");

  demo::ProjectSession reopened = makeSession(root);
  EXPECT(reopened.openProject("save_test").ok,
         "saved project should reopen by id");
  EXPECT(reopened.currentProject()->scenes.size() == 2,
         "saved project should round trip scene list");
  EXPECT(reopened.currentProject()->activeScene ==
             std::filesystem::path("scenes/lighting.scene.yaml"),
         "saved project should round trip active scene");
  EXPECT(reopened.activeScenePath()->filename() == "lighting.scene.yaml",
         "saved active scene should resolve after reopen");
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

void testSceneNewRejectsSymlinkedScenesDirectoryEscape() {
  const auto root = makeTempRoot("lx_project_session_new_scene_symlink");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "New Symlink Test").ok,
         "project init should succeed");
  const auto projectRoot = *session.projectRoot();
  const auto outsideRoot = root / "outside_scenes";
  std::filesystem::remove_all(projectRoot / "scenes");
  std::filesystem::create_directories(outsideRoot);

  std::error_code ec;
  std::filesystem::create_directory_symlink(outsideRoot, projectRoot / "scenes",
                                            ec);
  if (ec) {
    std::cerr << "[SKIP] new scene symlink assertion: " << ec.message()
              << "\n";
    return;
  }

  const auto result = session.newScene("safe_id");

  EXPECT(!result.ok, "scene new should reject symlinked scenes directory");
  EXPECT(!std::filesystem::exists(outsideRoot / "safe_id.scene.yaml"),
         "scene new should not write through symlinked scenes directory");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "rejected symlinked scene new should not add scene entry");
}

void testSceneDuplicateRejectsSymlinkedScenesDirectoryEscape() {
  const auto root = makeTempRoot("lx_project_session_duplicate_scene_symlink");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Duplicate Symlink Test").ok,
         "project init should succeed");
  const auto projectRoot = *session.projectRoot();
  const auto outsideRoot = root / "outside_scenes";
  std::filesystem::remove_all(projectRoot / "scenes");
  std::filesystem::create_directories(outsideRoot);
  writeFile(outsideRoot / "main.scene.yaml",
            "scene:\n  name: Outside Main\nnodes: []\n");

  std::error_code ec;
  std::filesystem::create_directory_symlink(outsideRoot, projectRoot / "scenes",
                                            ec);
  if (ec) {
    std::cerr << "[SKIP] duplicate scene symlink assertion: " << ec.message()
              << "\n";
    return;
  }

  const auto result = session.duplicateScene("main", "copy_id");

  EXPECT(!result.ok,
         "scene duplicate should reject symlinked scenes directory");
  EXPECT(!std::filesystem::exists(outsideRoot / "copy_id.scene.yaml"),
         "scene duplicate should not write through symlinked scenes directory");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "rejected symlinked scene duplicate should not add scene entry");
}

void testSceneIdsUsePortableWhitelist() {
  const auto root = makeTempRoot("lx_project_session_scene_id_whitelist");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Whitelist Test").ok,
         "project init should succeed");

  EXPECT(!session.newScene("bad:name").ok,
         "scene id with colon should be rejected");
  EXPECT(!session.newScene("bad name").ok,
         "scene id with space should be rejected");
  EXPECT(!session.newScene("..").ok, "dot-dot scene id should be rejected");
  EXPECT(!session.newScene("CON").ok,
         "Windows reserved scene id should be rejected");
  EXPECT(!session.duplicateScene("main", "bad:name").ok,
         "duplicate scene id with colon should be rejected");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "rejected scene ids should not add scene entries");
}

void testDuplicateSceneSuccessAndRejectsDuplicateIdAndPath() {
  const auto root = makeTempRoot("lx_project_session_duplicate_scene");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Duplicate Test").ok,
         "project init should succeed");

  const auto result = session.duplicateScene("main", "copy");

  EXPECT(result.ok, "duplicateScene should copy existing scene");
  EXPECT(session.currentProject()->scenes.size() == 2,
         "duplicateScene should add scene entry");
  EXPECT(session.currentProject()->activeScene ==
             std::filesystem::path("scenes/copy.scene.yaml"),
         "duplicateScene should activate duplicate");
  EXPECT(std::filesystem::exists(*session.projectRoot() /
                                 "scenes/copy.scene.yaml"),
         "duplicateScene should write duplicate file");
  EXPECT(!session.duplicateScene("main", "copy").ok,
         "duplicateScene should reject duplicate scene id");

  writeFile(*session.projectRoot() / "scenes/collision.scene.yaml",
            "scene:\n  name: Collision\nnodes: []\n");
  EXPECT(!session.duplicateScene("main", "collision").ok,
         "duplicateScene should reject duplicate target path");
}

void testSceneOpenRejectsPathOutsideProjectRoot() {
  const auto root = makeTempRoot("lx_project_session_scene_escape");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Escape Test").ok,
         "project init should succeed");
  const auto originalScene = session.activeScenePath();

  EXPECT(session.openScene("scenes/main.scene.yaml").ok,
         "registered scene path should open");

  const auto result = session.openScene("../outside.scene.yaml");

  EXPECT(!result.ok, "scene path should not escape project root");
  EXPECT(session.activeScenePath() == originalScene,
         "rejected scene open should keep active scene");
}

void testSceneOpenRejectsUnregisteredContainedPath() {
  const auto root = makeTempRoot("lx_project_session_scene_unregistered");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Ghost Test").ok,
         "project init should succeed");
  writeFile(*session.projectRoot() / "scenes/ghost.scene.yaml",
            "scene:\n  name: Ghost\nnodes: []\n");
  const auto originalScene = session.activeScenePath();

  const auto result = session.openScene("scenes/ghost.scene.yaml");

  EXPECT(!result.ok, "openScene should reject unregistered scene path");
  EXPECT(session.activeScenePath() == originalScene,
         "rejected unregistered scene should keep active scene");
}

void testRemoveSceneSuccessAndRejectsActiveOrLastScene() {
  const auto root = makeTempRoot("lx_project_session_remove_scene");
  demo::ProjectSession session = makeSession(root);
  EXPECT(session.initProject("empty", "Remove Test").ok,
         "project init should succeed");
  EXPECT(session.newScene("scratch").ok, "scene new should succeed");
  EXPECT(!session.removeScene("scratch").ok,
         "removeScene should reject active scene");
  EXPECT(session.openScene("main").ok, "openScene should switch by scene id");

  const auto removeResult = session.removeScene("scratch");

  EXPECT(removeResult.ok, "removeScene should remove inactive non-last scene");
  EXPECT(session.currentProject()->scenes.size() == 1,
         "removeScene should remove scene entry");
  EXPECT(!std::filesystem::exists(*session.projectRoot() /
                                  "scenes/scratch.scene.yaml"),
         "removeScene should delete scene file");
  EXPECT(!session.removeScene("main").ok,
         "removeScene should reject removing last scene");
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
  testInitRejectsTemplateCopyRootTraversal();
  testInitRejectsTemplateAbsoluteCopyRoot();
  testInitRejectsTemplateSymlinkCopyRootEscape();
  testInitRejectsNestedTemplateCopyRootSymlink();
  testInitRejectsMissingTemplateCopyRoot();
  testOpenProjectLoadsSavedProject();
  testSceneNewAddsProjectSceneEntry();
  testSaveProjectPersistsNewSceneAndActiveScene();
  testSceneNewRejectsPathTraversalSceneId();
  testSceneDuplicateRejectsPathTraversalSceneId();
  testSceneNewRejectsSymlinkedScenesDirectoryEscape();
  testSceneDuplicateRejectsSymlinkedScenesDirectoryEscape();
  testSceneIdsUsePortableWhitelist();
  testDuplicateSceneSuccessAndRejectsDuplicateIdAndPath();
  testSceneOpenRejectsPathOutsideProjectRoot();
  testSceneOpenRejectsUnregisteredContainedPath();
  testRemoveSceneSuccessAndRejectsActiveOrLastScene();
  testProjectCloseReturnsToNoProjectState();
  return failures == 0 ? 0 : 1;
}
