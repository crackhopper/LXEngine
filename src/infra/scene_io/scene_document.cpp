#include "infra/scene_io/scene_document.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace LX_infra::scene_io {
namespace {

constexpr const char *kDefaultRootNodeName = "scene_root";
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] SceneNodeDocument makeDefaultRootNode() {
  SceneNodeDocument rootNode;
  rootNode.nodeName = kDefaultRootNodeName;
  return rootNode;
}

struct SceneDocumentData final {
  std::string sceneName = "Scene";
  std::string gameplayCameraPath = "/game_cam";
  std::optional<EnvironmentState> environment;
  std::optional<LX_core::offline::RenderProfileDocument> renderProfileDocument;
  SceneNodeDocument rootNode;
  std::optional<EditorCameraState> editorCamera;

  SceneDocumentData() : rootNode(makeDefaultRootNode()) {}
};

[[nodiscard]] std::string dumpYamlNode(const YAML::Node &node) {
  YAML::Emitter out;
  out << node;
  return out.c_str();
}

void emitRawYamlNode(YAML::Emitter &out, const std::string &yamlText) {
  out << YAML::Load(yamlText);
}

[[nodiscard]] bool hasPrefix(std::string_view value,
                             std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

void validateSceneAssetUriInternal(const std::string &uri,
                                   const char *fieldName) {
  if (uri.empty()) {
    throw std::runtime_error(std::string(fieldName) + " must be non-empty");
  }
  if (uri.find(".asset_cache/") != std::string::npos ||
      uri.find(".asset_cache\\") != std::string::npos) {
    throw std::runtime_error(std::string(fieldName) +
                             " must use cache:// instead of .asset_cache");
  }
  if (!hasPrefix(uri, "cache://")) {
    return;
  }

  const std::string path = uri.substr(std::string("cache://").size());
  usize segmentCount = 0;
  bool previousSlash = true;
  for (const char ch : path) {
    if (ch == '\\') {
      throw std::runtime_error(std::string(fieldName) +
                               " cache URI must use forward slashes");
    }
    if (ch == '/') {
      if (previousSlash) {
        throw std::runtime_error(std::string(fieldName) +
                                 " cache URI contains an empty segment");
      }
      previousSlash = true;
      continue;
    }
    if (previousSlash) {
      ++segmentCount;
    }
    previousSlash = false;
  }
  if (previousSlash || segmentCount < 4) {
    throw std::runtime_error(std::string(fieldName) +
                             " cache URI must be cache://source/asset-id/"
                             "variant/relative-path");
  }
}

[[nodiscard]] LX_core::Vec3f loadVec3(const YAML::Node &node,
                                      const char *fieldName) {
  if (!node || !node.IsSequence() || node.size() != 3) {
    throw std::runtime_error(std::string("expected vec3 sequence for ") +
                             fieldName);
  }

  return LX_core::Vec3f{node[0].as<float>(), node[1].as<float>(),
                        node[2].as<float>()};
}

[[nodiscard]] std::optional<LX_core::Vec3f> loadOptionalVec3(
    const YAML::Node &node, const char *fieldName) {
  if (!node) {
    return std::nullopt;
  }
  return loadVec3(node, fieldName);
}

[[nodiscard]] LX_core::Transform makeWorldLookAtTransform(
    const LX_core::Vec3f &eye, const LX_core::Vec3f &target,
    const LX_core::Vec3f &upHint) {
  LX_core::Vec3f forward = (target - eye).normalized();
  if (forward.length2() <= 1e-8f) {
    forward = LX_core::Vec3f{0.0f, 0.0f, -1.0f};
  }

  LX_core::Vec3f up = upHint.normalized();
  if (up.length2() <= 1e-8f) {
    up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
  }

  const LX_core::Vec3f back = (-forward).normalized();
  LX_core::Vec3f right = up.cross(back);
  if (right.length2() <= 1e-8f) {
    const LX_core::Vec3f fallbackUp =
        std::abs(forward.y) > 0.99f ? LX_core::Vec3f{1.0f, 0.0f, 0.0f}
                                    : LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    right = fallbackUp.cross(back);
  }
  right = right.normalized();
  const LX_core::Vec3f correctedUp = back.cross(right).normalized();

  LX_core::Mat4f world = LX_core::Mat4f::identity();
  world(0, 0) = right.x;
  world(1, 0) = right.y;
  world(2, 0) = right.z;
  world(0, 1) = correctedUp.x;
  world(1, 1) = correctedUp.y;
  world(2, 1) = correctedUp.z;
  world(0, 2) = back.x;
  world(1, 2) = back.y;
  world(2, 2) = back.z;
  world(0, 3) = eye.x;
  world(1, 3) = eye.y;
  world(2, 3) = eye.z;
  return LX_core::Transform::fromMat4(world).normalized();
}

void saveVec3(YAML::Emitter &out, const LX_core::Vec3f &value) {
  out << YAML::Flow << YAML::BeginSeq << value.x << value.y << value.z
      << YAML::EndSeq;
}

[[nodiscard]] LX_core::MaterialParameterValue
loadMaterialParameterValue(const YAML::Node &node, const char *fieldName) {
  LX_core::MaterialParameterValue value;
  if (node.IsScalar()) {
    const std::string scalar = node.Scalar();
    if (scalar.find_first_of(".eE") == std::string::npos) {
      value.type = LX_core::MaterialParameterValueType::Int;
      value.intValue = node.as<i32>();
      return value;
    }
    value.type = LX_core::MaterialParameterValueType::Float;
    value.floatValue = node.as<float>();
    return value;
  }
  if (!node.IsSequence()) {
    throw std::runtime_error(std::string("expected scalar or vector for ") +
                             fieldName);
  }
  if (node.size() == 3) {
    value.type = LX_core::MaterialParameterValueType::Vec3;
    value.vectorValue = LX_core::Vec4f{node[0].as<float>(), node[1].as<float>(),
                                       node[2].as<float>(), 0.0f};
    return value;
  }
  if (node.size() == 4) {
    value.type = LX_core::MaterialParameterValueType::Vec4;
    value.vectorValue =
        LX_core::Vec4f{node[0].as<float>(), node[1].as<float>(),
                       node[2].as<float>(), node[3].as<float>()};
    return value;
  }
  throw std::runtime_error(std::string("expected Vec3 or Vec4 sequence for ") +
                           fieldName);
}

void saveMaterialParameterValue(YAML::Emitter &out,
                                const LX_core::MaterialParameterValue &value) {
  switch (value.type) {
  case LX_core::MaterialParameterValueType::Float:
    out << value.floatValue;
    break;
  case LX_core::MaterialParameterValueType::Int:
    out << value.intValue;
    break;
  case LX_core::MaterialParameterValueType::Vec3:
    out << YAML::Flow << YAML::BeginSeq << value.vectorValue.x
        << value.vectorValue.y << value.vectorValue.z << YAML::EndSeq;
    break;
  case LX_core::MaterialParameterValueType::Vec4:
    out << YAML::Flow << YAML::BeginSeq << value.vectorValue.x
        << value.vectorValue.y << value.vectorValue.z << value.vectorValue.w
        << YAML::EndSeq;
    break;
  }
}

[[nodiscard]] LX_core::Quatf loadQuat(const YAML::Node &node,
                                      const char *fieldName) {
  if (!node || !node.IsSequence() || node.size() != 4) {
    throw std::runtime_error(std::string("expected quat sequence for ") +
                             fieldName);
  }
  return LX_core::Quatf{node[0].as<float>(), node[1].as<float>(),
                        node[2].as<float>(), node[3].as<float>()}
      .normalized();
}

void saveQuat(YAML::Emitter &out, const LX_core::Quatf &value) {
  out << YAML::Flow << YAML::BeginSeq << value.w << value.v.x << value.v.y
      << value.v.z << YAML::EndSeq;
}

[[nodiscard]] CameraNodeState loadCameraState(const YAML::Node &node) {
  CameraNodeState state;
  if (const auto typeNode = node["type"]; typeNode) {
    const std::string type = typeNode.as<std::string>();
    if (type == "orthographic") {
      state.type = LX_core::CameraType::Orthographic;
    } else {
      state.type = LX_core::CameraType::Perspective;
    }
  }
  state.fovY = node["fovY"].as<float>();
  state.aspect = node["aspect"].as<float>();
  state.nearPlane = node["nearPlane"].as<float>();
  state.farPlane = node["farPlane"].as<float>();
  if (const auto orthographicHeight = node["orthographicHeight"];
      orthographicHeight) {
    state.orthographicHeight = orthographicHeight.as<float>();
  } else if (node["top"] && node["bottom"]) {
    state.orthographicHeight =
        std::abs(node["top"].as<float>() - node["bottom"].as<float>());
  }
  if (const auto focusDistance = node["focusDistance"]; focusDistance) {
    state.focusDistance = focusDistance.as<float>();
  } else if (node["eye"] && node["target"]) {
    state.focusDistance =
        (loadVec3(node["target"], "nodes[].camera.target") -
         loadVec3(node["eye"], "nodes[].camera.eye"))
            .length();
  }
  state.cullingMask = node["cullingMask"].as<LX_core::VisibilityLayerMask>();
  return state;
}

void saveCameraState(YAML::Emitter &out, const CameraNodeState &state,
                     const std::optional<std::string> &offlineYaml) {
  out << YAML::Key << "camera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "type" << YAML::Value
      << (state.type == LX_core::CameraType::Orthographic ? "orthographic"
                                                          : "perspective");
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "aspect" << YAML::Value << state.aspect;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
  if (state.type == LX_core::CameraType::Orthographic) {
    out << YAML::Key << "orthographicHeight" << YAML::Value
        << state.orthographicHeight;
  }
  out << YAML::Key << "focusDistance" << YAML::Value << state.focusDistance;
  out << YAML::Key << "cullingMask" << YAML::Value << state.cullingMask;
  if (offlineYaml.has_value()) {
    out << YAML::Key << "offline" << YAML::Value;
    emitRawYamlNode(out, *offlineYaml);
  }
  out << YAML::EndMap;
}

[[nodiscard]] LightKind loadLightKind(const YAML::Node &node) {
  const std::string kind = node.as<std::string>();
  if (kind == "Directional") {
    return LightKind::Directional;
  }
  if (kind == "Point") {
    return LightKind::Point;
  }
  if (kind == "Spot") {
    return LightKind::Spot;
  }
  throw std::runtime_error("unsupported light kind: " + kind);
}

[[nodiscard]] const char *lightKindName(const LightKind kind) {
  switch (kind) {
  case LightKind::Directional:
    return "Directional";
  case LightKind::Point:
    return "Point";
  case LightKind::Spot:
    return "Spot";
  }
  return "Directional";
}

[[nodiscard]] LightNodeState
loadLegacyDirectionalLightState(const YAML::Node &node) {
  LightNodeState state;
  state.kind = LightKind::Directional;
  state.direction =
      loadVec3(node["direction"], "nodes[].directionalLight.direction");
  state.color = loadVec3(node["color"], "nodes[].directionalLight.color");
  state.intensity = node["intensity"].as<float>();
  if (const auto shadowStrength = node["shadowStrength"]; shadowStrength) {
    state.shadowStrength = shadowStrength.as<float>();
  }
  if (const auto shadowDistance = node["shadowDistance"]; shadowDistance) {
    state.shadowDistance = shadowDistance.as<float>();
  }
  if (const auto shadowCascadeCount = node["shadowCascadeCount"];
      shadowCascadeCount) {
    state.shadowCascadeCount = shadowCascadeCount.as<u32>();
  }
  if (const auto offlineNode = node["offline"]; offlineNode) {
    state.offlineYaml = dumpYamlNode(offlineNode);
  }
  return state;
}

[[nodiscard]] LightNodeState loadLightState(const YAML::Node &node) {
  LightNodeState state;
  state.kind = loadLightKind(node["kind"]);
  if (state.kind == LightKind::Directional || state.kind == LightKind::Spot) {
    state.direction = loadVec3(node["direction"], "nodes[].light.direction");
  }
  state.color = loadVec3(node["color"], "nodes[].light.color");
  state.intensity = node["intensity"].as<float>();
  if (state.kind == LightKind::Directional) {
    if (const auto shadowStrength = node["shadowStrength"]; shadowStrength) {
      state.shadowStrength = shadowStrength.as<float>();
    }
    if (const auto shadowDistance = node["shadowDistance"]; shadowDistance) {
      state.shadowDistance = shadowDistance.as<float>();
    }
    if (const auto shadowCascadeCount = node["shadowCascadeCount"];
        shadowCascadeCount) {
      state.shadowCascadeCount = shadowCascadeCount.as<u32>();
    }
  }
  if (state.kind == LightKind::Point || state.kind == LightKind::Spot) {
    state.range = node["range"].as<float>();
  }
  if (state.kind == LightKind::Spot) {
    state.innerConeDegrees = node["innerConeDegrees"].as<float>();
    state.outerConeDegrees = node["outerConeDegrees"].as<float>();
  }
  if (const auto offlineNode = node["offline"]; offlineNode) {
    state.offlineYaml = dumpYamlNode(offlineNode);
  }
  return state;
}

void saveLightState(YAML::Emitter &out, const LightNodeState &state) {
  out << YAML::Key << "light" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "kind" << YAML::Value << lightKindName(state.kind);
  if (state.kind == LightKind::Directional || state.kind == LightKind::Spot) {
    out << YAML::Key << "direction" << YAML::Value;
    saveVec3(out, state.direction);
  }
  out << YAML::Key << "color" << YAML::Value;
  saveVec3(out, state.color);
  out << YAML::Key << "intensity" << YAML::Value << state.intensity;
  if (state.kind == LightKind::Directional) {
    out << YAML::Key << "shadowStrength" << YAML::Value << state.shadowStrength;
    out << YAML::Key << "shadowDistance" << YAML::Value << state.shadowDistance;
    out << YAML::Key << "shadowCascadeCount" << YAML::Value
        << state.shadowCascadeCount;
  }
  if (state.kind == LightKind::Point || state.kind == LightKind::Spot) {
    out << YAML::Key << "range" << YAML::Value << state.range;
  }
  if (state.kind == LightKind::Spot) {
    out << YAML::Key << "innerConeDegrees" << YAML::Value
        << state.innerConeDegrees;
    out << YAML::Key << "outerConeDegrees" << YAML::Value
        << state.outerConeDegrees;
  }
  if (state.offlineYaml.has_value()) {
    out << YAML::Key << "offline" << YAML::Value;
    emitRawYamlNode(out, *state.offlineYaml);
  }
  out << YAML::EndMap;
}

[[nodiscard]] MaterialOverrideState
loadMaterialOverrideState(const YAML::Node &node, const char *fieldName) {
  MaterialOverrideState state;
  if (!node) {
    return state;
  }
  if (!node.IsMap()) {
    throw std::runtime_error(std::string("expected map for ") + fieldName);
  }
  if (const auto baseColorNode = node["baseColor"]; baseColorNode) {
    state.baseColor = loadVec3(baseColorNode, fieldName);
  }
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string parameterKey = it->first.as<std::string>();
    if (parameterKey == "baseColor") {
      continue;
    }
    if (parameterKey.find('.') == std::string::npos) {
      throw std::runtime_error(std::string(fieldName) +
                               " key must use binding.member: " + parameterKey);
    }
    state.parameters.emplace(parameterKey,
                             loadMaterialParameterValue(it->second, fieldName));
  }
  return state;
}

void saveMaterialOverrideState(YAML::Emitter &out, const char *key,
                               const MaterialOverrideState &state) {
  if (state.empty()) {
    return;
  }
  out << YAML::Key << key << YAML::Value << YAML::BeginMap;
  if (state.baseColor.has_value()) {
    out << YAML::Key << "baseColor" << YAML::Value;
    saveVec3(out, *state.baseColor);
  }
  std::vector<std::string> parameterKeys;
  parameterKeys.reserve(state.parameters.size());
  for (const auto &[parameterKey, _] : state.parameters) {
    parameterKeys.push_back(parameterKey);
  }
  std::sort(parameterKeys.begin(), parameterKeys.end());
  for (const auto &parameterKey : parameterKeys) {
    out << YAML::Key << parameterKey << YAML::Value;
    saveMaterialParameterValue(out, state.parameters.at(parameterKey));
  }
  out << YAML::EndMap;
}

[[nodiscard]] ProceduralMaterialState
loadProceduralMaterialState(const YAML::Node &node, const char *fieldName) {
  ProceduralMaterialState state;
  if (!node) {
    return state;
  }
  if (!node.IsMap()) {
    throw std::runtime_error(std::string("expected map for ") + fieldName);
  }
  if (const auto enabled = node["enabled"]; enabled) {
    state.enabled = enabled.as<bool>();
  }
  if (const auto binding = node["binding"]; binding) {
    state.binding = binding.as<std::string>();
  }
  if (const auto timeMember = node["timeMember"]; timeMember) {
    state.timeMember = timeMember.as<std::string>();
  }
  if (const auto resolutionMember = node["resolutionMember"];
      resolutionMember) {
    state.resolutionMember = resolutionMember.as<std::string>();
  }
  if (const auto audioBandsMember = node["audioBandsMember"];
      audioBandsMember) {
    const std::string value = audioBandsMember.as<std::string>();
    state.audioBandsMember = value.empty() ? std::nullopt
                                           : std::optional<std::string>(value);
  }
  if (const auto audioChannelBinding = node["audioChannelBinding"];
      audioChannelBinding) {
    const std::string value = audioChannelBinding.as<std::string>();
    state.audioChannelBinding =
        value.empty() ? std::nullopt : std::optional<std::string>(value);
  }
  if (state.binding.empty() || state.timeMember.empty() ||
      state.resolutionMember.empty()) {
    throw std::runtime_error(std::string(fieldName) +
                             " binding/time/resolution members must be non-empty");
  }
  return state;
}

void saveProceduralMaterialState(YAML::Emitter &out,
                                 const ProceduralMaterialState &state) {
  if (state.empty()) {
    return;
  }
  out << YAML::Key << "proceduralMaterial" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "enabled" << YAML::Value << state.enabled;
  out << YAML::Key << "binding" << YAML::Value << state.binding;
  out << YAML::Key << "timeMember" << YAML::Value << state.timeMember;
  out << YAML::Key << "resolutionMember" << YAML::Value
      << state.resolutionMember;
  if (state.audioBandsMember.has_value()) {
    out << YAML::Key << "audioBandsMember" << YAML::Value
        << *state.audioBandsMember;
  }
  if (state.audioChannelBinding.has_value()) {
    out << YAML::Key << "audioChannelBinding" << YAML::Value
        << *state.audioChannelBinding;
  }
  out << YAML::EndMap;
}

[[nodiscard]] EnvironmentState loadEnvironmentState(const YAML::Node &node) {
  EnvironmentState state;
  if (!node) {
    return state;
  }
  if (!node.IsMap()) {
    throw std::runtime_error("expected map for scene.environment");
  }
  if (const auto enabled = node["enabled"]; enabled) {
    state.enabled = enabled.as<bool>();
  }
  if (const auto hdrUri = node["hdrUri"]; hdrUri) {
    state.hdrUri = hdrUri.as<std::string>();
    validateSceneAssetUriInternal(state.hdrUri, "scene.environment.hdrUri");
  }
  if (const auto skyboxEnabled = node["skyboxEnabled"]; skyboxEnabled) {
    state.skyboxEnabled = skyboxEnabled.as<bool>();
  }
  if (const auto intensity = node["intensity"]; intensity) {
    state.intensity = intensity.as<float>();
  }
  if (const auto roughnessMipCount = node["roughnessMipCount"];
      roughnessMipCount) {
    state.roughnessMipCount = roughnessMipCount.as<float>();
  }
  if (state.enabled && state.hdrUri.empty()) {
    throw std::runtime_error(
        "scene.environment.hdrUri must be non-empty when enabled");
  }
  return state;
}

[[nodiscard]] LX_core::offline::OutputCameraOverrides
loadOutputCameraOverrides(const YAML::Node &node,
                          const std::string &profileName) {
  if (!node) {
    return {};
  }
  if (!node.IsMap()) {
    throw std::runtime_error("scene.outputProfiles." + profileName +
                             ".cameraOverrides must be a map");
  }

  LX_core::offline::OutputCameraOverrides overrides;
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string key = it->first.as<std::string>();
    const YAML::Node value = it->second;
    if (key == "fovY") {
      overrides.fovY = value.as<float>();
    } else if (key == "aspect") {
      overrides.aspect = value.as<float>();
    } else if (key == "nearPlane") {
      overrides.nearPlane = value.as<float>();
    } else if (key == "farPlane") {
      overrides.farPlane = value.as<float>();
    } else if (key == "orthographicHeight") {
      overrides.orthographicHeight = value.as<float>();
    } else if (key == "cullingMask") {
      overrides.cullingMask = value.as<u32>();
    } else {
      throw std::runtime_error("unsupported camera override field in output "
                               "profile " +
                               profileName + ": " + key);
    }
  }
  return overrides;
}

[[nodiscard]] LX_core::offline::OutputProfile
loadOutputProfile(const YAML::Node &node, const std::string &name) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error("scene.outputProfiles." + name +
                             " must be a map");
  }

  LX_core::offline::OutputProfile profile;
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string key = it->first.as<std::string>();
    const YAML::Node value = it->second;
    if (key == "backend") {
      throw std::runtime_error("scene.outputProfiles." + name +
                               ".backend is no longer supported");
    }
    if (key == "integrator" || key == "samples" || key == "seed" ||
        key == "maxBounce") {
      throw std::runtime_error("scene.outputProfiles." + name + "." + key +
                               " is no longer supported; offline render "
                               "settings belong under scene.offlineRender");
    }
    if (key == "maxDepth") {
      throw std::runtime_error("scene.outputProfiles." + name +
                               ".maxDepth is no longer supported; use "
                               "scene.offlineRender.maxBounce");
    }
    if (key == "profiles") {
      throw std::runtime_error("scene.outputProfiles." + name +
                               ".profiles is no longer supported");
    }
    if (key == "camera") {
      profile.cameraPath = value.as<std::string>();
    } else if (key == "width") {
      profile.width = value.as<u32>();
    } else if (key == "height") {
      profile.height = value.as<u32>();
    } else if (key == "outputFormat") {
      profile.outputFormat = value.as<std::string>();
    } else if (key == "outDir") {
      profile.outDir = value.as<std::string>();
    } else if (key == "cameraOverrides") {
      profile.cameraOverrides = loadOutputCameraOverrides(value, name);
    } else {
      profile.extensionYamlByField.emplace(key, dumpYamlNode(value));
    }
  }

  if (profile.cameraPath.empty()) {
    throw std::runtime_error("output profile " + name +
                             " camera must be non-empty");
  }
  if (profile.width == 0 || profile.height == 0) {
    throw std::runtime_error("output profile " + name +
                             " width/height must be positive");
  }
  if (profile.outputFormat != "png" && profile.outputFormat != "exr-png") {
    throw std::runtime_error(
        "unsupported outputFormat in output profile " + name + ": " +
        profile.outputFormat);
  }
  return profile;
}

[[nodiscard]] LX_core::offline::OfflineRenderSettings
loadOfflineRenderSettings(const YAML::Node &node) {
  if (!node) {
    return LX_core::offline::makeDefaultOfflineRenderSettings();
  }
  if (!node.IsMap()) {
    throw std::runtime_error("scene.offlineRender must be a map");
  }

  LX_core::offline::OfflineRenderSettings settings =
      LX_core::offline::makeDefaultOfflineRenderSettings();
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string key = it->first.as<std::string>();
    const YAML::Node value = it->second;
    if (key == "profiles") {
      throw std::runtime_error(
          "scene.offlineRender.profiles is no longer supported; use "
          "scene.outputProfiles plus scene.offlineRender");
    }
    if (key == "defaultProfile") {
      throw std::runtime_error(
          "scene.offlineRender.defaultProfile is no longer supported; use "
          "scene.defaultOutputProfile and scene.offlineRender.profile");
    }
    if (key == "backend") {
      throw std::runtime_error(
          "scene.offlineRender.backend is no longer supported");
    }
    if (key == "maxDepth") {
      throw std::runtime_error(
          "scene.offlineRender.maxDepth is no longer supported; use "
          "scene.offlineRender.maxBounce");
    }
    if (key == "integrator") {
      settings.integrator = value.as<std::string>();
    } else if (key == "samples") {
      settings.samples = value.as<u32>();
    } else if (key == "maxBounce") {
      settings.maxBounce = value.as<u32>();
    } else if (key == "seed") {
      settings.seed = value.as<u32>();
    } else if (key == "profile") {
      settings.profileName = value.as<std::string>();
    } else if (key == "shadows") {
      settings.shadows = value.as<bool>();
    } else if (key == "compareMode") {
      settings.compareMode = value.as<std::string>();
    } else {
      settings.extensionYamlByField.emplace(key, dumpYamlNode(value));
    }
  }

  if (settings.integrator != "primary-ray" &&
      settings.integrator != "path-tracing" &&
      settings.integrator != "probe-bake") {
    throw std::runtime_error("unsupported offline render integrator: " +
                             settings.integrator);
  }
  if (settings.samples == 0 || settings.maxBounce == 0) {
    throw std::runtime_error("offlineRender samples/maxBounce must be positive");
  }
  if (settings.compareMode != "shaded" && settings.compareMode != "albedo") {
    throw std::runtime_error("unsupported offlineRender compareMode: " +
                             settings.compareMode);
  }
  return settings;
}

[[nodiscard]] LX_core::offline::RenderProfileDocument
loadRenderProfileDocument(const YAML::Node &sceneNode) {
  if (!sceneNode) {
    return {};
  }

  if (const YAML::Node offlineRenderNode = sceneNode["offlineRender"];
      offlineRenderNode && offlineRenderNode["profiles"]) {
    throw std::runtime_error(
        "scene.offlineRender.profiles is no longer supported; use "
        "scene.outputProfiles plus scene.offlineRender");
  }

  const YAML::Node outputProfilesNode = sceneNode["outputProfiles"];
  const YAML::Node offlineRenderNode = sceneNode["offlineRender"];
  const YAML::Node defaultOutputProfileNode = sceneNode["defaultOutputProfile"];
  if (!outputProfilesNode && !offlineRenderNode && !defaultOutputProfileNode) {
    return {};
  }

  if (offlineRenderNode && offlineRenderNode["defaultProfile"]) {
    throw std::runtime_error(
        "scene.offlineRender.defaultProfile is no longer supported; use "
        "scene.defaultOutputProfile and scene.offlineRender.profile");
  }
  if (offlineRenderNode && offlineRenderNode["backend"]) {
    throw std::runtime_error(
        "scene.offlineRender.backend is no longer supported");
  }
  if (offlineRenderNode && offlineRenderNode["maxDepth"]) {
    throw std::runtime_error(
        "scene.offlineRender.maxDepth is no longer supported; use "
        "scene.offlineRender.maxBounce");
  }

  if (!outputProfilesNode || !outputProfilesNode.IsMap() ||
      outputProfilesNode.size() == 0) {
    throw std::runtime_error("scene.outputProfiles must be a non-empty map");
  }

  LX_core::offline::RenderProfileDocument document;
  if (defaultOutputProfileNode) {
    document.defaultOutputProfile = defaultOutputProfileNode.as<std::string>();
  }
  for (auto it = outputProfilesNode.begin(); it != outputProfilesNode.end();
       ++it) {
    const std::string name = it->first.as<std::string>();
    if (name.empty()) {
      throw std::runtime_error("scene.outputProfiles contains an empty name");
    }
    document.outputProfiles.emplace(name, loadOutputProfile(it->second, name));
  }
  if (document.outputProfiles.find(document.defaultOutputProfile) ==
      document.outputProfiles.end()) {
    throw std::runtime_error(
        "scene.defaultOutputProfile does not name an existing output profile: " +
        document.defaultOutputProfile);
  }

  document.offline = loadOfflineRenderSettings(offlineRenderNode);
  if (document.offline.profileName.empty()) {
    document.offline.profileName = document.defaultOutputProfile;
  }
  if (document.outputProfiles.find(document.offline.profileName) ==
      document.outputProfiles.end()) {
    throw std::runtime_error(
        "scene.offlineRender.profile does not name an existing output profile: " +
        document.offline.profileName);
  }
  return document;
}

[[nodiscard]] bool hasOutputCameraOverrides(
    const LX_core::offline::OutputCameraOverrides &overrides) {
  return overrides.fovY.has_value() || overrides.aspect.has_value() ||
         overrides.nearPlane.has_value() || overrides.farPlane.has_value() ||
         overrides.orthographicHeight.has_value() ||
         overrides.cullingMask.has_value();
}

void saveOutputCameraOverrides(
    YAML::Emitter &out,
    const LX_core::offline::OutputCameraOverrides &overrides) {
  out << YAML::BeginMap;
  if (overrides.fovY.has_value()) {
    out << YAML::Key << "fovY" << YAML::Value << *overrides.fovY;
  }
  if (overrides.aspect.has_value()) {
    out << YAML::Key << "aspect" << YAML::Value << *overrides.aspect;
  }
  if (overrides.nearPlane.has_value()) {
    out << YAML::Key << "nearPlane" << YAML::Value << *overrides.nearPlane;
  }
  if (overrides.farPlane.has_value()) {
    out << YAML::Key << "farPlane" << YAML::Value << *overrides.farPlane;
  }
  if (overrides.orthographicHeight.has_value()) {
    out << YAML::Key << "orthographicHeight" << YAML::Value
        << *overrides.orthographicHeight;
  }
  if (overrides.cullingMask.has_value()) {
    out << YAML::Key << "cullingMask" << YAML::Value
        << *overrides.cullingMask;
  }
  out << YAML::EndMap;
}

void saveOutputProfile(YAML::Emitter &out,
                       const LX_core::offline::OutputProfile &profile) {
  out << YAML::BeginMap;
  out << YAML::Key << "camera" << YAML::Value << profile.cameraPath;
  out << YAML::Key << "width" << YAML::Value << profile.width;
  out << YAML::Key << "height" << YAML::Value << profile.height;
  out << YAML::Key << "outputFormat" << YAML::Value << profile.outputFormat;
  out << YAML::Key << "outDir" << YAML::Value << profile.outDir.string();
  if (hasOutputCameraOverrides(profile.cameraOverrides)) {
    out << YAML::Key << "cameraOverrides" << YAML::Value;
    saveOutputCameraOverrides(out, profile.cameraOverrides);
  }
  for (const auto &[key, yamlText] : profile.extensionYamlByField) {
    out << YAML::Key << key << YAML::Value;
    emitRawYamlNode(out, yamlText);
  }
  out << YAML::EndMap;
}

void saveRenderProfileDocument(
    YAML::Emitter &out,
    const LX_core::offline::RenderProfileDocument &document) {
  if (document.empty()) {
    return;
  }
  out << YAML::Key << "defaultOutputProfile" << YAML::Value
      << document.defaultOutputProfile;
  out << YAML::Key << "outputProfiles" << YAML::Value << YAML::BeginMap;
  std::vector<std::string> profileNames;
  profileNames.reserve(document.outputProfiles.size());
  for (const auto &[name, _] : document.outputProfiles) {
    profileNames.push_back(name);
  }
  std::sort(profileNames.begin(), profileNames.end());
  for (const auto &name : profileNames) {
    out << YAML::Key << name << YAML::Value;
    saveOutputProfile(out, document.outputProfiles.at(name));
  }
  out << YAML::EndMap;
  out << YAML::Key << "offlineRender" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "integrator" << YAML::Value
      << document.offline.integrator;
  out << YAML::Key << "samples" << YAML::Value << document.offline.samples;
  out << YAML::Key << "maxBounce" << YAML::Value << document.offline.maxBounce;
  out << YAML::Key << "seed" << YAML::Value << document.offline.seed;
  out << YAML::Key << "profile" << YAML::Value << document.offline.profileName;
  out << YAML::Key << "shadows" << YAML::Value << document.offline.shadows;
  if (document.offline.compareMode != "shaded") {
    out << YAML::Key << "compareMode" << YAML::Value
        << document.offline.compareMode;
  }
  for (const auto &[key, yamlText] : document.offline.extensionYamlByField) {
    out << YAML::Key << key << YAML::Value;
    emitRawYamlNode(out, yamlText);
  }
  out << YAML::EndMap;
}

void saveEnvironmentState(YAML::Emitter &out,
                          const EnvironmentState &state) {
  if (state.empty()) {
    return;
  }
  out << YAML::Key << "environment" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "enabled" << YAML::Value << state.enabled;
  out << YAML::Key << "hdrUri" << YAML::Value << state.hdrUri;
  out << YAML::Key << "skyboxEnabled" << YAML::Value << state.skyboxEnabled;
  out << YAML::Key << "intensity" << YAML::Value << state.intensity;
  out << YAML::Key << "roughnessMipCount" << YAML::Value
      << state.roughnessMipCount;
  out << YAML::EndMap;
}

[[nodiscard]] std::optional<EditorCameraState>
loadEditorCamera(const YAML::Node &node) {
  if (!node) {
    return std::nullopt;
  }

  return EditorCameraState{
      .position = loadVec3(node["position"], "editor.editorCamera.position"),
      .rotationEulerDeg = loadVec3(node["rotationEulerDeg"],
                                   "editor.editorCamera.rotationEulerDeg"),
      .fovY = node["fovY"].as<float>(),
      .nearPlane = node["nearPlane"].as<float>(),
      .farPlane = node["farPlane"].as<float>(),
  };
}

void saveEditorCamera(YAML::Emitter &out, const EditorCameraState &state) {
  out << YAML::Key << "editor" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "editorCamera" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "position" << YAML::Value;
  saveVec3(out, state.position);
  out << YAML::Key << "rotationEulerDeg" << YAML::Value;
  saveVec3(out, state.rotationEulerDeg);
  out << YAML::Key << "fovY" << YAML::Value << state.fovY;
  out << YAML::Key << "nearPlane" << YAML::Value << state.nearPlane;
  out << YAML::Key << "farPlane" << YAML::Value << state.farPlane;
  out << YAML::EndMap;
  out << YAML::EndMap;
}

[[nodiscard]] SceneNodeDocument loadNodeDocument(const YAML::Node &node) {
  SceneNodeDocument entry;
  entry.nodeName =
      node["nodeName"] ? node["nodeName"].as<std::string>() : std::string{};
  if (entry.nodeName.empty()) {
    throw std::runtime_error("scene document node is missing nodeName");
  }
  entry.name = node["name"] ? node["name"].as<std::string>() : std::string{};
  entry.parentPath =
      node["parentPath"] ? node["parentPath"].as<std::string>() : std::string{};
  if (const auto transformNode = node["transform"]; transformNode) {
    entry.transform.translation =
        loadVec3(transformNode["translation"], "nodes[].transform.translation");
    entry.transform.rotation =
        loadQuat(transformNode["rotation"], "nodes[].transform.rotation");
    entry.transform.scale =
        loadVec3(transformNode["scale"], "nodes[].transform.scale");
  }
  if (const auto visibilityNode = node["visibilityMask"]; visibilityNode) {
    entry.visibilityMask = visibilityNode.as<LX_core::VisibilityLayerMask>();
  }
  if (const auto meshNode = node["mesh"]; meshNode && meshNode["uri"]) {
    entry.meshUri = meshNode["uri"].as<std::string>();
    validateSceneAssetUriInternal(*entry.meshUri, "nodes[].mesh.uri");
    if (const auto offlineNode = meshNode["offline"]; offlineNode) {
      entry.meshOfflineYaml = dumpYamlNode(offlineNode);
    }
  }
  if (const auto materialNode = node["material"];
      materialNode && materialNode["uri"]) {
    entry.materialUri = materialNode["uri"].as<std::string>();
    validateSceneAssetUriInternal(*entry.materialUri, "nodes[].material.uri");
    if (const auto offlineNode = materialNode["offline"]; offlineNode) {
      entry.materialOfflineYaml = dumpYamlNode(offlineNode);
    }
  }
  entry.proceduralMaterial = loadProceduralMaterialState(
      node["proceduralMaterial"], "nodes[].proceduralMaterial");
  entry.nodeMaterialOverrides = loadMaterialOverrideState(
      node["nodeMaterialOverrides"], "nodes[].nodeMaterialOverrides");
  entry.materialOverrides = loadMaterialOverrideState(
      node["materialOverrides"], "nodes[].materialOverrides");
  if (const auto cameraNode = node["camera"]; cameraNode) {
    const auto legacyEye =
        loadOptionalVec3(cameraNode["eye"], "nodes[].camera.eye");
    const auto legacyTarget =
        loadOptionalVec3(cameraNode["target"], "nodes[].camera.target");
    const auto legacyUp =
        loadOptionalVec3(cameraNode["up"], "nodes[].camera.up");
    entry.camera = loadCameraState(cameraNode);
    if (legacyEye.has_value() && legacyTarget.has_value()) {
      LX_core::Transform cameraTransform = makeWorldLookAtTransform(
          *legacyEye, *legacyTarget,
          legacyUp.value_or(LX_core::Vec3f{0.0f, 1.0f, 0.0f}));
      cameraTransform.scale = entry.transform.scale;
      entry.transform = cameraTransform;
    }
    if (const auto offlineNode = cameraNode["offline"]; offlineNode) {
      entry.cameraOfflineYaml = dumpYamlNode(offlineNode);
    }
  }
  if (const auto lightNode = node["light"]; lightNode) {
    entry.light = loadLightState(lightNode);
  } else if (const auto legacyLightNode = node["directionalLight"];
             legacyLightNode) {
    entry.light = loadLegacyDirectionalLightState(legacyLightNode);
  }
  if (const auto childrenNode = node["children"]; childrenNode) {
    if (!childrenNode.IsSequence()) {
      throw std::runtime_error("scene document children must be a sequence");
    }
    entry.children.reserve(childrenNode.size());
    for (const auto &childNode : childrenNode) {
      entry.children.push_back(loadNodeDocument(childNode));
    }
  }
  if (const auto offlineNode = node["offline"]; offlineNode) {
    entry.offlineYaml = dumpYamlNode(offlineNode);
  }
  return entry;
}

void validateExplicitRootNode(const SceneNodeDocument &rootNode) {
  if (rootNode.nodeName != kDefaultRootNodeName) {
    throw std::runtime_error("scene document root identity must use canonical "
                             "nodeName 'scene_root'");
  }
  if (!rootNode.name.empty() || !rootNode.parentPath.empty()) {
    throw std::runtime_error(
        "scene document root identity must use empty name and no parentPath");
  }
  if (rootNode.meshUri.has_value() || rootNode.meshOfflineYaml.has_value() ||
      rootNode.materialUri.has_value() ||
      rootNode.materialOfflineYaml.has_value() ||
      !rootNode.proceduralMaterial.empty() ||
      !rootNode.nodeMaterialOverrides.empty() ||
      !rootNode.materialOverrides.empty() || rootNode.camera.has_value() ||
      rootNode.cameraOfflineYaml.has_value() || rootNode.light.has_value() ||
      rootNode.offlineYaml.has_value()) {
    throw std::runtime_error("scene document root payload is unsupported");
  }
}

void saveNodeDocument(YAML::Emitter &out, const SceneNodeDocument &node) {
  out << YAML::BeginMap;
  out << YAML::Key << "nodeName" << YAML::Value << node.nodeName;
  out << YAML::Key << "name" << YAML::Value << node.name;
  out << YAML::Key << "transform" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "translation" << YAML::Value;
  saveVec3(out, node.transform.translation);
  out << YAML::Key << "rotation" << YAML::Value;
  saveQuat(out, node.transform.rotation);
  out << YAML::Key << "scale" << YAML::Value;
  saveVec3(out, node.transform.scale);
  out << YAML::EndMap;
  out << YAML::Key << "visibilityMask" << YAML::Value << node.visibilityMask;
  if (node.meshUri.has_value()) {
    out << YAML::Key << "mesh" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "uri" << YAML::Value << *node.meshUri;
    if (node.meshOfflineYaml.has_value()) {
      out << YAML::Key << "offline" << YAML::Value;
      emitRawYamlNode(out, *node.meshOfflineYaml);
    }
    out << YAML::EndMap;
  }
  if (node.materialUri.has_value()) {
    out << YAML::Key << "material" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "uri" << YAML::Value << *node.materialUri;
    if (node.materialOfflineYaml.has_value()) {
      out << YAML::Key << "offline" << YAML::Value;
      emitRawYamlNode(out, *node.materialOfflineYaml);
    }
    out << YAML::EndMap;
  }
  saveProceduralMaterialState(out, node.proceduralMaterial);
  saveMaterialOverrideState(out, "nodeMaterialOverrides",
                            node.nodeMaterialOverrides);
  saveMaterialOverrideState(out, "materialOverrides", node.materialOverrides);
  if (node.camera.has_value()) {
    saveCameraState(out, *node.camera, node.cameraOfflineYaml);
  }
  if (node.light.has_value()) {
    saveLightState(out, *node.light);
  }
  if (node.offlineYaml.has_value()) {
    out << YAML::Key << "offline" << YAML::Value;
    emitRawYamlNode(out, *node.offlineYaml);
  }
  if (!node.children.empty()) {
    out << YAML::Key << "children" << YAML::Value << YAML::BeginSeq;
    for (const auto &child : node.children) {
      saveNodeDocument(out, child);
    }
    out << YAML::EndSeq;
  }
  out << YAML::EndMap;
}

[[nodiscard]] std::vector<std::string> splitPathSegments(std::string path) {
  if (path.empty()) {
    path = "/";
  } else if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }

  std::vector<std::string> segments;
  std::string current;
  for (usize i = 1; i < path.size(); ++i) {
    if (path[i] == '/') {
      segments.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(path[i]);
  }
  if (path.size() > 1) {
    segments.push_back(current);
  }
  return segments;
}

[[nodiscard]] SceneNodeDocument *findNodeByPath(SceneNodeDocument &rootNode,
                                                const std::string &path) {
  const auto segments = splitPathSegments(path);
  SceneNodeDocument *current = &rootNode;
  for (const auto &segment : segments) {
    auto childIt = std::find_if(
        current->children.begin(), current->children.end(),
        [&segment](const SceneNodeDocument &child) {
          return child.name == segment || child.nodeName == segment;
        });
    if (childIt == current->children.end()) {
      return nullptr;
    }
    current = &*childIt;
  }
  return current;
}

[[nodiscard]] std::string canonicalPathSegment(const SceneNodeDocument &node) {
  return node.name.empty() ? node.nodeName : node.name;
}

[[nodiscard]] std::string canonicalizePath(const SceneNodeDocument &rootNode,
                                           const std::string &path) {
  const auto segments = splitPathSegments(path);
  if (segments.empty()) {
    return "/";
  }

  const SceneNodeDocument *current = &rootNode;
  std::string canonicalPath;
  for (const auto &segment : segments) {
    const auto childIt = std::find_if(
        current->children.begin(), current->children.end(),
        [&segment](const SceneNodeDocument &child) {
          return child.name == segment || child.nodeName == segment;
        });
    if (childIt == current->children.end()) {
      return path;
    }

    canonicalPath += "/";
    canonicalPath += canonicalPathSegment(*childIt);
    current = &*childIt;
  }

  return canonicalPath;
}

[[nodiscard]] SceneNodeDocument
normalizeLegacyNodes(std::vector<SceneNodeDocument> flatNodes) {
  SceneNodeDocument rootNode = makeDefaultRootNode();

  while (!flatNodes.empty()) {
    bool attachedAny = false;
    for (auto it = flatNodes.begin(); it != flatNodes.end();) {
      const std::string normalizedParentPath =
          it->parentPath.empty() ? "/" : it->parentPath;
      SceneNodeDocument *parent =
          findNodeByPath(rootNode, normalizedParentPath);
      if (parent == nullptr) {
        ++it;
        continue;
      }

      it->parentPath.clear();
      parent->children.push_back(std::move(*it));
      it = flatNodes.erase(it);
      attachedAny = true;
    }
    if (!attachedAny) {
      throw std::runtime_error(
          "scene document parent path not found during legacy normalization");
    }
  }

  return rootNode;
}

} // namespace

SceneDocument::SceneDocument(const SceneDocument &other) {
  if (other.m_impl) {
    m_impl = std::make_shared<SceneDocumentData>(
        *std::static_pointer_cast<const SceneDocumentData>(other.m_impl));
  }
}

SceneDocument &SceneDocument::operator=(const SceneDocument &other) {
  if (this == &other) {
    return *this;
  }

  if (!other.m_impl) {
    m_impl.reset();
    return *this;
  }

  m_impl = std::make_shared<SceneDocumentData>(
      *std::static_pointer_cast<const SceneDocumentData>(other.m_impl));
  return *this;
}

const std::string &SceneDocument::sceneName() const {
  static const std::string kDefaultSceneName = "Scene";
  if (!m_impl) {
    return kDefaultSceneName;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->sceneName;
}

void SceneDocument::setSceneName(std::string sceneName) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->sceneName =
      std::move(sceneName);
}

void SceneDocument::setGameplayCameraPath(std::string path) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->gameplayCameraPath =
      std::move(path);
}

const std::string &SceneDocument::gameplayCameraPath() const {
  static const std::string kDefaultGameplayCameraPath = "/game_cam";
  if (!m_impl) {
    return kDefaultGameplayCameraPath;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)
      ->gameplayCameraPath;
}

bool SceneDocument::hasEnvironment() const {
  return m_impl && std::static_pointer_cast<const SceneDocumentData>(m_impl)
                       ->environment.has_value();
}

const EnvironmentState &SceneDocument::environment() const {
  if (!m_impl) {
    throw std::runtime_error("scene document has no environment");
  }
  const auto &state =
      std::static_pointer_cast<const SceneDocumentData>(m_impl)->environment;
  if (!state.has_value()) {
    throw std::runtime_error("scene document has no environment");
  }
  return *state;
}

void SceneDocument::setEnvironment(EnvironmentState state) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->environment =
      std::move(state);
}

bool SceneDocument::hasRenderProfileDocument() const {
  return m_impl && std::static_pointer_cast<const SceneDocumentData>(m_impl)
                       ->renderProfileDocument.has_value();
}

const LX_core::offline::RenderProfileDocument &
SceneDocument::renderProfileDocument() const {
  if (!m_impl) {
    throw std::runtime_error("scene document has no render profile document");
  }
  const auto &profiles =
      std::static_pointer_cast<const SceneDocumentData>(m_impl)
          ->renderProfileDocument;
  if (!profiles.has_value()) {
    throw std::runtime_error("scene document has no render profile document");
  }
  return *profiles;
}

void SceneDocument::setRenderProfileDocument(
    LX_core::offline::RenderProfileDocument profiles) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->renderProfileDocument =
      std::move(profiles);
}

SceneNodeDocument &SceneDocument::mutableRootNode() {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  return std::static_pointer_cast<SceneDocumentData>(m_impl)->rootNode;
}

const SceneNodeDocument &SceneDocument::rootNode() const {
  static const SceneNodeDocument kDefaultRootNode = makeDefaultRootNode();
  if (!m_impl) {
    return kDefaultRootNode;
  }
  return std::static_pointer_cast<const SceneDocumentData>(m_impl)->rootNode;
}

bool SceneDocument::hasEditorCamera() const {
  return m_impl && std::static_pointer_cast<const SceneDocumentData>(m_impl)
                       ->editorCamera.has_value();
}

const EditorCameraState &SceneDocument::editorCamera() const {
  if (!m_impl) {
    throw std::runtime_error("scene document has no editor camera");
  }
  const auto &state =
      std::static_pointer_cast<const SceneDocumentData>(m_impl)->editorCamera;
  if (!state.has_value()) {
    throw std::runtime_error("scene document has no editor camera");
  }
  return *state;
}

void SceneDocument::setEditorCamera(const EditorCameraState &state) {
  if (!m_impl) {
    m_impl = std::make_shared<SceneDocumentData>();
  }
  std::static_pointer_cast<SceneDocumentData>(m_impl)->editorCamera = state;
}

SceneDocument loadSceneDocument(const std::filesystem::path &path) {
  const YAML::Node root = YAML::LoadFile(path.string());

  SceneDocument document;
  if (const YAML::Node sceneNode = root["scene"]; sceneNode) {
    if (const YAML::Node nameNode = sceneNode["name"]; nameNode) {
      document.setSceneName(nameNode.as<std::string>());
    }
    if (const YAML::Node gameplayCameraPathNode =
            sceneNode["gameplayCameraPath"];
        gameplayCameraPathNode) {
      document.setGameplayCameraPath(gameplayCameraPathNode.as<std::string>());
    }
    const EnvironmentState environment =
        loadEnvironmentState(sceneNode["environment"]);
    if (!environment.empty()) {
      document.setEnvironment(environment);
    }
    LX_core::offline::RenderProfileDocument renderProfiles =
        loadRenderProfileDocument(sceneNode);
    if (!renderProfiles.empty()) {
      document.setRenderProfileDocument(std::move(renderProfiles));
    }
  }

  if (const YAML::Node rootNode = root["root"]; rootNode.IsDefined()) {
    if (!rootNode.IsMap()) {
      throw std::runtime_error("scene document root must be a map");
    }
    auto parsedRoot = loadNodeDocument(rootNode);
    validateExplicitRootNode(parsedRoot);
    document.mutableRootNode() = std::move(parsedRoot);
  } else if (const YAML::Node nodesNode = root["nodes"];
             nodesNode && nodesNode.IsSequence()) {
    std::vector<SceneNodeDocument> flatNodes;
    flatNodes.reserve(nodesNode.size());
    for (const auto &node : nodesNode) {
      flatNodes.push_back(loadNodeDocument(node));
    }
    document.mutableRootNode() = normalizeLegacyNodes(std::move(flatNodes));
    document.setGameplayCameraPath(
        canonicalizePath(document.rootNode(), document.gameplayCameraPath()));
  }

  if (const YAML::Node editorNode = root["editor"]; editorNode) {
    if (const auto editorCamera = loadEditorCamera(editorNode["editorCamera"]);
        editorCamera.has_value()) {
      document.setEditorCamera(*editorCamera);
    }
  }

  return document;
}

void saveSceneDocument(const std::filesystem::path &path,
                       const SceneDocument &document) {
  validateExplicitRootNode(document.rootNode());

  if (const auto parentPath = path.parent_path(); !parentPath.empty()) {
    std::filesystem::create_directories(parentPath);
  }

  YAML::Emitter out;
  out << YAML::BeginMap;

  out << YAML::Key << "scene" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "name" << YAML::Value << document.sceneName();
  out << YAML::Key << "gameplayCameraPath" << YAML::Value
      << document.gameplayCameraPath();
  if (document.hasEnvironment()) {
    saveEnvironmentState(out, document.environment());
  }
  if (document.hasRenderProfileDocument()) {
    saveRenderProfileDocument(out, document.renderProfileDocument());
  }
  out << YAML::EndMap;

  out << YAML::Key << "root" << YAML::Value;
  saveNodeDocument(out, document.rootNode());

  if (document.hasEditorCamera()) {
    saveEditorCamera(out, document.editorCamera());
  }

  out << YAML::EndMap;

  std::ofstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open scene document for write: " +
                             path.string());
  }
  stream << out.c_str() << '\n';
}

bool isValidCacheUri(const std::string &uri) {
  try {
    if (!hasPrefix(uri, "cache://")) {
      return false;
    }
    validateSceneAssetUriInternal(uri, "cache URI");
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}

void validateSceneAssetUri(const std::string &uri, const char *fieldName) {
  validateSceneAssetUriInternal(uri, fieldName);
}

} // namespace LX_infra::scene_io
