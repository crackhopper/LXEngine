#include "infra/scene_asset/scene_material_loader.hpp"

#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace LX_infra::scene_asset {
namespace {

using LX_core::MaterialInstanceSharedPtr;
using LX_core::ShaderPropertyType;
using LX_core::StringID;
using LX_infra::scene_io::MaterialOverrideState;

[[nodiscard]] bool isGltfMeshUri(const std::string &uri) {
  std::string extension = std::filesystem::path(uri).extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".gltf" || extension == ".glb";
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
coerceMaterialParameterValue(const ShaderPropertyType reflectedType,
                             const LX_core::MaterialParameterValue &input,
                             LX_core::MaterialParameterValue &output) {
  output = input;
  if (reflectedType == ShaderPropertyType::Float &&
      input.type == LX_core::MaterialParameterValueType::Int) {
    output.type = LX_core::MaterialParameterValueType::Float;
    output.floatValue = static_cast<float>(input.intValue);
    return true;
  }
  if (reflectedType == ShaderPropertyType::Int &&
      input.type == LX_core::MaterialParameterValueType::Float) {
    output.type = LX_core::MaterialParameterValueType::Int;
    output.intValue = static_cast<i32>(input.floatValue);
    return true;
  }
  if (reflectedType == ShaderPropertyType::Float) {
    return input.type == LX_core::MaterialParameterValueType::Float;
  }
  if (reflectedType == ShaderPropertyType::Int) {
    return input.type == LX_core::MaterialParameterValueType::Int;
  }
  if (reflectedType == ShaderPropertyType::Vec3) {
    return input.type == LX_core::MaterialParameterValueType::Vec3;
  }
  if (reflectedType == ShaderPropertyType::Vec4) {
    return input.type == LX_core::MaterialParameterValueType::Vec4;
  }
  return false;
}

void applyGenericMaterialOverrides(const MaterialInstanceSharedPtr &material,
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
    const auto reflectedMember =
        material->findParameterMember(StringID(binding), StringID(member));
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
    material->setParameterValue(StringID(binding), StringID(member), coerced);
  }
  material->syncGpuData();
}

} // namespace

void applySceneMaterialOverrides(const MaterialInstanceSharedPtr &material,
                                 const MaterialOverrideState &overrides) {
  applyGenericMaterialOverrides(material, overrides);
}

MaterialInstanceSharedPtr
loadSceneMaterialBinding(const SceneMaterialBindingLoadDesc &desc) {
  if (!desc.resolveAssetPath) {
    throw std::logic_error("scene material loader requires path resolver");
  }
  if (!desc.loadGenericMaterial) {
    throw std::logic_error("scene material loader requires generic loader");
  }

  MaterialInstanceSharedPtr material;
  if (desc.binding.source == "gltf") {
    if (!desc.meshUri.has_value() || !isGltfMeshUri(*desc.meshUri)) {
      throw std::runtime_error("source: gltf material requires glTF mesh");
    }
    const std::filesystem::path meshPath = desc.resolveAssetPath(*desc.meshUri);
    if (desc.binding.uri.empty()) {
      throw std::runtime_error(
          "source: gltf material requires explicit material uri");
    }
    const std::filesystem::path materialPath =
        desc.resolveAssetPath(desc.binding.uri);
    material = loadGltfSceneAsset(meshPath, materialPath).material;
  } else {
    const std::filesystem::path materialPath =
        desc.resolveAssetPath(desc.binding.uri);
    material = desc.loadGenericMaterial(materialPath);
  }

  if (!material) {
    throw std::runtime_error("failed to load scene material binding");
  }

  applySceneMaterialOverrides(material, desc.binding.materialOverrides);
  applySceneMaterialOverrides(material, desc.binding.nodeMaterialOverrides);
  applySceneMaterialOverrides(material, desc.materialOverrides);
  applySceneMaterialOverrides(material, desc.nodeMaterialOverrides);
  return material;
}

} // namespace LX_infra::scene_asset
