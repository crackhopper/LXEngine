#include "infra/scene_io/scene_validation_profile.hpp"

#include <iostream>
#include <stdexcept>

using namespace LX_infra::scene_io;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testSourceProfileParsesDirectValidationSettings() {
  const SceneValidationProfile profile = parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  scenePath: assets/scenes/helmet.yaml
  activeTechnique: ForwardDirect
  shadows: false
  ibl: false
  transparency: false
realtimeRender:
  camera: /validation_camera
  width: 128
  height: 64
  debugDump: true
  outputPath: artifacts/071/helmet
offlineRender:
  seed: 17
  samples: 4
toneMapping:
  mode: reinhard
  exposure: 0.75
  gamma: 2.0
)yaml");

  EXPECT(profile.sourceMode == ValidationSourceMode::Source,
         "sourceMode should parse source");
  EXPECT(profile.scenePath == "assets/scenes/helmet.yaml",
         "scene path should parse");
  EXPECT(profile.activeTechnique == "ForwardDirect",
         "active technique should parse");
  EXPECT(profile.cameraPath == "/validation_camera",
         "camera path should parse");
  EXPECT(profile.width == 128 && profile.height == 64,
         "resolution should parse");
  EXPECT(profile.debugDump, "debug dump flag should parse");
  EXPECT(profile.outputPath == "artifacts/071/helmet",
         "output path should parse");
  EXPECT(profile.randomSeed == 17 && profile.samples == 4,
         "offline seed and samples should parse");
  EXPECT(!profile.shadows && !profile.ibl && !profile.transparency,
         "direct validation disables shadows, IBL and transparency");
  EXPECT(profile.toneMapping.mode == LX_core::image::ToneMappingMode::Reinhard,
         "tone mapping mode should parse");
  EXPECT(profile.toneMapping.exposure == 0.75f &&
             profile.toneMapping.gamma == 2.0f,
         "tone mapping numeric fields should parse");
}

void testPackageProfileRequiresPackagePath() {
  const SceneValidationProfile profile = parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: package
  packagePath: artifacts/071/helmet.lxpkg
  activeTechnique: DeferredDirect
realtimeRender:
  width: 32
  height: 32
)yaml");

  EXPECT(profile.sourceMode == ValidationSourceMode::Package,
         "sourceMode should parse package");
  EXPECT(profile.packagePath == "artifacts/071/helmet.lxpkg",
         "package path should parse");
  EXPECT(profile.activeTechnique == "DeferredDirect",
         "package profile technique should parse");
}

void testMissingModePathFailsWithDiagnostic() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: package
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    EXPECT(std::string(error.what()).find("packagePath") != std::string::npos,
           "missing package path diagnostic should name packagePath");
  }
  EXPECT(threw, "missing package path should throw");
}

void testZeroResolutionFails() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  scenePath: assets/scenes/helmet.yaml
realtimeRender:
  width: 0
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    EXPECT(std::string(error.what()).find("width/height") != std::string::npos,
           "zero resolution diagnostic should name width/height");
  }
  EXPECT(threw, "zero resolution should throw");
}

} // namespace

int main() {
  testSourceProfileParsesDirectValidationSettings();
  testPackageProfileRequiresPackagePath();
  testMissingModePathFailsWithDiagnostic();
  testZeroResolutionFails();

  if (g_failures != 0) {
    std::cerr << g_failures << " render validation profile checks failed\n";
    return 1;
  }
  return 0;
}
