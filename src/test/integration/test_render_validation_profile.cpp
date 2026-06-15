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
  EXPECT(profile.profileKind == ValidationProfileKind::Standard,
         "profileKind should default to standard");
  EXPECT(profile.scenePath == "assets/scenes/helmet.yaml",
         "scene path should parse");
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
  EXPECT(profile.materialV2Strict,
         "render validation should enable Material v2 strict bridge checks by "
         "default");
  EXPECT(profile.toneMapping.mode == LX_core::image::ToneMappingMode::Reinhard,
         "tone mapping mode should parse");
  EXPECT(profile.toneMapping.exposure == 0.75f &&
             profile.toneMapping.gamma == 2.0f,
         "tone mapping numeric fields should parse");
}

void testHelmetProfileCannotDisableMaterialV2Strict() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: helmet
  scenePath: assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
  materialV2Strict: false
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    const std::string message = error.what();
    EXPECT(message.find("materialV2Strict") != std::string::npos,
           "strict-profile opt-out diagnostic should name materialV2Strict");
    EXPECT(message.find("helmet/BMW/req071") != std::string::npos,
           "strict-profile opt-out diagnostic should name the protected "
           "profile family");
  }
  EXPECT(threw, "helmet/BMW/071 validation profiles may not disable strict "
                "Material v2 checks");
}

void testHelmetProfileKindCannotDisableMaterialV2StrictWithoutPathToken() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: helmet
  scenePath: assets/scenes/direct_validation.scene.yaml
  materialV2Strict: false
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    const std::string message = error.what();
    EXPECT(message.find("profileKind") != std::string::npos,
           "strict-profile opt-out diagnostic should name profileKind");
    EXPECT(message.find("materialV2Strict") != std::string::npos,
           "strict-profile opt-out diagnostic should name materialV2Strict");
    EXPECT(message.find("helmet/BMW/req071") != std::string::npos,
           "strict-profile opt-out diagnostic should name the protected "
           "profile family");
  }
  EXPECT(threw, "helmet profileKind should reject strict opt-out without "
                "requiring a helmet path token");
}

void testReq071ProfileKindCannotDisableMaterialV2Strict() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: req071
  scenePath: assets/scenes/direct_validation.scene.yaml
  materialV2Strict: false
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    const std::string message = error.what();
    EXPECT(message.find("profileKind") != std::string::npos,
           "REQ-071 strict opt-out diagnostic should name profileKind");
    EXPECT(message.find("materialV2Strict") != std::string::npos,
           "REQ-071 strict opt-out diagnostic should name materialV2Strict");
    EXPECT(message.find("helmet/BMW/req071") != std::string::npos,
           "REQ-071 strict opt-out diagnostic should name the protected "
           "profile family");
  }
  EXPECT(threw, "REQ-071 profileKind should reject strict opt-out");
}

void testStandardProfileKindCannotDisableMaterialV2Strict() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: standard
  scenePath: assets/scenes/direct_validation.scene.yaml
  materialV2Strict: false
  allowMaterialV2StrictOptOut: true
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    const std::string message = error.what();
    EXPECT(message.find("profileKind") != std::string::npos,
           "standard opt-out diagnostic should name profileKind");
    EXPECT(message.find("materialV2Strict") != std::string::npos,
           "standard opt-out diagnostic should name materialV2Strict");
  }
  EXPECT(threw, "standard profileKind should reject Material v2 strict opt-out");
}

void testBmwProfileOmittedMaterialV2StrictStillRequiresStrictPolicy() {
  const SceneValidationProfile profile = parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: bmw
  scenePath: assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
realtimeRender:
  width: 32
  height: 32
)yaml");

  EXPECT(profile.materialV2Strict,
         "BMW validation profile should require strict Material v2 policy even "
         "when materialV2Strict is omitted");
  EXPECT(profile.profileKind == ValidationProfileKind::BMW,
         "BMW validation profileKind should parse");
}

void testMaterialV2StrictCanBeDisabledForExplicitNon071Diagnostics() {
  const SceneValidationProfile profile = parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  profileKind: debug
  scenePath: assets/scenes/debug_material_probe.scene.yaml
  materialV2Strict: false
  allowMaterialV2StrictOptOut: true
realtimeRender:
  width: 32
  height: 32
)yaml");

  EXPECT(!profile.materialV2Strict,
         "diagnostic profiles should be able to disable Material v2 strict "
         "checks explicitly");
  EXPECT(profile.profileKind == ValidationProfileKind::Debug,
         "debug validation profileKind should parse");
  EXPECT(profile.allowMaterialV2StrictOptOut,
         "debug validation strict opt-out flag should parse");
}

void testPackageProfileRequiresPackagePath() {
  const SceneValidationProfile profile = parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: package
  packagePath: artifacts/071/helmet.lxpkg
realtimeRender:
  width: 32
  height: 32
)yaml");

  EXPECT(profile.sourceMode == ValidationSourceMode::Package,
         "sourceMode should parse package");
  EXPECT(profile.packagePath == "artifacts/071/helmet.lxpkg",
         "package path should parse");
}

void testActiveTechniqueFieldIsRejected() {
  bool threw = false;
  try {
    (void)parseSceneValidationProfileYaml(R"yaml(
renderValidation:
  sourceMode: source
  scenePath: assets/scenes/helmet.yaml
  activeTechnique: ForwardDirect
realtimeRender:
  width: 32
  height: 32
)yaml");
  } catch (const std::runtime_error &error) {
    threw = true;
    const std::string message = error.what();
    EXPECT(message.find("activeTechnique") != std::string::npos,
           "deleted technique field diagnostic should name activeTechnique");
    EXPECT(message.find("RenderPathGraph") != std::string::npos,
           "deleted technique field diagnostic should point to render path "
           "graphs");
  }
  EXPECT(threw, "renderValidation.activeTechnique should be rejected");
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
  testHelmetProfileCannotDisableMaterialV2Strict();
  testHelmetProfileKindCannotDisableMaterialV2StrictWithoutPathToken();
  testReq071ProfileKindCannotDisableMaterialV2Strict();
  testStandardProfileKindCannotDisableMaterialV2Strict();
  testBmwProfileOmittedMaterialV2StrictStillRequiresStrictPolicy();
  testMaterialV2StrictCanBeDisabledForExplicitNon071Diagnostics();
  testPackageProfileRequiresPackagePath();
  testActiveTechniqueFieldIsRejected();
  testMissingModePathFailsWithDiagnostic();
  testZeroResolutionFails();

  if (g_failures != 0) {
    std::cerr << g_failures << " render validation profile checks failed\n";
    return 1;
  }
  return 0;
}
