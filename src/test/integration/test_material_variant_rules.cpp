#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace LX_core;
using namespace LX_infra;

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

namespace fs = std::filesystem;

int runSelf(const fs::path &self, const char *mode) {
  const std::string cmd =
      "\"" + self.string() + "\" " + mode + " >/dev/null 2>&1";
  return std::system(cmd.c_str());
}

fs::path findMaterialsDir() {
  auto p = fs::current_path();
  for (int i = 0; i < 5; ++i) {
    if (fs::exists(p / "materials"))
      return p / "materials";
    auto parent = p.parent_path();
    if (parent == p)
      break;
    p = parent;
  }
  return p / "materials";
}

void writeTempMaterial(const fs::path &path, const std::string &content) {
  std::ofstream out(path);
  out << content;
}

int invalidNormalWithoutLightingMode() {
  auto tmpPath = findMaterialsDir() / "test_invalid_normal_no_light.material";
  writeTempMaterial(tmpPath,
      "shader: blinnphong_0\n"
      "variants:\n"
      "  USE_LIGHTING: false\n"
      "  USE_UV: true\n"
      "  USE_NORMAL_MAP: true\n"
      "variantRules:\n"
      "  - requires: [USE_NORMAL_MAP]\n"
      "    depends: [USE_LIGHTING, USE_UV]\n"
      "passes:\n"
      "  Forward:\n"
      "    renderState:\n"
      "      depthTest: true\n");
  loadGenericMaterial(tmpPath);
  fs::remove(tmpPath);
  return 0;
}

int invalidNormalWithoutUvMode() {
  auto tmpPath = findMaterialsDir() / "test_invalid_normal_no_uv.material";
  writeTempMaterial(tmpPath,
      "shader: blinnphong_0\n"
      "variants:\n"
      "  USE_LIGHTING: true\n"
      "  USE_UV: false\n"
      "  USE_NORMAL_MAP: true\n"
      "variantRules:\n"
      "  - requires: [USE_NORMAL_MAP]\n"
      "    depends: [USE_LIGHTING, USE_UV]\n"
      "passes:\n"
      "  Forward:\n"
      "    renderState:\n"
      "      depthTest: true\n");
  loadGenericMaterial(tmpPath);
  fs::remove(tmpPath);
  return 0;
}

void testVariantRuleFatalSubprocesses(const fs::path &self) {
  EXPECT(runSelf(self, "--invalid-normal-without-lighting") != 0,
         "normal map without lighting must terminate");
  EXPECT(runSelf(self, "--invalid-normal-without-uv") != 0,
         "normal map without uv must terminate");
}

void testValidVariantCombination() {
  auto tmpPath = findMaterialsDir() / "test_valid_variants.material";
  writeTempMaterial(tmpPath,
      "shader: blinnphong_0\n"
      "variants:\n"
      "  USE_LIGHTING: true\n"
      "  USE_UV: true\n"
      "  USE_NORMAL_MAP: true\n"
      "variantRules:\n"
      "  - requires: [USE_NORMAL_MAP]\n"
      "    depends: [USE_LIGHTING, USE_UV]\n"
      "passes:\n"
      "  Forward:\n"
      "    renderState:\n"
      "      depthTest: true\n");
  auto mat = loadGenericMaterial(tmpPath);
  fs::remove(tmpPath);
  EXPECT(mat != nullptr, "valid variant combination should produce a material");
}

} // namespace

int main(int argc, char **argv) {
  expSetEnvVK();
  if (argc > 1) {
    const std::string mode = argv[1];
    if (mode == "--invalid-normal-without-lighting")
      return invalidNormalWithoutLightingMode();
    if (mode == "--invalid-normal-without-uv")
      return invalidNormalWithoutUvMode();
  }

  if (!cdToWhereResourcesCouldFound("blinnphong_0")) {
    std::cerr << "SKIP: failed to locate shader assets for blinnphong_0\n";
    return 0;
  }

  testValidVariantCombination();
  testVariantRuleFatalSubprocesses(fs::absolute(argv[0]));

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: all material variant rules tests passed\n";
  return 0;
}
