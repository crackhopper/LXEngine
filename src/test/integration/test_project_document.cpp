#include "demos/lxe_editor/project_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

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

[[nodiscard]] bool loadTemplateThrows(const std::filesystem::path &path) {
  try {
    (void)demo::loadProjectTemplateDocument(path);
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

[[nodiscard]] bool loadProjectThrows(const std::filesystem::path &path) {
  try {
    (void)demo::loadProjectDocument(path);
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

[[nodiscard]] demo::ProjectDocument makeValidProjectDocument() {
  demo::ProjectDocument document;
  document.id = "my_project";
  document.displayName = "My Project";
  document.activeScene = "scenes/main.scene.yaml";
  document.scenes.push_back({"main", "scenes/main.scene.yaml"});
  document.scenes.push_back(
      {"lighting_test", "scenes/lighting_test.scene.yaml"});
  document.assetRoots.push_back("assets/");
  document.createdFromTemplate = "basic-3d";
  return document;
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
  const demo::ProjectDocument document = makeValidProjectDocument();

  EXPECT(demo::saveProjectDocument(path, document),
         "project document should save");
  const auto loaded = demo::loadProjectDocument(path);

  EXPECT(loaded.id == "my_project", "project id should round trip");
  EXPECT(loaded.displayName == "My Project",
         "project display name should round trip");
  EXPECT(loaded.activeScene == std::filesystem::path("scenes/main.scene.yaml"),
         "active scene should round trip");
  EXPECT(loaded.scenes.size() == 2, "scene entries should round trip");
  EXPECT(loaded.scenes[0].path ==
             std::filesystem::path("scenes/main.scene.yaml"),
         "first scene path should round trip");
  EXPECT(loaded.scenes[1].id == "lighting_test",
         "second scene id should round trip");
  EXPECT(loaded.scenes[1].path ==
             std::filesystem::path("scenes/lighting_test.scene.yaml"),
         "second scene path should round trip");
  EXPECT(loaded.assetRoots.size() == 1, "asset roots should round trip");
  EXPECT(loaded.assetRoots[0] == std::filesystem::path("assets/"),
         "asset root path should round trip");
  EXPECT(loaded.createdFromTemplate.has_value(),
         "createdFromTemplate should round trip");
  EXPECT(*loaded.createdFromTemplate == "basic-3d",
         "createdFromTemplate value should round trip");
}

void testTemplateDocumentRejectsInvalidSchemaAndMissingFields() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_doc_template_bad";
  std::filesystem::remove_all(root);

  const auto wrongSchemaPath = root / "wrong_schema.yaml";
  writeFile(wrongSchemaPath, "schema: lxe.project.v1\n"
                             "id: basic-3d\n"
                             "displayName: Basic 3D\n"
                             "defaultScene: scenes/main.scene.yaml\n");
  EXPECT(loadTemplateThrows(wrongSchemaPath),
         "template load should reject wrong schema");

  const auto missingIdPath = root / "missing_id.yaml";
  writeFile(missingIdPath, "schema: lxe.project_template.v1\n"
                           "displayName: Basic 3D\n"
                           "defaultScene: scenes/main.scene.yaml\n");
  EXPECT(loadTemplateThrows(missingIdPath),
         "template load should reject missing id");

  const auto missingDefaultScenePath = root / "missing_default_scene.yaml";
  writeFile(missingDefaultScenePath, "schema: lxe.project_template.v1\n"
                                     "id: basic-3d\n"
                                     "displayName: Basic 3D\n");
  EXPECT(loadTemplateThrows(missingDefaultScenePath),
         "template load should reject missing defaultScene");
}

void testProjectDocumentRejectsInvalidSchemaAndMissingFields() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_doc_project_bad";
  std::filesystem::remove_all(root);

  const auto wrongSchemaPath = root / "wrong_schema.yaml";
  writeFile(wrongSchemaPath, "schema: lxe.project_template.v1\n"
                             "id: my_project\n"
                             "displayName: My Project\n"
                             "activeScene: scenes/main.scene.yaml\n");
  EXPECT(loadProjectThrows(wrongSchemaPath),
         "project load should reject wrong schema");

  const auto missingIdPath = root / "missing_id.yaml";
  writeFile(missingIdPath, "schema: lxe.project.v1\n"
                           "displayName: My Project\n"
                           "activeScene: scenes/main.scene.yaml\n");
  EXPECT(loadProjectThrows(missingIdPath),
         "project load should reject missing id");

  const auto missingActiveScenePath = root / "missing_active_scene.yaml";
  writeFile(missingActiveScenePath, "schema: lxe.project.v1\n"
                                    "id: my_project\n"
                                    "displayName: My Project\n");
  EXPECT(loadProjectThrows(missingActiveScenePath),
         "project load should reject missing activeScene");
}

void testSaveProjectDocumentRejectsMissingRequiredFields() {
  const auto root =
      std::filesystem::temp_directory_path() / "lx_project_doc_invalid_save";
  std::filesystem::remove_all(root);

  auto missingId = makeValidProjectDocument();
  missingId.id.clear();
  const auto missingIdPath = root / "missing_id.yaml";
  EXPECT(!demo::saveProjectDocument(missingIdPath, missingId),
         "project save should reject empty id");
  EXPECT(!std::filesystem::exists(missingIdPath),
         "empty id should not write a document");

  auto missingDisplayName = makeValidProjectDocument();
  missingDisplayName.displayName.clear();
  const auto missingDisplayNamePath = root / "missing_display_name.yaml";
  EXPECT(!demo::saveProjectDocument(missingDisplayNamePath, missingDisplayName),
         "project save should reject empty displayName");
  EXPECT(!std::filesystem::exists(missingDisplayNamePath),
         "empty displayName should not write a document");

  auto missingActiveScene = makeValidProjectDocument();
  missingActiveScene.activeScene.clear();
  const auto missingActiveScenePath = root / "missing_active_scene.yaml";
  EXPECT(!demo::saveProjectDocument(missingActiveScenePath, missingActiveScene),
         "project save should reject empty activeScene");
  EXPECT(!std::filesystem::exists(missingActiveScenePath),
         "empty activeScene should not write a document");
}
} // namespace

int main() {
  testTemplateDocumentLoadsCopyRoots();
  testProjectDocumentRoundTripsScenes();
  testTemplateDocumentRejectsInvalidSchemaAndMissingFields();
  testProjectDocumentRejectsInvalidSchemaAndMissingFields();
  testSaveProjectDocumentRejectsMissingRequiredFields();
  if (failures != 0) {
    return 1;
  }
  std::cout << "test_project_document passed\n";
  return 0;
}
