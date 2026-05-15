#include "demos/lxe_editor/project_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

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

void testTemplateDocumentLoadsCopyRoots() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_doc_template";
  std::filesystem::remove_all(root);
  const auto path = root / "project_template.yaml";
  writeFile(path, "schema: lxe.project_template.v1\n"
                  "id: basic-3d\n"
                  "displayName: Basic 3D\n"
                  "defaultScene: scenes/main.scene.yaml\n"
                  "copy:\n"
                  "  - scenes/\n"
                  "  - assets/\n");

  const auto document = demo::loadProjectTemplateDocument(path);

  EXPECT(document.id == "basic-3d", "template id should load");
  EXPECT(document.defaultScene ==
             std::filesystem::path("scenes/main.scene.yaml"),
         "default scene should load as a relative path");
  EXPECT(document.copyRoots.size() == 2, "copy roots should load");
}

void testProjectDocumentRoundTripsScenes() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_doc_project";
  std::filesystem::remove_all(root);
  const auto path = root / "project.yaml";
  demo::ProjectDocument document;
  document.id = "my_project";
  document.displayName = "My Project";
  document.activeScene = "scenes/main.scene.yaml";
  document.scenes.push_back({"main", "scenes/main.scene.yaml"});
  document.scenes.push_back(
      {"lighting_test", "scenes/lighting_test.scene.yaml"});
  document.assetRoots.push_back("assets/");
  document.createdFromTemplate = "basic-3d";

  EXPECT(demo::saveProjectDocument(path, document),
         "project document should save");
  const auto loaded = demo::loadProjectDocument(path);

  EXPECT(loaded.id == "my_project", "project id should round trip");
  EXPECT(loaded.scenes.size() == 2, "scene entries should round trip");
  EXPECT(loaded.scenes[1].id == "lighting_test",
         "second scene id should round trip");
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
