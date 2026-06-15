#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/platform/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace LX_infra::scene_io {

enum class ValidationSourceMode {
  Source,
  Package,
};

enum class ValidationProfileKind {
  Standard,
  Debug,
  Helmet,
  BMW,
  Req071,
};

struct SceneValidationProfile final {
  ValidationSourceMode sourceMode = ValidationSourceMode::Source;
  ValidationProfileKind profileKind = ValidationProfileKind::Standard;
  std::filesystem::path scenePath;
  std::filesystem::path packagePath;
  std::string cameraPath = "/game_cam";
  u32 width = 128;
  u32 height = 128;
  u32 randomSeed = 1;
  u32 samples = 1;
  bool debugDump = false;
  bool shadows = false;
  bool ibl = false;
  bool transparency = false;
  bool materialV2Strict = true;
  bool allowMaterialV2StrictOptOut = false;
  LX_core::image::ToneMappingSettings toneMapping;
  std::filesystem::path outputPath;
};

[[nodiscard]] ValidationSourceMode
parseValidationSourceMode(std::string_view value);

[[nodiscard]] ValidationProfileKind
parseValidationProfileKind(std::string_view value);

[[nodiscard]] SceneValidationProfile
parseSceneValidationProfileYaml(std::string_view yamlText);

} // namespace LX_infra::scene_io
