#include "infra/scene_io/scene_validation_profile.hpp"

#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace LX_infra::scene_io {
namespace {

[[nodiscard]] bool readBool(const YAML::Node &node, bool fallback) {
  return node ? node.as<bool>() : fallback;
}

[[nodiscard]] u32 readU32(const YAML::Node &node, u32 fallback) {
  return node ? node.as<u32>() : fallback;
}

[[nodiscard]] std::string readString(const YAML::Node &node,
                                     std::string fallback) {
  return node ? node.as<std::string>() : std::move(fallback);
}

[[nodiscard]] LX_core::image::ToneMappingMode
parseToneMappingMode(const std::string &value) {
  if (value == "aces" || value == "Aces" || value == "ACES") {
    return LX_core::image::ToneMappingMode::Aces;
  }
  if (value == "reinhard" || value == "Reinhard") {
    return LX_core::image::ToneMappingMode::Reinhard;
  }
  throw std::runtime_error("Unknown validation toneMapping mode: " + value);
}

} // namespace

ValidationSourceMode parseValidationSourceMode(std::string_view value) {
  if (value == "source" || value == "Source") {
    return ValidationSourceMode::Source;
  }
  if (value == "package" || value == "Package") {
    return ValidationSourceMode::Package;
  }
  throw std::runtime_error("Unknown validation sourceMode: " +
                           std::string(value));
}

SceneValidationProfile
parseSceneValidationProfileYaml(std::string_view yamlText) {
  const YAML::Node root = YAML::Load(std::string(yamlText));
  SceneValidationProfile profile;

  const YAML::Node validation = root["renderValidation"];
  if (validation) {
    profile.sourceMode = parseValidationSourceMode(
        readString(validation["sourceMode"], "source"));
    profile.scenePath = readString(validation["scenePath"], "");
    profile.packagePath = readString(validation["packagePath"], "");
    profile.activeTechnique =
        readString(validation["activeTechnique"], profile.activeTechnique);
    profile.shadows = readBool(validation["shadows"], profile.shadows);
    profile.ibl = readBool(validation["ibl"], profile.ibl);
    profile.transparency =
        readBool(validation["transparency"], profile.transparency);
  }

  const YAML::Node realtime = root["realtimeRender"];
  if (realtime) {
    profile.cameraPath = readString(realtime["camera"], profile.cameraPath);
    profile.width = readU32(realtime["width"], profile.width);
    profile.height = readU32(realtime["height"], profile.height);
    profile.debugDump = readBool(realtime["debugDump"], profile.debugDump);
    profile.outputPath = readString(realtime["outputPath"], "");
  }

  const YAML::Node offline = root["offlineRender"];
  if (offline) {
    profile.randomSeed = readU32(offline["seed"], profile.randomSeed);
    profile.samples = readU32(offline["samples"], profile.samples);
  }

  const YAML::Node tone = root["toneMapping"];
  if (tone) {
    profile.toneMapping.mode =
        parseToneMappingMode(readString(tone["mode"], "aces"));
    if (tone["exposure"]) {
      profile.toneMapping.exposure = tone["exposure"].as<float>();
    }
    if (tone["gamma"]) {
      profile.toneMapping.gamma = tone["gamma"].as<float>();
    }
  }

  if (profile.sourceMode == ValidationSourceMode::Source &&
      profile.scenePath.empty()) {
    throw std::runtime_error(
        "renderValidation.scenePath is required for source mode");
  }
  if (profile.sourceMode == ValidationSourceMode::Package &&
      profile.packagePath.empty()) {
    throw std::runtime_error(
        "renderValidation.packagePath is required for package mode");
  }
  if (profile.width == 0 || profile.height == 0) {
    throw std::runtime_error("realtimeRender width/height must be non-zero");
  }
  return profile;
}

} // namespace LX_infra::scene_io
