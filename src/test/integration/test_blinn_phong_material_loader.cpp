#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/blinn_phong_material_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

int runSelf(const std::filesystem::path &self, const char *mode) {
  const std::string cmd =
      "\"" + self.string() + "\" " + mode + " >/dev/null 2>&1";
  return std::system(cmd.c_str());
}

int invalidNormalWithoutLightingMode() {
  (void)cdToWhereShadersExist("blinnphong_0");
  auto material = loadBlinnPhongMaterial(
      ResourcePassFlag::Forward,
      {ShaderVariant{"USE_LIGHTING", false},
       ShaderVariant{"USE_UV", true},
       ShaderVariant{"USE_NORMAL_MAP", true}});
  (void)material;
  return 0;
}

int invalidNormalWithoutUvMode() {
  (void)cdToWhereShadersExist("blinnphong_0");
  auto material = loadBlinnPhongMaterial(
      ResourcePassFlag::Forward,
      {ShaderVariant{"USE_LIGHTING", true},
       ShaderVariant{"USE_UV", false},
       ShaderVariant{"USE_NORMAL_MAP", true}});
  (void)material;
  return 0;
}

void testLoaderFatalSubprocesses(const std::filesystem::path &self) {
  EXPECT(runSelf(self, "--invalid-normal-without-lighting") != 0,
         "normal map without lighting must terminate");
  EXPECT(runSelf(self, "--invalid-normal-without-uv") != 0,
         "normal map without uv must terminate");
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    const std::string mode = argv[1];
    if (mode == "--invalid-normal-without-lighting")
      return invalidNormalWithoutLightingMode();
    if (mode == "--invalid-normal-without-uv")
      return invalidNormalWithoutUvMode();
  }

  if (!cdToWhereShadersExist("blinnphong_0")) {
    std::cerr << "SKIP: failed to locate shader assets for blinnphong_0\n";
    return 0;
  }

  testLoaderFatalSubprocesses(std::filesystem::absolute(argv[0]));

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: all blinn phong material loader tests passed\n";
  return 0;
}
