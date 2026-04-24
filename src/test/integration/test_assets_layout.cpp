#include "core/utils/filesystem_tools.hpp"
#include "core/utils/env.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>

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

void testDamagedHelmet() {
  auto saved = std::filesystem::current_path();
  bool ok = cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  EXPECT(ok, "DamagedHelmet.gltf must be found");
  std::filesystem::current_path(saved);
}

void testHdrEnvironmentMap() {
  auto saved = std::filesystem::current_path();
  bool ok = cdToWhereAssetsExist("env/studio_small_03_2k.hdr");
  EXPECT(ok, "studio_small_03_2k.hdr must be found");
  std::filesystem::current_path(saved);
}

void testVikingRoom() {
  auto saved = std::filesystem::current_path();
  bool ok = cdToWhereAssetsExist("models/viking_room/viking_room.obj");
  EXPECT(ok, "viking_room.obj must be found");
  std::filesystem::current_path(saved);
}

void testNonexistent() {
  auto saved = std::filesystem::current_path();
  bool ok = cdToWhereAssetsExist("nonexistent/foo.bar");
  EXPECT(!ok, "nonexistent path must not be found");
  std::filesystem::current_path(saved);
}

void testShaderLookupKeepsProjectRoot() {
  namespace fs = std::filesystem;
  const auto saved = fs::current_path();
  fs::path probe = saved / "src" / "test";
  if (!fs::exists(probe)) {
    probe = saved / "build" / "src" / "test";
  }
  if (!fs::exists(probe)) {
    EXPECT(false, "expected src/test or build/src/test to exist");
    return;
  }

  fs::current_path(probe);
  const bool ok = cdToWhereResourcesCouldFound("blinnphong_0");
  EXPECT(ok, "blinnphong_0 shaders must be found");
  EXPECT(fs::exists(fs::current_path() / "materials" / "blinnphong_default.material"),
         "shader lookup should leave cwd at a directory that can see materials/");
  EXPECT(fs::exists(fs::current_path() / "assets"),
         "resource lookup should leave cwd at a directory that can see assets/");
  fs::current_path(saved);
}

void testInitializeRuntimeAssetRootFromBuildDir() {
  namespace fs = std::filesystem;
  const auto saved = fs::current_path();
  fs::path probe = saved / "build";
  if (!fs::exists(probe)) {
    probe = saved;
  }

  fs::current_path(probe);
  const bool ok = initializeRuntimeAssetRoot();
  EXPECT(ok, "runtime asset root should initialize from build or repo root");
  if (ok) {
    const fs::path root = getRuntimeAssetRoot();
    EXPECT(fs::exists(root / "assets"),
           "runtime root should expose assets/");
    EXPECT(fs::exists(root / "materials"),
           "runtime root should expose materials/");
    EXPECT(fs::exists(getRuntimeShaderSourceDir() / "blinnphong_0.vert"),
           "runtime root should expose GLSL sources");
  }
  fs::current_path(saved);
}

void testPackagedStyleRuntimeRootFromEnv() {
  namespace fs = std::filesystem;
  const auto saved = fs::current_path();
  const fs::path repoRoot = saved;
  const fs::path tmpRoot = fs::temp_directory_path() / "lxengine_packaged_root";
  std::error_code ec;
  fs::remove_all(tmpRoot, ec);
  fs::create_directories(tmpRoot / "assets");
  fs::create_directories(tmpRoot / "materials");
  fs::create_directories(tmpRoot / "shaders" / "glsl");

  fs::copy_file(repoRoot / "assets" / "models" / "damaged_helmet" /
                    "DamagedHelmet.gltf",
                tmpRoot / "assets" / "DamagedHelmet.gltf",
                fs::copy_options::overwrite_existing);
  fs::copy_file(repoRoot / "materials" / "blinnphong_default.material",
                tmpRoot / "materials" / "blinnphong_default.material",
                fs::copy_options::overwrite_existing);
  fs::copy_file(repoRoot / "shaders" / "glsl" / "blinnphong_0.vert",
                tmpRoot / "shaders" / "glsl" / "blinnphong_0.vert",
                fs::copy_options::overwrite_existing);
  fs::copy_file(repoRoot / "shaders" / "glsl" / "blinnphong_0.frag",
                tmpRoot / "shaders" / "glsl" / "blinnphong_0.frag",
                fs::copy_options::overwrite_existing);
  std::ofstream(tmpRoot / "shaders" / "glsl" / "blinnphong_0.vert.spv").put('\0');
  std::ofstream(tmpRoot / "shaders" / "glsl" / "blinnphong_0.frag.spv").put('\0');

  setenv("LX_RUNTIME_ROOT", tmpRoot.string().c_str(), 1);
  const bool ok = initializeRuntimeAssetRoot(tmpRoot / "nested");
  EXPECT(ok, "runtime root should initialize from LX_RUNTIME_ROOT");
  if (ok) {
    EXPECT(getRuntimeAssetRoot() == fs::absolute(tmpRoot),
           "LX_RUNTIME_ROOT should become authoritative runtime root");
    EXPECT(resolveRuntimePath("materials/blinnphong_default.material") ==
               fs::absolute(tmpRoot / "materials" /
                            "blinnphong_default.material"),
           "relative runtime path should resolve inside packaged root");
    EXPECT(getShaderPath("blinnphong_0", "vert.spv") ==
               fs::absolute(tmpRoot / "shaders" / "glsl" /
                            "blinnphong_0.vert.spv")
                   .string(),
           "shader binary path should resolve inside packaged root");
  }
  unsetenv("LX_RUNTIME_ROOT");
  fs::remove_all(tmpRoot, ec);
  fs::current_path(saved);
}

} // namespace

int main() {
  expSetEnvVK();
  testDamagedHelmet();
  testHdrEnvironmentMap();
  testVikingRoom();
  testNonexistent();
  testShaderLookupKeepsProjectRoot();
  testInitializeRuntimeAssetRootFromBuildDir();
  testPackagedStyleRuntimeRootFromEnv();

  if (failures == 0) {
    std::cout << "[PASS] All asset layout tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
