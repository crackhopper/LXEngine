#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

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

void test_loads_damaged_helmet() {
  const bool ok = cdToWhereAssetsExist(
      "models/damaged_helmet/DamagedHelmet.gltf");
  if (!ok) {
    std::cerr << "[FAIL] test_loads_damaged_helmet: "
                 "cdToWhereAssetsExist could not locate DamagedHelmet\n";
    ++failures;
    return;
  }

  infra::GLTFLoader loader;
  try {
    loader.load("assets/models/damaged_helmet/DamagedHelmet.gltf");
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] test_loads_damaged_helmet: load threw: " << e.what()
              << "\n";
    ++failures;
    return;
  }

  EXPECT(!loader.getPositions().empty(), "positions must be non-empty");
  EXPECT(!loader.getNormals().empty(), "normals must be non-empty");
  EXPECT(!loader.getTexCoords().empty(), "texCoords must be non-empty");
  EXPECT(!loader.getIndices().empty(), "indices must be non-empty");
  EXPECT(loader.getIndices().size() % 3 == 0,
         "indices.size() must be a multiple of 3");
  // The DamagedHelmet glTF in this repo does NOT declare TANGENT.
  // Spec requirement: getTangents() must return empty when TANGENT is absent,
  // and the loader must not throw for this reason. Assert that contract here.
  EXPECT(loader.getTangents().empty(),
         "DamagedHelmet does not declare TANGENT; getTangents() must be empty");

  const auto &mat = loader.getMaterial();
  EXPECT(!mat.baseColorTexture.empty(),
         "DamagedHelmet baseColorTexture must be non-empty");
  EXPECT(!mat.metallicRoughnessTexture.empty(),
         "DamagedHelmet metallicRoughnessTexture must be non-empty");
  EXPECT(!mat.normalTexture.empty(),
         "DamagedHelmet normalTexture must be non-empty");
  EXPECT(!mat.occlusionTexture.empty(),
         "DamagedHelmet occlusionTexture must be non-empty");
  EXPECT(!mat.emissiveTexture.empty(),
         "DamagedHelmet emissiveTexture must be non-empty");
  EXPECT(mat.baseColorFactor[3] > 0.0f,
         "DamagedHelmet baseColorFactor.w should be > 0");
}

void test_throws_on_missing_file() {
  infra::GLTFLoader loader;
  bool threw = false;
  std::string what;
  try {
    loader.load("/no/such/path/does_not_exist.gltf");
  } catch (const std::runtime_error &e) {
    threw = true;
    what = e.what();
  }
  EXPECT(threw, "missing file must throw std::runtime_error");
  EXPECT(what.find("does_not_exist.gltf") != std::string::npos,
         "error message must contain the file path");
}

void test_throws_on_corrupt_file() {
  // A valid OBJ file is not a valid glTF document.
  const bool ok = cdToWhereAssetsExist(
      "models/viking_room/viking_room.obj");
  if (!ok) {
    std::cerr << "[FAIL] test_throws_on_corrupt_file: "
                 "cdToWhereAssetsExist could not locate viking_room\n";
    ++failures;
    return;
  }

  infra::GLTFLoader loader;
  bool threw = false;
  try {
    loader.load("assets/models/viking_room/viking_room.obj");
  } catch (const std::runtime_error &) {
    threw = true;
  }
  EXPECT(threw, "non-glTF file must throw std::runtime_error");
}

} // namespace

int main() {
  expSetEnvVK();
  test_loads_damaged_helmet();
  test_throws_on_missing_file();
  test_throws_on_corrupt_file();

  if (failures == 0) {
    std::cout << "[PASS] All gltf loader tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
