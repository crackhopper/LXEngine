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
  EXPECT(projects.entries().size() == 1, "one project should be listed");
  EXPECT(projects.findById("demo").has_value(), "project id should resolve");
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
}
} // namespace

int main() {
  testCatalogListsTemplatesAndProjects();
  testSceneResolutionStaysInsideProject();
  testSlugAndProjectPathAllocation();
  testProjectResolveIdOrPath();
  return failures == 0 ? 0 : 1;
}
