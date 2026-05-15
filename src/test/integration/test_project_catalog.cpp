#include "demos/lxe_editor/project_catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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

void writeFile(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

[[nodiscard]] bool
sceneResolutionThrows(const std::filesystem::path &projectRoot,
                      const demo::ProjectDocument &document,
                      const std::string &sceneIdOrPath) {
  try {
    (void)demo::resolveProjectScenePath(projectRoot, document, sceneIdOrPath);
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

[[nodiscard]] bool resolveProjectThrows(const demo::ProjectCatalog &projects,
                                        const std::string &token) {
  try {
    (void)projects.resolveIdOrPath(token);
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

void testCatalogListsTemplatesAndProjects() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog";
  std::filesystem::remove_all(root);
  writeFile(root / "assets/project_templates/empty/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: empty\n"
            "displayName: Empty\n"
            "defaultScene: scenes/main.scene.yaml\n"
            "copy:\n"
            "  - scenes/\n");
  writeFile(root / "data/projects/demo/project.yaml",
            "schema: lxe.project.v1\n"
            "id: demo\n"
            "displayName: Demo\n"
            "activeScene: scenes/main.scene.yaml\n"
            "scenes:\n"
            "  - id: main\n"
            "    path: scenes/main.scene.yaml\n");

  demo::ProjectTemplateCatalog templates(root / "assets/project_templates");
  demo::ProjectCatalog projects(root / "data/projects");
  templates.refresh();
  projects.refresh();

  EXPECT(templates.entries().size() == 1, "one template should be listed");
  EXPECT(templates.findById("empty").has_value(), "template id should resolve");
  EXPECT(templates.entries()[0].path ==
             std::filesystem::absolute(root / "assets/project_templates/empty")
                 .lexically_normal(),
         "template path should be absolute and normalized");
  EXPECT(projects.entries().size() == 1, "one project should be listed");
  EXPECT(projects.findById("demo").has_value(), "project id should resolve");
  EXPECT(projects.entries()[0].path ==
             std::filesystem::absolute(root / "data/projects/demo")
                 .lexically_normal(),
         "project path should be absolute and normalized");
}

void testMalformedTemplateIsSkipped() {
  const auto root = std::filesystem::temp_directory_path() /
                    "lx_project_catalog_bad_template";
  std::filesystem::remove_all(root);
  writeFile(root / "assets/project_templates/empty/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: empty\n"
            "displayName: Empty\n"
            "defaultScene: scenes/main.scene.yaml\n");
  writeFile(root / "assets/project_templates/bad/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "displayName: Missing Id\n"
            "defaultScene: scenes/main.scene.yaml\n");

  demo::ProjectTemplateCatalog templates(root / "assets/project_templates");
  templates.refresh();

  EXPECT(templates.entries().size() == 1,
         "malformed template should be skipped");
  EXPECT(templates.findById("empty").has_value(),
         "valid template should still be listed");
}

void testMalformedProjectIsSkipped() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_bad_project";
  std::filesystem::remove_all(root);
  writeFile(root / "data/projects/demo/project.yaml",
            "schema: lxe.project.v1\n"
            "id: demo\n"
            "displayName: Demo\n"
            "activeScene: scenes/main.scene.yaml\n"
            "scenes:\n"
            "  - id: main\n"
            "    path: scenes/main.scene.yaml\n");
  writeFile(root / "data/projects/bad/project.yaml",
            "schema: lxe.project.v1\n"
            "displayName: Missing Id\n"
            "activeScene: scenes/main.scene.yaml\n");

  demo::ProjectCatalog projects(root / "data/projects");
  projects.refresh();

  EXPECT(projects.entries().size() == 1, "malformed project should be skipped");
  EXPECT(projects.findById("demo").has_value(),
         "valid project should still be listed");
}

void testDuplicateTemplateIdsKeepFirstPath() {
  const auto root = std::filesystem::temp_directory_path() /
                    "lx_project_catalog_dup_template";
  std::filesystem::remove_all(root);
  writeFile(root / "assets/project_templates/a/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: duplicate\n"
            "displayName: First\n"
            "defaultScene: scenes/main.scene.yaml\n");
  writeFile(root / "assets/project_templates/z/project_template.yaml",
            "schema: lxe.project_template.v1\n"
            "id: duplicate\n"
            "displayName: Later\n"
            "defaultScene: scenes/main.scene.yaml\n");

  demo::ProjectTemplateCatalog templates(root / "assets/project_templates");
  templates.refresh();

  EXPECT(templates.entries().size() == 1,
         "duplicate template id should be skipped");
  EXPECT(templates.findById("duplicate").has_value(),
         "kept template id should resolve");
  EXPECT(templates.entries()[0].displayName == "First",
         "first duplicate template path should be kept");
  EXPECT(templates.entries()[0].path ==
             std::filesystem::absolute(root / "assets/project_templates/a")
                 .lexically_normal(),
         "kept duplicate template should have deterministic path");
}

void testDuplicateProjectIdsKeepFirstPath() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_dup_project";
  std::filesystem::remove_all(root);
  writeFile(root / "data/projects/a/project.yaml",
            "schema: lxe.project.v1\n"
            "id: duplicate\n"
            "displayName: First\n"
            "activeScene: scenes/main.scene.yaml\n"
            "scenes:\n"
            "  - id: main\n"
            "    path: scenes/main.scene.yaml\n");
  writeFile(root / "data/projects/z/project.yaml",
            "schema: lxe.project.v1\n"
            "id: duplicate\n"
            "displayName: Later\n"
            "activeScene: scenes/main.scene.yaml\n"
            "scenes:\n"
            "  - id: main\n"
            "    path: scenes/main.scene.yaml\n");

  demo::ProjectCatalog projects(root / "data/projects");
  projects.refresh();

  EXPECT(projects.entries().size() == 1,
         "duplicate project id should be skipped");
  EXPECT(projects.findById("duplicate").has_value(),
         "kept project id should resolve");
  EXPECT(projects.entries()[0].displayName == "First",
         "first duplicate project path should be kept");
  EXPECT(projects.entries()[0].path ==
             std::filesystem::absolute(root / "data/projects/a")
                 .lexically_normal(),
         "kept duplicate project should have deterministic path");
}

void testSceneResolutionStaysInsideProject() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_scene";
  std::filesystem::remove_all(root);
  writeFile(root / "data/projects/demo/project.yaml",
            "schema: lxe.project.v1\n"
            "id: demo\n"
            "displayName: Demo\n"
            "activeScene: scenes/main.scene.yaml\n"
            "scenes:\n"
            "  - id: main\n"
            "    path: scenes/main.scene.yaml\n");
  demo::ProjectDocument document =
      demo::loadProjectDocument(root / "data/projects/demo/project.yaml");

  const auto resolved = demo::resolveProjectScenePath(
      root / "data/projects/demo", document, "main");
  EXPECT(resolved == std::filesystem::absolute(root / "data/projects/demo/"
                                                      "scenes/main.scene.yaml")
                         .lexically_normal(),
         "scene id should resolve under project root");
  EXPECT(sceneResolutionThrows(root / "data/projects/demo", document,
                               "../outside.scene.yaml"),
         "project-relative scene path should not escape project root");
  EXPECT(sceneResolutionThrows(root / "data/projects/demo", document,
                               (root / "outside.scene.yaml").string()),
         "absolute scene path should not escape project root");
  EXPECT(sceneResolutionThrows(root / "data/projects/demo", document,
                               (root / "data/projects/demo-sibling/"
                                       "main.scene.yaml")
                                   .string()),
         "sibling-prefix scene path should not escape project root");
}

void testSceneResolutionRejectsSymlinkEscape() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_symlink";
  std::filesystem::remove_all(root);
  const auto projectRoot = root / "data/projects/demo";
  const auto outsideRoot = root / "outside";
  std::filesystem::create_directories(projectRoot / "scenes");
  std::filesystem::create_directories(outsideRoot);

  std::error_code ec;
  std::filesystem::create_directory_symlink(outsideRoot,
                                            projectRoot / "scenes/linked", ec);
  if (ec) {
    std::cerr << "[SKIP] symlink escape assertion: " << ec.message() << "\n";
    return;
  }

  demo::ProjectDocument document;
  document.id = "demo";
  document.displayName = "Demo";
  document.activeScene = "scenes/linked/main.scene.yaml";
  document.scenes.push_back({"main", "scenes/linked/main.scene.yaml"});

  EXPECT(sceneResolutionThrows(projectRoot, document, "main"),
         "scene path through symlink should not escape project root");
}

void testSlugAndProjectPathAllocation() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_slug";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "my_project");

  EXPECT(demo::makeProjectSlug("My Project!") == "my_project_",
         "slug should lowercase and replace unsafe characters");
  EXPECT(demo::makeProjectSlug("   ") == "project",
         "empty slug should default to project");
  EXPECT(
      demo::allocateProjectPath(root, "My Project") ==
          std::filesystem::absolute(root / "my_project-2").lexically_normal(),
      "existing project directory should get numeric suffix");
}

void testProjectResolveIdOrPath() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_catalog_resolve";
  std::filesystem::remove_all(root);
  writeFile(root / "demo/project.yaml", "schema: lxe.project.v1\n"
                                        "id: demo\n"
                                        "displayName: Demo\n"
                                        "activeScene: scenes/main.scene.yaml\n"
                                        "scenes:\n"
                                        "  - id: main\n"
                                        "    path: scenes/main.scene.yaml\n");

  demo::ProjectCatalog projects(root);
  projects.refresh();

  EXPECT(projects.resolveIdOrPath("demo") ==
             std::filesystem::absolute(root / "demo").lexically_normal(),
         "project id should resolve to catalog path");
  EXPECT(projects.resolveIdOrPath((root / "demo").string()) ==
             std::filesystem::absolute(root / "demo").lexically_normal(),
         "path token should resolve as path");
  EXPECT(resolveProjectThrows(projects, "missing"),
         "unknown bare project id should throw");
}
} // namespace

int main() {
  testCatalogListsTemplatesAndProjects();
  testMalformedTemplateIsSkipped();
  testMalformedProjectIsSkipped();
  testDuplicateTemplateIdsKeepFirstPath();
  testDuplicateProjectIdsKeepFirstPath();
  testSceneResolutionStaysInsideProject();
  testSceneResolutionRejectsSymlinkEscape();
  testSlugAndProjectPathAllocation();
  testProjectResolveIdOrPath();
  return failures == 0 ? 0 : 1;
}
