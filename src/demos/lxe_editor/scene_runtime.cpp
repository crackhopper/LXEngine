#include "demos/lxe_editor/scene_runtime.hpp"

#include "core/asset/audio_spectrum_texture.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "demos/lxe_editor/builtin_asset_catalog.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/project_document.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_document.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

constexpr const char *RuntimeDebugDrawNodePrefix = "debug_draw_";
constexpr const char *LegacyCameraHelperNodeName = "helper_camera";
constexpr const char *LegacyLightHelperNodeName = "helper_light";
constexpr const char *BuiltinPrimitivePrefix =
    "builtin://lxe_editor/primitives/";
constexpr const char *BuiltinPatchPrefix = "builtin://lxe_editor/patches/";
constexpr const char *BuiltinPrimitiveMaterial =
    "assets/materials/blinnphong_lit.material";
constexpr const char *BuiltinModelPrefix = "assets/models/builtin/";
constexpr const char *kDefaultGroundMaterial =
    "assets/materials/blinnphong_lit.material";
constexpr const char *kDefaultHelmetMaterial =
    "assets/materials/blinnphong_textured.material";

struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  SceneDocument document;
  std::vector<std::filesystem::path> assetRoots;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
};

[[nodiscard]] bool pathStartsWith(const std::filesystem::path &path,
                                  const std::filesystem::path &prefix) {
  auto pathIt = path.begin();
  auto prefixIt = prefix.begin();
  for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt) {
    if (pathIt == path.end() || *pathIt != *prefixIt) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path
absoluteNormal(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::filesystem::path
stripLeadingAssetsComponent(const std::filesystem::path &uri) {
  std::filesystem::path stripped;
  bool skipped = false;
  for (const auto &component : uri) {
    if (!skipped && component.generic_string() == "assets") {
      skipped = true;
      continue;
    }
    stripped /= component;
  }
  return skipped ? stripped : uri;
}

[[nodiscard]] std::optional<std::filesystem::path>
resolveProjectAssetPath(const std::vector<std::filesystem::path> &assetRoots,
                        const std::filesystem::path &uri) {
  if (uri.empty() || uri.is_absolute()) {
    return std::nullopt;
  }
  const std::filesystem::path assetRelative = stripLeadingAssetsComponent(uri);
  for (const auto &assetRoot : assetRoots) {
    const std::filesystem::path candidate =
        (assetRoot / assetRelative).lexically_normal();
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<std::filesystem::path>
discoverProjectAssetRoots(const std::filesystem::path &scenePath) {
  std::vector<std::filesystem::path> roots;
  std::filesystem::path probe = scenePath.parent_path();
  while (!probe.empty()) {
    const std::filesystem::path projectPath = probe / "project.yaml";
    if (std::filesystem::exists(projectPath)) {
      const ProjectDocument project = loadProjectDocument(projectPath);
      const std::filesystem::path projectRoot = absoluteNormal(probe);
      for (const auto &assetRoot : project.assetRoots) {
        if (assetRoot.empty() || assetRoot.is_absolute()) {
          continue;
        }
        const std::filesystem::path resolved =
            (projectRoot / assetRoot).lexically_normal();
        if (pathStartsWith(resolved, projectRoot) &&
            std::filesystem::exists(resolved)) {
          roots.push_back(resolved);
        }
      }
      return roots;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  return roots;
}

[[nodiscard]] bool isRuntimeDebugDrawNodeName(const std::string &nodeName) {
  return nodeName.rfind(RuntimeDebugDrawNodePrefix, 0) == 0;
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const SceneNodeDocument &nodeDocument) {
  return isRuntimeDebugDrawNodeName(nodeDocument.nodeName);
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && isRuntimeDebugDrawNodeName(node->getNodeName());
}

[[nodiscard]] bool isLegacyEditorHelperName(const std::string &name) {
  return name == LegacyCameraHelperNodeName ||
         name == LegacyLightHelperNodeName;
}

[[nodiscard]] bool isBuiltinPrimitiveMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinPrimitivePrefix, 0) == 0;
}

[[nodiscard]] bool isBuiltinPatchMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinPatchPrefix, 0) == 0;
}

[[nodiscard]] bool isBuiltinModelMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinModelPrefix, 0) == 0;
}

[[nodiscard]] std::optional<std::string>
primitiveUriFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "primitive_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  const std::string shape = nodeName.substr(begin, suffixPos - begin);
  if (shape != "cube" && shape != "sphere" && shape != "plane" &&
      shape != "cylinder" && shape != "cone") {
    return std::nullopt;
  }
  return std::string(BuiltinPrimitivePrefix) + shape;
}

[[nodiscard]] std::optional<std::string>
patchUriFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "patch_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  const std::string shape = nodeName.substr(begin, suffixPos - begin);
  if (shape != "triangle" && shape != "square" && shape != "circle") {
    return std::nullopt;
  }
  return std::string(BuiltinPatchPrefix) + shape;
}

[[nodiscard]] std::optional<std::string>
modelAssetIdFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "model_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  return nodeName.substr(begin, suffixPos - begin);
}

[[nodiscard]] BuiltinAssetCatalog loadBuiltinAssetCatalog() {
  BuiltinAssetCatalog catalog;
  catalog.refresh(resolveRuntimePath("assets/models/builtin"));
  return catalog;
}

[[nodiscard]] std::string stripCopySuffix(const std::string &name) {
  const std::string suffix = ".copy";
  const auto suffixPos = name.rfind(suffix);
  if (suffixPos == std::string::npos) {
    return name;
  }
  const usize afterSuffix = suffixPos + suffix.size();
  if (afterSuffix == name.size()) {
    return name.substr(0, suffixPos);
  }
  if (afterSuffix + 4 == name.size() && name[afterSuffix] == '.' &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 1])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 2])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 3]))) {
    return name.substr(0, suffixPos);
  }
  return name;
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const SceneNodeDocument &nodeDocument) {
  return isLegacyEditorHelperName(nodeDocument.nodeName) ||
         isLegacyEditorHelperName(nodeDocument.name);
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && (isLegacyEditorHelperName(node->getNodeName()) ||
                  isLegacyEditorHelperName(node->getName()));
}

[[nodiscard]] std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string makeVec3Json(const LX_core::Vec3f &value) {
  std::ostringstream oss;
  oss << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":" << value.z
      << "}";
  return oss.str();
}

[[nodiscard]] const char *
materialParameterTypeName(const LX_core::MaterialParameterValueType type) {
  switch (type) {
  case LX_core::MaterialParameterValueType::Float:
    return "float";
  case LX_core::MaterialParameterValueType::Int:
    return "int";
  case LX_core::MaterialParameterValueType::Vec3:
    return "Vec3";
  case LX_core::MaterialParameterValueType::Vec4:
    return "Vec4";
  }
  return "unknown";
}

[[nodiscard]] std::string
makeMaterialValueJson(const LX_core::MaterialParameterValue &value) {
  switch (value.type) {
  case LX_core::MaterialParameterValueType::Float:
    return std::to_string(value.floatValue);
  case LX_core::MaterialParameterValueType::Int:
    return std::to_string(value.intValue);
  case LX_core::MaterialParameterValueType::Vec3:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "]";
  case LX_core::MaterialParameterValueType::Vec4:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "," +
           std::to_string(value.vectorValue.w) + "]";
  }
  return "null";
}

[[nodiscard]] LX_core::CommandResult makeCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}, {}};
}

[[nodiscard]] LX_core::CommandResult makeCommandOk(std::string message,
                                                   std::string structured) {
  return LX_core::CommandResult{
      true, std::move(message), std::move(structured), {}};
}

[[nodiscard]] const std::vector<std::string> &materialPresetUris() {
  static const std::vector<std::string> kPresets = {
      "assets/materials/blinnphong_lit.material",
      "assets/materials/blinnphong_default.material",
      "assets/materials/blinnphong_textured.material",
      "assets/materials/pbr_gold.material",
  };
  return kPresets;
}

[[nodiscard]] std::vector<std::string> discoverMaterialAssetUris() {
  std::vector<std::string> uris;
  const std::filesystem::path materialsDir =
      resolveRuntimePath("assets/materials");
  std::error_code error;
  if (!std::filesystem::exists(materialsDir, error)) {
    return uris;
  }
  for (const auto &entry :
       std::filesystem::directory_iterator(materialsDir, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    const std::string filename = path.filename().string();
    if (path.extension() == ".material") {
      uris.push_back("assets/materials/" + filename);
    }
  }
  std::sort(uris.begin(), uris.end());
  return uris;
}

[[nodiscard]] bool isAllowedMaterialPreset(const std::string &uri) {
  const auto &presets = materialPresetUris();
  if (std::find(presets.begin(), presets.end(), uri) != presets.end()) {
    return true;
  }
  const auto assetUris = discoverMaterialAssetUris();
  return std::find(assetUris.begin(), assetUris.end(), uri) != assetUris.end();
}

[[nodiscard]] std::string normalizeMaterialUri(const SceneNodeDocument &node) {
  if (node.materialUri.has_value()) {
    if (*node.materialUri == "builtin://lxe_editor/ground_material") {
      return kDefaultGroundMaterial;
    }
    return *node.materialUri;
  }
  if (node.meshUri == "builtin://lxe_editor/helmet") {
    return kDefaultHelmetMaterial;
  }
  if (node.meshUri == "builtin://lxe_editor/ground_mesh") {
    return kDefaultGroundMaterial;
  }
  return kDefaultGroundMaterial;
}

[[nodiscard]] bool shouldForceReceiverOnlyMesh(const std::string &meshUri) {
  return meshUri == "builtin://lxe_editor/primitives/plane" ||
         isBuiltinPatchMeshUri(meshUri);
}

void applyReceiverOnlyMeshMaterialPolicy(
    const SceneNodeDocument &node,
    const LX_core::MaterialInstanceSharedPtr &material) {
  if (!node.meshUri.has_value() || !material ||
      !shouldForceReceiverOnlyMesh(*node.meshUri)) {
    return;
  }
  if (material->isPassEnabled(LX_core::Pass_Shadow)) {
    material->setPassEnabled(LX_core::Pass_Shadow, false);
  }
}

[[nodiscard]] bool
documentNodeHasMaterialSurface(const SceneNodeDocument &node) {
  return node.meshUri.has_value() || node.materialUri.has_value();
}

[[nodiscard]] bool
materialHasBaseColor(const LX_core::MaterialInstanceSharedPtr &material) {
  if (!material) {
    return false;
  }
  const auto layout =
      material->getParameterBufferLayout(LX_core::StringID("MaterialUBO"));
  if (!layout.has_value()) {
    return false;
  }
  const auto &members = layout->get().members;
  return std::any_of(members.begin(), members.end(), [](const auto &member) {
    return member.name == "baseColor" &&
           member.type == LX_core::ShaderPropertyType::Vec3;
  });
}

void applyBaseColorIfSupported(
    const LX_core::MaterialInstanceSharedPtr &material,
    const std::optional<LX_core::Vec3f> &color) {
  if (!material || !color.has_value() || !materialHasBaseColor(material)) {
    return;
  }
  material->setParameter(LX_core::StringID("MaterialUBO"),
                         LX_core::StringID("baseColor"), *color);
  material->syncGpuData();
}

[[nodiscard]] bool splitMaterialParameterKey(const std::string &key,
                                             std::string &binding,
                                             std::string &member) {
  const usize dot = key.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size()) {
    return false;
  }
  binding = key.substr(0, dot);
  member = key.substr(dot + 1);
  return true;
}

[[nodiscard]] bool
coerceMaterialParameterValue(const LX_core::ShaderPropertyType reflectedType,
                             const LX_core::MaterialParameterValue &input,
                             LX_core::MaterialParameterValue &output) {
  output = input;
  if (reflectedType == LX_core::ShaderPropertyType::Float &&
      input.type == LX_core::MaterialParameterValueType::Int) {
    output.type = LX_core::MaterialParameterValueType::Float;
    output.floatValue = static_cast<float>(input.intValue);
    return true;
  }
  if (reflectedType == LX_core::ShaderPropertyType::Int &&
      input.type == LX_core::MaterialParameterValueType::Float) {
    output.type = LX_core::MaterialParameterValueType::Int;
    output.intValue = static_cast<i32>(input.floatValue);
    return true;
  }
  if (reflectedType == LX_core::ShaderPropertyType::Float) {
    return input.type == LX_core::MaterialParameterValueType::Float;
  }
  if (reflectedType == LX_core::ShaderPropertyType::Int) {
    return input.type == LX_core::MaterialParameterValueType::Int;
  }
  if (reflectedType == LX_core::ShaderPropertyType::Vec3) {
    return input.type == LX_core::MaterialParameterValueType::Vec3;
  }
  if (reflectedType == LX_core::ShaderPropertyType::Vec4) {
    return input.type == LX_core::MaterialParameterValueType::Vec4;
  }
  return false;
}

void applyGenericMaterialOverrides(
    const LX_core::MaterialInstanceSharedPtr &material,
    const MaterialOverrideState &overrides) {
  if (!material) {
    return;
  }
  for (const auto &[key, value] : overrides.parameters) {
    std::string binding;
    std::string member;
    if (!splitMaterialParameterKey(key, binding, member)) {
      throw std::runtime_error("invalid material override key: " + key);
    }
    const auto reflectedMember = material->findParameterMember(
        LX_core::StringID(binding), LX_core::StringID(member));
    if (!reflectedMember.has_value()) {
      throw std::runtime_error("material parameter not found for override: " +
                               key);
    }
    LX_core::MaterialParameterValue coerced;
    if (!coerceMaterialParameterValue(reflectedMember->get().type, value,
                                      coerced)) {
      throw std::runtime_error(
          "material parameter type mismatch for override: " + key);
    }
    material->setParameterValue(LX_core::StringID(binding),
                                LX_core::StringID(member), coerced);
  }
  material->syncGpuData();
}

void configureProceduralMaterialResources(
    const LX_core::MaterialInstanceSharedPtr &material,
    const ProceduralMaterialState &state) {
  if (!material || !state.enabled || !state.audioChannelBinding.has_value()) {
    return;
  }
  const LX_core::StringID bindingId(*state.audioChannelBinding);
  const auto canonical = material->getTemplate()
                             ? material->getTemplate()
                                   ->findCanonicalMaterialBinding(bindingId)
                             : std::nullopt;
  if (!canonical.has_value() ||
      canonical->get().type != LX_core::ShaderPropertyType::Texture2D) {
    return;
  }

  LX_core::AudioSpectrumTexture audio(bindingId);
  material->setTexture(bindingId, audio.sampler());
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadMaterialForSceneNode(const std::vector<std::filesystem::path> &assetRoots,
                         const std::string &uri,
                         const MaterialOverrideState &materialOverrides,
                         const MaterialOverrideState &nodeOverrides,
                         const ProceduralMaterialState &proceduralMaterial =
                             ProceduralMaterialState{}) {
  const std::filesystem::path materialPath =
      resolveProjectAssetPath(assetRoots, uri)
          .value_or(std::filesystem::path(uri));
  auto material = LX_infra::loadGenericMaterial(materialPath);
  if (!material) {
    throw std::runtime_error("failed to load material: " + uri);
  }
  applyBaseColorIfSupported(material, materialOverrides.baseColor);
  applyGenericMaterialOverrides(material, materialOverrides);
  applyBaseColorIfSupported(material, nodeOverrides.baseColor);
  applyGenericMaterialOverrides(material, nodeOverrides);
  configureProceduralMaterialResources(material, proceduralMaterial);
  return material;
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr loadModelMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const std::string &uri, const std::string &albedoTextureUri,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    const ProceduralMaterialState &proceduralMaterial =
        ProceduralMaterialState{}) {
  const std::filesystem::path materialPath =
      resolveProjectAssetPath(assetRoots, uri)
          .value_or(std::filesystem::path(uri));
  auto material = LX_infra::loadGenericMaterial(materialPath);
  if (!material) {
    throw std::runtime_error("failed to load material: " + uri);
  }
  bindModelAlbedoTexture(material, albedoTextureUri);
  applyBaseColorIfSupported(material, materialOverrides.baseColor);
  applyGenericMaterialOverrides(material, materialOverrides);
  applyBaseColorIfSupported(material, nodeOverrides.baseColor);
  applyGenericMaterialOverrides(material, nodeOverrides);
  configureProceduralMaterialResources(material, proceduralMaterial);
  return material;
}

[[nodiscard]] std::filesystem::path
normalizeDocumentPath(const std::filesystem::path &path) {
  if (path.empty()) {
    throw std::runtime_error("scene document path is empty");
  }
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr &node,
                       const char *nodeLabel) {
  if (!node) {
    throw std::runtime_error(std::string("missing scene node: ") + nodeLabel);
  }
  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error(std::string("missing camera component on ") +
                             nodeLabel);
  }
  return camera->get();
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
makeCameraNode(const std::string &nodeName, const std::string &displayName,
               const LX_core::VisibilityLayerMask cullingMask) {
  auto node = LX_core::SceneNode::create(nodeName);
  node->setName(displayName);
  const auto camera = node->addComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("failed to create camera component for " +
                             nodeName);
  }
  camera->get().setTarget(LX_core::RenderTarget{});
  camera->get().setCullingMask(cullingMask);
  return node;
}

[[nodiscard]] std::string cameraPathToDisplayName(const std::string &path,
                                                  const std::string &fallback) {
  if (path.empty() || path == "/") {
    return fallback;
  }
  const auto slash = path.find_last_of('/');
  const std::string name =
      slash == std::string::npos ? path : path.substr(slash + 1);
  return name.empty() ? fallback : name;
}

[[nodiscard]] SceneDocument makeEmptySceneDocument() {
  SceneDocument document;
  document.setSceneName("Scene");
  document.setGameplayCameraPath("/game_cam");

  auto &rootNode = document.mutableRootNode();
  SceneNodeDocument gameCameraNode;
  gameCameraNode.nodeName = "game_camera";
  gameCameraNode.name = "game_cam";
  gameCameraNode.camera = CameraNodeState{
      .eye = {0.0f, 2.0f, 6.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .type = LX_core::CameraType::Perspective,
      .fovY = 45.0f,
      .aspect = 16.0f / 9.0f,
      .nearPlane = 0.1f,
      .farPlane = 1000.0f,
      .left = -1.0f,
      .right = 1.0f,
      .bottom = -1.0f,
      .top = 1.0f,
      .cullingMask = LX_core::Layer_All & ~LX_core::Layer_EditorOverlay &
                     ~Layer_EditorHelper,
  };
  rootNode.children.push_back(std::move(gameCameraNode));

  SceneNodeDocument directionalLightNode;
  directionalLightNode.nodeName = "dir_light_node";
  directionalLightNode.name = "dir_light";
  directionalLightNode.visibilityMask = LX_core::Layer_All;
  directionalLightNode.light = LightNodeState{
      .kind = LightKind::Directional,
      .direction = {-0.3f, -1.0f, -0.5f},
      .color = {1.0f, 0.98f, 0.9f},
      .intensity = 1.0f,
  };
  rootNode.children.push_back(std::move(directionalLightNode));
  return document;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr buildRenderableNodeFromDocument(
    const SceneNodeDocument &nodeDocument,
    const std::vector<std::filesystem::path> &assetRoots) {
  if (!nodeDocument.meshUri.has_value()) {
    return LX_core::SceneNode::create(nodeDocument.nodeName);
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/helmet") {
    auto node = buildHelmetNode(
        resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf"));
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        materialComponent->get().setMaterialInstance(loadMaterialForSceneNode(
            assetRoots, uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides,
            nodeDocument.proceduralMaterial));
      } else {
        applyBaseColorIfSupported(
            materialComponent->get().getMaterialInstance(),
            nodeDocument.materialOverrides.baseColor);
        applyBaseColorIfSupported(
            materialComponent->get().getMaterialInstance(),
            nodeDocument.nodeMaterialOverrides.baseColor);
      }
    }
    return node;
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/ground_mesh") {
    auto node = buildGroundNode();
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material = loadMaterialForSceneNode(
            assetRoots, uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides,
            nodeDocument.proceduralMaterial);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isBuiltinPrimitiveMeshUri(*nodeDocument.meshUri)) {
    auto node =
        buildBuiltinPrimitiveNode(*nodeDocument.meshUri, nodeDocument.nodeName);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material = loadMaterialForSceneNode(
            assetRoots, uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides,
            nodeDocument.proceduralMaterial);
        applyReceiverOnlyMeshMaterialPolicy(nodeDocument, material);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isBuiltinPatchMeshUri(*nodeDocument.meshUri)) {
    auto node = buildBuiltinPatchNode(*nodeDocument.meshUri,
                                      nodeDocument.nodeName);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material = loadMaterialForSceneNode(
            assetRoots, uri, nodeDocument.materialOverrides,
            nodeDocument.nodeMaterialOverrides,
            nodeDocument.proceduralMaterial);
        applyReceiverOnlyMeshMaterialPolicy(nodeDocument, material);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isBuiltinModelMeshUri(*nodeDocument.meshUri)) {
    const std::string materialUri = normalizeMaterialUri(nodeDocument);
    const BuiltinAssetCatalog builtinAssets = loadBuiltinAssetCatalog();
    const auto asset = builtinAssets.findByMeshUri(*nodeDocument.meshUri);
    auto node = buildModelAssetNode(
        *nodeDocument.meshUri, materialUri,
        asset ? asset->albedoTextureUri : std::string{}, nodeDocument.nodeName);
    node->setName(nodeDocument.name);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        materialComponent->get().setMaterialInstance(
            loadModelMaterialForSceneNode(assetRoots, materialUri,
                                          asset ? asset->albedoTextureUri
                                                : std::string{},
                                          nodeDocument.materialOverrides,
                                          nodeDocument.nodeMaterialOverrides,
                                          nodeDocument.proceduralMaterial));
      }
    }
    return node;
  }

  return LX_core::SceneNode::create(nodeDocument.nodeName);
}

void applyNodeIdentityAndTransform(LX_core::SceneNode &node,
                                   const SceneNodeDocument &documentNode) {
  node.setName(documentNode.name);
  node.setLocalTransform(documentNode.transform);
  node.setVisibilityLayerMask(documentNode.visibilityMask);
}

void applyCameraState(LX_core::SceneNode &node,
                      LX_core::CameraComponent &camera,
                      const CameraNodeState &state) {
  camera.applyProjectionState(state.type, state.fovY, state.aspect,
                              state.nearPlane, state.farPlane, state.left,
                              state.right, state.bottom, state.top);
  camera.setTarget(LX_core::RenderTarget{});
  camera.setCullingMask(state.cullingMask & ~Layer_EditorHelper);
  camera.lookAt(state.eye, state.target, state.up);
  auto local = node.getLocalTransform();
  local.translation = state.eye;
  local.scale = state.type == LX_core::CameraType::Perspective
                    ? node.getLocalTransform().scale
                    : local.scale;
  node.setLocalTransform(local);
}

void configureDirectionalLight(LX_core::DirectionalLight &light,
                               const LightNodeState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setShadowStrength(state.shadowStrength);
  light.setShadowDistance(state.shadowDistance);
  light.setShadowCascadeCount(state.shadowCascadeCount);
}

void configurePointLight(LX_core::PointLight &light,
                         const LightNodeState &state) {
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
}

void configureSpotLight(LX_core::SpotLight &light,
                        const LightNodeState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
  light.setInnerConeDegrees(state.innerConeDegrees);
  light.setOuterConeDegrees(state.outerConeDegrees);
}

void buildSceneNodesRecursive(
    const SceneNodeDocument &nodeDocument,
    const LX_core::SceneNodeSharedPtr &parent,
    const std::shared_ptr<SceneRuntimeData> &runtime,
    std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> &nodesByPath) {
  if (isRuntimeDebugDrawNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping runtime-only scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }
  if (isLegacyEditorHelperNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping legacy editor helper scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }

  LX_core::SceneNodeSharedPtr node;
  if (nodeDocument.camera.has_value()) {
    node = makeCameraNode(nodeDocument.nodeName,
                          nodeDocument.name.empty() ? nodeDocument.nodeName
                                                    : nodeDocument.name,
                          nodeDocument.camera->cullingMask);
  } else {
    node = buildRenderableNodeFromDocument(nodeDocument, runtime->assetRoots);
  }

  applyNodeIdentityAndTransform(*node, nodeDocument);
  if (parent) {
    node->setParent(parent);
  }

  if (nodeDocument.camera.has_value()) {
    auto &camera =
        requireCameraComponent(node, nodeDocument.nodeName.c_str()).get();
    applyCameraState(*node, camera, *nodeDocument.camera);
    runtime->scene->addCamera(node);
  } else {
    runtime->scene->addRenderable(node);
  }

  nodesByPath[node->getPath()] = node;

  if (nodeDocument.light.has_value()) {
    switch (nodeDocument.light->kind) {
    case LightKind::Directional: {
      auto light = std::make_shared<LX_core::DirectionalLight>();
      configureDirectionalLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Point: {
      auto light = std::make_shared<LX_core::PointLight>();
      configurePointLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Spot: {
      auto light = std::make_shared<LX_core::SpotLight>();
      configureSpotLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    }
  }

  for (const auto &childDocument : nodeDocument.children) {
    buildSceneNodesRecursive(childDocument, node, runtime, nodesByPath);
  }
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
buildRuntimeFromDocument(const SceneDocument &document,
                         const std::optional<std::filesystem::path> &path,
                         std::vector<std::filesystem::path> assetRoots = {}) {
  auto runtime = std::make_shared<SceneRuntimeData>();
  runtime->documentPath = path;
  runtime->document = document;
  runtime->assetRoots = std::move(assetRoots);
  runtime->scene = LX_core::Scene::create(document.sceneName(), nullptr);

  while (!runtime->scene->getLights().empty()) {
    runtime->scene->removeLight(runtime->scene->getLights().front());
  }

  std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> nodesByPath;
  auto rootNode = runtime->scene->getRootNode();
  applyNodeIdentityAndTransform(*rootNode, document.rootNode());
  for (const auto &childDocument : document.rootNode().children) {
    buildSceneNodesRecursive(childDocument, rootNode, runtime, nodesByPath);
  }

  const std::string gameplayPath = document.gameplayCameraPath();
  const auto gameplayNodeIt = nodesByPath.find(gameplayPath);
  if (gameplayNodeIt == nodesByPath.end()) {
    throw std::runtime_error(
        "gameplay camera path not found in scene document: " + gameplayPath);
  }
  runtime->gameCameraNode = gameplayNodeIt->second;

  runtime->editorCameraNode =
      makeCameraNode("editor_camera", "editor_cam", LX_core::Layer_All);
  runtime->editorCameraNode->setVisibilityLayerMask(
      LX_core::Layer_EditorOverlay);
  auto &editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  auto &gameCamera =
      requireCameraComponent(runtime->gameCameraNode, "game_camera").get();
  if (document.hasEditorCamera()) {
    document.editorCamera().applyTo(*runtime->editorCameraNode, editorCamera);
  } else {
    editorCamera.lookAt(gameCamera.getEyePosition(), gameCamera.getLookTarget(),
                        gameCamera.getUpVector());
    runtime->editorCameraNode->setLocalTransform(
        runtime->gameCameraNode->getLocalTransform());
    editorCamera.applyProjectionState(
        gameCamera.getProjectionType(), gameCamera.getFovY(),
        gameCamera.getAspect(), gameCamera.getNearPlane(),
        gameCamera.getFarPlane(), gameCamera.getLeft(), gameCamera.getRight(),
        gameCamera.getBottom(), gameCamera.getTop());
  }
  runtime->scene->addCamera(runtime->editorCameraNode);

  return runtime;
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
requireRuntimeData(const std::shared_ptr<void> &impl) {
  if (!impl) {
    throw std::runtime_error("scene runtime is not loaded");
  }
  return std::static_pointer_cast<SceneRuntimeData>(impl);
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeByName(const SceneNodeDocument &node,
                       const std::string &nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (const auto &child : node.children) {
    if (const auto *match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument *
findDocumentNodeByName(SceneNodeDocument &node, const std::string &nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (auto &child : node.children) {
    if (auto *match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument *
findDocumentNodeForRuntimePath(SceneRuntimeData &runtime,
                               const std::string &path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode *node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.mutableRootNode(),
                                node->getNodeName());
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeForRuntimePath(const SceneRuntimeData &runtime,
                               const std::string &path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode *node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.rootNode(),
                                node->getNodeName());
}

void forEachDocumentNode(SceneNodeDocument &node,
                         const std::function<void(SceneNodeDocument &)> &fn) {
  fn(node);
  for (auto &child : node.children) {
    forEachDocumentNode(child, fn);
  }
}

void forEachRuntimeNode(const LX_core::SceneNodeSharedPtr &node,
                        const std::function<void(LX_core::SceneNode &)> &fn) {
  if (!node) {
    return;
  }
  fn(*node);
  for (const auto &child : node->getChildren()) {
    forEachRuntimeNode(child, fn);
  }
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeByNameOrCopySource(const SceneNodeDocument &node,
                                   const std::string &nodeName) {
  if (const auto *exact = findDocumentNodeByName(node, nodeName)) {
    return exact;
  }
  const std::string sourceName = stripCopySuffix(nodeName);
  if (sourceName == nodeName) {
    return nullptr;
  }
  return findDocumentNodeByName(node, sourceName);
}

[[nodiscard]] CameraNodeState
captureCameraState(const LX_core::CameraComponent &camera) {
  return CameraNodeState{
      .eye = camera.getEyePosition(),
      .target = camera.getLookTarget(),
      .up = camera.getUpVector(),
      .type = camera.getProjectionType(),
      .fovY = camera.getFovY(),
      .aspect = camera.getAspect(),
      .nearPlane = camera.getNearPlane(),
      .farPlane = camera.getFarPlane(),
      .left = camera.getLeft(),
      .right = camera.getRight(),
      .bottom = camera.getBottom(),
      .top = camera.getTop(),
      .cullingMask = camera.getCullingMask(),
  };
}

[[nodiscard]] LightNodeState
captureDirectionalLightState(const LX_core::DirectionalLight &light) {
  return LightNodeState{
      .kind = LightKind::Directional,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .shadowStrength = light.getShadowParams().z,
      .shadowDistance = light.getShadowDistance(),
      .shadowCascadeCount = light.getShadowCascadeCount(),
  };
}

[[nodiscard]] LightNodeState
capturePointLightState(const LX_core::PointLight &light) {
  return LightNodeState{
      .kind = LightKind::Point,
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
  };
}

[[nodiscard]] LightNodeState
captureSpotLightState(const LX_core::SpotLight &light) {
  return LightNodeState{
      .kind = LightKind::Spot,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
      .innerConeDegrees = light.getInnerConeDegrees(),
      .outerConeDegrees = light.getOuterConeDegrees(),
  };
}

[[nodiscard]] SceneDocument
captureSceneDocument(const std::shared_ptr<SceneRuntimeData> &runtime) {
  SceneDocument document;
  document.setSceneName(runtime->scene ? runtime->scene->getSceneName()
                                       : "Scene");
  document.setGameplayCameraPath(runtime->gameCameraNode
                                     ? runtime->gameCameraNode->getPath()
                                     : "/game_cam");
  const BuiltinAssetCatalog builtinAssets = loadBuiltinAssetCatalog();

  auto captureNode =
      [&](const auto &self,
          const LX_core::SceneNodeSharedPtr &node) -> SceneNodeDocument {
    SceneNodeDocument entry;
    entry.nodeName = node->getNodeName();
    entry.name = node->getName();
    entry.transform = node->getLocalTransform();
    entry.visibilityMask = node->getVisibilityLayerMask();

    if (const auto *existing = findDocumentNodeByNameOrCopySource(
            runtime->document.rootNode(), node->getNodeName())) {
      entry.visibilityMask = existing->visibilityMask;
      entry.meshUri = existing->meshUri;
      entry.materialUri = existing->materialUri;
      entry.proceduralMaterial = existing->proceduralMaterial;
      entry.nodeMaterialOverrides = existing->nodeMaterialOverrides;
      entry.materialOverrides = existing->materialOverrides;
    } else if (node->getName() == "helmet") {
      entry.meshUri = "builtin://lxe_editor/helmet";
      entry.materialUri = kDefaultHelmetMaterial;
    } else if (node->getName() == "ground") {
      entry.meshUri = "builtin://lxe_editor/ground_mesh";
      entry.materialUri = kDefaultGroundMaterial;
    } else if (const auto primitiveUri =
                   primitiveUriFromNodeName(node->getNodeName())) {
      entry.meshUri = *primitiveUri;
      entry.materialUri = BuiltinPrimitiveMaterial;
    } else if (const auto patchUri = patchUriFromNodeName(node->getNodeName())) {
      entry.meshUri = *patchUri;
      entry.materialUri = BuiltinPrimitiveMaterial;
    } else if (const auto assetId =
                   modelAssetIdFromNodeName(node->getNodeName())) {
      if (const auto asset = builtinAssets.findByAssetId(*assetId)) {
        entry.meshUri = asset->meshUri;
        entry.materialUri = asset->defaultMaterialUri;
      }
    }

    if (const auto camera = node->getComponent<LX_core::CameraComponent>();
        camera.has_value()) {
      entry.camera = captureCameraState(camera->get());
    }

    if (const auto light = runtime->scene->getLight(*node)) {
      if (const auto directional =
              std::dynamic_pointer_cast<LX_core::DirectionalLight>(light)) {
        entry.light = captureDirectionalLightState(*directional);
      } else if (const auto point =
                     std::dynamic_pointer_cast<LX_core::PointLight>(light)) {
        entry.light = capturePointLightState(*point);
      } else if (const auto spot =
                     std::dynamic_pointer_cast<LX_core::SpotLight>(light)) {
        entry.light = captureSpotLightState(*spot);
      }
    }

    for (const auto &child : node->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      entry.children.push_back(self(self, child));
    }

    return entry;
  };

  auto &rootEntry = document.mutableRootNode();
  if (runtime->scene && runtime->scene->getRootNode()) {
    const auto &rootNode = runtime->scene->getRootNode();
    rootEntry.nodeName = rootNode->getNodeName();
    rootEntry.name = rootNode->getName();
    rootEntry.parentPath.clear();
    rootEntry.transform = rootNode->getLocalTransform();
    rootEntry.visibilityMask = rootNode->getVisibilityLayerMask();
    rootEntry.meshUri.reset();
    rootEntry.materialUri.reset();
    rootEntry.proceduralMaterial = ProceduralMaterialState{};
    rootEntry.nodeMaterialOverrides = MaterialOverrideState{};
    rootEntry.materialOverrides = MaterialOverrideState{};
    rootEntry.camera.reset();
    rootEntry.light.reset();
    rootEntry.children.clear();

    for (const auto &child : rootNode->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      rootEntry.children.push_back(captureNode(captureNode, child));
    }
  }

  auto &editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  document.setEditorCamera(
      EditorCameraState::captureFrom(*runtime->editorCameraNode, editorCamera));
  return document;
}

} // namespace

void SceneRuntime::createEmptyScene() {
  m_impl = buildRuntimeFromDocument(makeEmptySceneDocument(), std::nullopt);
}

void SceneRuntime::loadFromDocumentPath(const std::filesystem::path &path) {
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  const SceneDocument document = loadSceneDocument(normalizedPath);
  m_impl = buildRuntimeFromDocument(document, normalizedPath,
                                    discoverProjectAssetRoots(normalizedPath));
}

void SceneRuntime::saveToCurrentDocumentPath() {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->documentPath.has_value()) {
    throw std::runtime_error("scene runtime has no current document path");
  }
  saveToDocumentPath(*runtime->documentPath);
}

void SceneRuntime::saveToDocumentPath(const std::filesystem::path &path) {
  const auto runtime = requireRuntimeData(m_impl);
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  SceneDocument document = captureSceneDocument(runtime);
  saveSceneDocument(normalizedPath, document);
  runtime->document = std::move(document);
  runtime->documentPath = normalizedPath;
}

std::optional<std::filesystem::path> SceneRuntime::documentPath() const {
  const auto runtime = requireRuntimeData(m_impl);
  return runtime->documentPath;
}

LX_core::SceneSharedPtr SceneRuntime::scene() const {
  return requireRuntimeData(m_impl)->scene;
}

LX_core::SceneNodeSharedPtr SceneRuntime::editorCameraNode() const {
  return requireRuntimeData(m_impl)->editorCameraNode;
}

LX_core::SceneNodeSharedPtr SceneRuntime::gameCameraNode() const {
  return requireRuntimeData(m_impl)->gameCameraNode;
}

std::optional<std::string>
SceneRuntime::materialUriForNode(const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  const auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return std::nullopt;
  }
  if (!documentNode->meshUri.has_value() &&
      !documentNode->materialUri.has_value()) {
    return std::nullopt;
  }
  return normalizeMaterialUri(*documentNode);
}

std::optional<LX_core::Vec3f>
SceneRuntime::nodeMaterialBaseColorForNode(const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return std::nullopt;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return std::nullopt;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value() ||
      !materialComponent->get().getMaterialInstance()) {
    return std::nullopt;
  }
  const auto value =
      materialComponent->get().getMaterialInstance()->readParameterValue(
          LX_core::StringID("MaterialUBO"), LX_core::StringID("baseColor"));
  if (!value.has_value() ||
      value->type != LX_core::MaterialParameterValueType::Vec3) {
    return std::nullopt;
  }
  return LX_core::Vec3f{value->vectorValue.x, value->vectorValue.y,
                        value->vectorValue.z};
}

bool SceneRuntime::nodeMaterialBaseColorEditable(
    const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return false;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return false;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  return materialComponent.has_value() &&
         materialHasBaseColor(materialComponent->get().getMaterialInstance());
}

std::vector<std::string> SceneRuntime::materialPresets() const {
  std::vector<std::string> out = materialPresetUris();
  const auto discovered = discoverMaterialAssetUris();
  out.insert(out.end(), discovered.begin(), discovered.end());
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::optional<LX_core::MaterialParameterValue>
SceneRuntime::nodeMaterialParameterForNode(const std::string &path,
                                           const std::string &binding,
                                           const std::string &member) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return std::nullopt;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return std::nullopt;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value() ||
      !materialComponent->get().getMaterialInstance()) {
    return std::nullopt;
  }
  return materialComponent->get().getMaterialInstance()->readParameterValue(
      LX_core::StringID(binding), LX_core::StringID(member));
}

std::vector<RuntimeMaterialParameterValue>
SceneRuntime::nodeMaterialParametersForNode(const std::string &path) const {
  std::vector<RuntimeMaterialParameterValue> out;
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return out;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return out;
  }
  const auto materialComponent =
      node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value() ||
      !materialComponent->get().getMaterialInstance()) {
    return out;
  }
  const auto material = materialComponent->get().getMaterialInstance();
  if (!material->getTemplate()) {
    return out;
  }
  for (const auto &[bindingId, binding] :
       material->getTemplate()->getCanonicalMaterialBindings()) {
    if (binding.type != LX_core::ShaderPropertyType::UniformBuffer &&
        binding.type != LX_core::ShaderPropertyType::StorageBuffer) {
      continue;
    }
    for (const auto &member : binding.members) {
      if (member.type != LX_core::ShaderPropertyType::Float &&
          member.type != LX_core::ShaderPropertyType::Int &&
          member.type != LX_core::ShaderPropertyType::Vec3 &&
          member.type != LX_core::ShaderPropertyType::Vec4) {
        continue;
      }
      const auto value = material->readParameterValue(
          bindingId, LX_core::StringID(member.name));
      if (!value.has_value()) {
        continue;
      }
      out.push_back(RuntimeMaterialParameterValue{
          .binding = binding.name, .member = member.name, .value = *value});
    }
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.binding + "." + a.member < b.binding + "." + b.member;
  });
  return out;
}

std::vector<std::string>
SceneRuntime::updateProceduralMaterials(const float totalTime,
                                        const LX_core::Vec2f &resolution) {
  std::vector<std::string> diagnostics;
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    diagnostics.push_back("scene runtime is not loaded");
    return diagnostics;
  }

  const auto writeRequiredFloat =
      [&](LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state, const std::string &path) {
        const auto member = material.findParameterMember(
            LX_core::StringID(state.binding), LX_core::StringID(state.timeMember));
        if (!member.has_value()) {
          diagnostics.push_back("procedural time member missing on " + path +
                                ": " + state.binding + "." +
                                state.timeMember);
          return;
        }
        if (member->get().type != LX_core::ShaderPropertyType::Float) {
          diagnostics.push_back("procedural time member is not Float on " +
                                path + ": " + state.binding + "." +
                                state.timeMember);
          return;
        }
        material.setParameter(LX_core::StringID(state.binding),
                              LX_core::StringID(state.timeMember), totalTime);
      };

  const auto writeRequiredResolution =
      [&](LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state, const std::string &path) {
        const auto member = material.findParameterMember(
            LX_core::StringID(state.binding),
            LX_core::StringID(state.resolutionMember));
        if (!member.has_value()) {
          diagnostics.push_back("procedural resolution member missing on " +
                                path + ": " + state.binding + "." +
                                state.resolutionMember);
          return;
        }
        if (member->get().type != LX_core::ShaderPropertyType::Vec4) {
          diagnostics.push_back("procedural resolution member is not Vec4 on " +
                                path + ": " + state.binding + "." +
                                state.resolutionMember);
          return;
        }
        const float width = std::max(resolution.x, 1.0f);
        const float height = std::max(resolution.y, 1.0f);
        material.setParameter(LX_core::StringID(state.binding),
                              LX_core::StringID(state.resolutionMember),
                              LX_core::Vec4f{width, height, 1.0f / width,
                                             1.0f / height});
      };

  const auto writeOptionalAudioBands =
      [&](LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state) {
        if (!state.audioBandsMember.has_value()) {
          return;
        }
        const auto member = material.findParameterMember(
            LX_core::StringID(state.binding),
            LX_core::StringID(*state.audioBandsMember));
        if (!member.has_value()) {
          return;
        }
        if (member->get().type != LX_core::ShaderPropertyType::Vec4) {
          diagnostics.push_back("procedural audioBands member is not Vec4: " +
                                state.binding + "." +
                                *state.audioBandsMember);
          return;
        }
        const float bass = 0.45f + 0.25f * std::sin(totalTime * 1.7f);
        const float mid = 0.35f + 0.20f * std::sin(totalTime * 2.3f + 0.4f);
        material.setParameter(LX_core::StringID(state.binding),
                              LX_core::StringID(*state.audioBandsMember),
                              LX_core::Vec4f{bass, mid, 0.0f, 0.0f});
      };

  const auto writeOptionalAudioChannel =
      [&](LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state) {
        if (!state.audioChannelBinding.has_value()) {
          return;
        }
        const LX_core::StringID bindingId(*state.audioChannelBinding);
        const auto sampler = material.getTexture(bindingId);
        if (!sampler || !sampler->texture()) {
          return;
        }
        sampler->update(LX_core::AudioSpectrumTexture::makeFakePixels(
            sampler->texture()->desc().width, totalTime));
      };

  forEachRuntimeNode(
      runtime->scene->getRootNode(), [&](LX_core::SceneNode &node) {
        auto *documentNode =
            findDocumentNodeByName(runtime->document.mutableRootNode(),
                                   node.getNodeName());
        if (!documentNode || !documentNode->proceduralMaterial.enabled) {
          return;
        }
        const auto materialComponent =
            node.getComponent<LX_core::MaterialComponent>();
        if (!materialComponent.has_value() ||
            !materialComponent->get().getMaterialInstance()) {
          diagnostics.push_back("procedural node has no material: " +
                                node.getPath());
          return;
        }
        auto material = materialComponent->get().getMaterialInstance();
        writeRequiredFloat(*material, documentNode->proceduralMaterial,
                           node.getPath());
        writeRequiredResolution(*material, documentNode->proceduralMaterial,
                                node.getPath());
        writeOptionalAudioBands(*material, documentNode->proceduralMaterial);
        writeOptionalAudioChannel(*material, documentNode->proceduralMaterial);
        material->syncGpuData();
      });

  return diagnostics;
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialUri(const std::string &path,
                                 const std::string &uri) {
  if (!isAllowedMaterialPreset(uri)) {
    return makeCommandError("unsupported material preset: " + uri);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }

  try {
    auto material = loadMaterialForSceneNode(
        runtime->assetRoots, uri, documentNode->materialOverrides,
        documentNode->nodeMaterialOverrides,
        documentNode->proceduralMaterial);
    applyReceiverOnlyMeshMaterialPolicy(*documentNode, material);
    auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
    if (materialComponent.has_value()) {
      materialComponent->get().setMaterialInstance(std::move(material));
    } else {
      node->addComponent<LX_core::MaterialComponent>(std::move(material));
    }
  } catch (const std::exception &error) {
    return makeCommandError(std::string("failed to set materialUri: ") +
                            error.what());
  }
  documentNode->materialUri = uri;

  return makeCommandOk("materialUri updated",
                       "{\"path\":\"" + jsonEscape(path) +
                           "\",\"materialUri\":\"" + jsonEscape(uri) + "\"}");
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialBaseColor(const std::string &path,
                                       const LX_core::Vec3f &color) {
  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.baseColor = color;
    auto material = loadMaterialForSceneNode(runtime->assetRoots, uri,
                                             documentNode->materialOverrides,
                                             nodeOverrides,
                                             documentNode->proceduralMaterial);
    if (!materialHasBaseColor(material)) {
      return makeCommandError(
          "material does not expose MaterialUBO.baseColor: " + uri);
    }
    applyReceiverOnlyMeshMaterialPolicy(*documentNode, material);
    materialComponent->get().setMaterialInstance(std::move(material));
    documentNode->materialUri = uri;
    documentNode->nodeMaterialOverrides = nodeOverrides;
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to set node material baseColor: ") + error.what());
  }

  return makeCommandOk("node material baseColor updated",
                       "{\"path\":\"" + jsonEscape(path) +
                           "\",\"baseColor\":" + makeVec3Json(color) + "}");
}

LX_core::CommandResult SceneRuntime::setNodeMaterialParameter(
    const std::string &path, const std::string &binding,
    const std::string &member, const LX_core::MaterialParameterValue &value) {
  if (binding == "MaterialUBO" && member == "baseColor") {
    if (value.type != LX_core::MaterialParameterValueType::Vec3) {
      return makeCommandError(
          "MaterialUBO.baseColor requires Vec3 material parameter value");
    }
    return setNodeMaterialBaseColor(path, LX_core::Vec3f{value.vectorValue.x,
                                                         value.vectorValue.y,
                                                         value.vectorValue.z});
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  const std::string key = binding + "." + member;
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.parameters[key] = value;
    auto material = loadMaterialForSceneNode(runtime->assetRoots, uri,
                                             documentNode->materialOverrides,
                                             nodeOverrides,
                                             documentNode->proceduralMaterial);
    const auto reflectedMember = material->findParameterMember(
        LX_core::StringID(binding), LX_core::StringID(member));
    if (!reflectedMember.has_value()) {
      return makeCommandError("material parameter not found: " + key);
    }
    applyReceiverOnlyMeshMaterialPolicy(*documentNode, material);
    materialComponent->get().setMaterialInstance(std::move(material));
    documentNode->materialUri = uri;
    documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to set node material parameter: ") + error.what());
  }

  return makeCommandOk(
      "node material parameter updated",
      "{\"path\":\"" + jsonEscape(path) + "\",\"binding\":\"" +
          jsonEscape(binding) + "\",\"member\":\"" + jsonEscape(member) +
          "\",\"type\":\"" + materialParameterTypeName(value.type) +
          "\",\"value\":" + makeMaterialValueJson(value) + "}");
}

LX_core::CommandResult
SceneRuntime::clearNodeMaterialParameter(const std::string &path,
                                         const std::string &binding,
                                         const std::string &member) {
  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  const std::string key = binding + "." + member;
  if (binding == "MaterialUBO" && member == "baseColor") {
    if (!documentNode->nodeMaterialOverrides.baseColor.has_value()) {
      return makeCommandError("node has no baseColor override: " + path);
    }
    try {
      MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
      nodeOverrides.baseColor.reset();
      auto material = loadMaterialForSceneNode(runtime->assetRoots, uri,
                                               documentNode->materialOverrides,
                                               nodeOverrides,
                                               documentNode->proceduralMaterial);
      applyReceiverOnlyMeshMaterialPolicy(*documentNode, material);
      materialComponent->get().setMaterialInstance(std::move(material));
      documentNode->materialUri = uri;
      documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
    } catch (const std::exception &error) {
      return makeCommandError(
          std::string("failed to clear node material baseColor: ") +
          error.what());
    }
    return makeCommandOk(
        "node material baseColor override cleared",
        "{\"path\":\"" + jsonEscape(path) +
            "\",\"binding\":\"MaterialUBO\",\"member\":\"baseColor\"}");
  }
  if (documentNode->nodeMaterialOverrides.parameters.find(key) ==
      documentNode->nodeMaterialOverrides.parameters.end()) {
    return makeCommandError("node has no material parameter override: " + key);
  }
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.parameters.erase(key);
    auto material = loadMaterialForSceneNode(runtime->assetRoots, uri,
                                             documentNode->materialOverrides,
                                             nodeOverrides,
                                             documentNode->proceduralMaterial);
    applyReceiverOnlyMeshMaterialPolicy(*documentNode, material);
    materialComponent->get().setMaterialInstance(std::move(material));
    documentNode->materialUri = uri;
    documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to clear node material parameter: ") +
        error.what());
  }

  return makeCommandOk("node material parameter override cleared",
                       "{\"path\":\"" + jsonEscape(path) + "\",\"binding\":\"" +
                           jsonEscape(binding) + "\",\"member\":\"" +
                           jsonEscape(member) + "\"}");
}

LX_core::CommandResult
SceneRuntime::applyMaterialOverride(const std::string &path,
                                    const std::string &field) {
  if (field != "baseColor") {
    return makeCommandError("unknown material override field: " + field);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  const auto color = documentNode->nodeMaterialOverrides.baseColor;
  if (!color.has_value()) {
    return makeCommandError("node has no baseColor override: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  usize updatedDocuments = 0;
  forEachDocumentNode(runtime->document.mutableRootNode(),
                      [&](SceneNodeDocument &candidate) {
                        if (!documentNodeHasMaterialSurface(candidate)) {
                          return;
                        }
                        if (normalizeMaterialUri(candidate) != uri) {
                          return;
                        }
                        candidate.materialUri = uri;
                        candidate.materialOverrides.baseColor = *color;
                        ++updatedDocuments;
                      });

  usize updatedRuntimeNodes = 0;
  forEachRuntimeNode(
      runtime->scene->getRootNode(), [&](LX_core::SceneNode &node) {
        auto *candidateDocument = findDocumentNodeByName(
            runtime->document.mutableRootNode(), node.getNodeName());
        if (!candidateDocument ||
            !documentNodeHasMaterialSurface(*candidateDocument) ||
            normalizeMaterialUri(*candidateDocument) != uri) {
          return;
        }
        const auto materialComponent =
            node.getComponent<LX_core::MaterialComponent>();
        if (!materialComponent.has_value()) {
          return;
        }
        const auto effectiveNodeOverride =
            candidateDocument->nodeMaterialOverrides.baseColor;
        try {
          auto material = loadMaterialForSceneNode(
              runtime->assetRoots, uri, candidateDocument->materialOverrides,
              MaterialOverrideState{.baseColor = effectiveNodeOverride},
              candidateDocument->proceduralMaterial);
          applyReceiverOnlyMeshMaterialPolicy(*candidateDocument, material);
          materialComponent->get().setMaterialInstance(std::move(material));
          ++updatedRuntimeNodes;
        } catch (const std::exception &error) {
          std::cerr << "[lxe_editor] failed to apply material override to "
                    << node.getPath() << ": " << error.what() << "\n";
        }
      });

  return makeCommandOk(
      "material baseColor override applied",
      "{\"materialUri\":\"" + jsonEscape(uri) +
          "\",\"updatedDocuments\":" + std::to_string(updatedDocuments) +
          ",\"updatedRuntimeNodes\":" + std::to_string(updatedRuntimeNodes) +
          ",\"baseColor\":" + makeVec3Json(*color) + "}");
}

} // namespace LX_demo::lxe_editor
