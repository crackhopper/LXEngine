#include "demos/lxe_editor/scene_catalog.hpp"

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

[[nodiscard]] std::filesystem::path makeTempRoot(const char* name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets/scenes");
  std::filesystem::create_directories(root / "data/scenes");
  return root;
}

void writeSceneFile(const std::filesystem::path& path) {
  std::ofstream out(path);
  out << "scene:\n  name: test\nnodes: []\n";
}

void testCatalogListsAssetAndLocalScenes() {
  const auto root = makeTempRoot("lx_scene_catalog");
  writeSceneFile(root / "assets/scenes/sample.scene.yaml");
  writeSceneFile(root / "data/scenes/work.scene.yaml");

  demo::SceneCatalog catalog(demo::SceneCatalogRoots{
      .assetRoots = {root / "assets/scenes"},
      .localRoots = {root / "data/scenes"},
  });
  catalog.refresh();

  EXPECT(catalog.entries().size() == 2, "catalog should include both roots");
  EXPECT(catalog.entries()[0].kind == demo::SceneSourceKind::Asset,
         "asset scene should be marked asset");
  EXPECT(catalog.entries()[1].kind == demo::SceneSourceKind::Local,
         "local scene should be marked local");
  EXPECT(catalog.findById("sample.scene.yaml").has_value(),
         "asset id should resolve");
  EXPECT(catalog.findById("work.scene.yaml").has_value(),
         "local id should resolve");
}

void testCatalogClassifiesExplicitPaths() {
  const auto root = makeTempRoot("lx_scene_catalog_classify");
  const auto assetPath = root / "assets/scenes/sample.scene.yaml";
  const auto localPath = root / "data/scenes/work.scene.yaml";
  writeSceneFile(assetPath);
  writeSceneFile(localPath);

  demo::SceneCatalog catalog(demo::SceneCatalogRoots{
      .assetRoots = {root / "assets/scenes"},
      .localRoots = {root / "data/scenes"},
  });

  const auto assetEntry = catalog.classifyPath(assetPath);
  const auto localEntry = catalog.classifyPath(localPath);
  EXPECT(assetEntry.has_value(), "asset path should classify");
  EXPECT(localEntry.has_value(), "local path should classify");
  EXPECT(assetEntry->kind == demo::SceneSourceKind::Asset,
         "asset path should classify as asset");
  EXPECT(localEntry->kind == demo::SceneSourceKind::Local,
         "local path should classify as local");
}

void testCatalogResolvesIdOrPath() {
  const auto root = makeTempRoot("lx_scene_catalog_resolve");
  const auto assetPath = root / "assets/scenes/sample.scene.yaml";
  writeSceneFile(assetPath);

  demo::SceneCatalog catalog(demo::SceneCatalogRoots{
      .assetRoots = {root / "assets/scenes"},
      .localRoots = {root / "data/scenes"},
  });
  catalog.refresh();

  EXPECT(catalog.resolveNameOrPath("sample.scene.yaml") ==
             std::filesystem::absolute(assetPath).lexically_normal(),
         "catalog id should resolve to file path");
  EXPECT(catalog.resolveNameOrPath(assetPath.string()) ==
             std::filesystem::absolute(assetPath).lexically_normal(),
         "explicit path should normalize");
}

} // namespace

int main() {
  testCatalogListsAssetAndLocalScenes();
  testCatalogClassifiesExplicitPaths();
  testCatalogResolvesIdOrPath();

  if (failures != 0) {
    std::cerr << "test_scene_catalog failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_catalog passed\n";
  return 0;
}
