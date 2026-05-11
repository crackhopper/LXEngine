#include "demos/lxe_editor/scene_session.hpp"

#include <filesystem>
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

demo::SceneSession makeSession() {
  return demo::SceneSession(std::filesystem::temp_directory_path() /
                                "lx_scene_session_data/scenes",
                            [] { return std::string("2026-05-11-153000"); });
}

void testSessionDefaultsToUser() {
  auto session = makeSession();
  EXPECT(session.permission() == demo::ScenePermissionLevel::User,
         "default permission should be user");
}

void testUserSavingAssetRedirectsToTimestampedLocalCopy() {
  auto session = makeSession();
  session.setCurrentDocument("assets/scenes/sample.scene.yaml",
                             demo::SceneSourceKind::Asset);

  const auto decision = session.decideSaveTarget(std::nullopt, "sample");
  EXPECT(decision.kind == demo::SceneSourceKind::Local,
         "user asset save should redirect to local");
  EXPECT(decision.redirectedFromAsset,
         "user asset save should report redirection");
  EXPECT(decision.path.filename() == "sample.2026-05-11-153000.scene.yaml",
         "user asset save should use timestamped local file");
}

void testAdminSavingAssetStaysOnAssetPath() {
  auto session = makeSession();
  session.setPermission(demo::ScenePermissionLevel::Admin);
  session.setCurrentDocument("assets/scenes/sample.scene.yaml",
                             demo::SceneSourceKind::Asset);

  const auto decision = session.decideSaveTarget(std::nullopt, "sample");
  EXPECT(decision.kind == demo::SceneSourceKind::Asset,
         "admin asset save should stay asset");
  EXPECT(!decision.redirectedFromAsset,
         "admin asset save should not redirect");
  EXPECT(decision.path.filename() == "sample.scene.yaml",
         "admin asset save should keep same path");
}

void testLocalSaveStaysInPlace() {
  auto session = makeSession();
  session.setCurrentDocument("data/scenes/work.scene.yaml",
                             demo::SceneSourceKind::Local);

  const auto decision = session.decideSaveTarget(std::nullopt, "work");
  EXPECT(decision.kind == demo::SceneSourceKind::Local,
         "local save should stay local");
  EXPECT(decision.path.filename() == "work.scene.yaml",
         "local save should keep existing path");
}

void testUnnamedSceneSaveCreatesTimestampedLocalFile() {
  auto session = makeSession();
  const auto decision = session.decideSaveTarget(std::nullopt, "Untitled Scene");
  EXPECT(decision.kind == demo::SceneSourceKind::Local,
         "unnamed scene should save to local");
  EXPECT(decision.path.filename() ==
             "Untitled_Scene.2026-05-11-153000.scene.yaml",
         "unnamed scene should create timestamped local file");
}

} // namespace

int main() {
  testSessionDefaultsToUser();
  testUserSavingAssetRedirectsToTimestampedLocalCopy();
  testAdminSavingAssetStaysOnAssetPath();
  testLocalSaveStaysInPlace();
  testUnnamedSceneSaveCreatesTimestampedLocalFile();

  if (failures != 0) {
    std::cerr << "test_scene_session failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_session passed\n";
  return 0;
}
